#ifndef DE_GEO_PROJECTOR_CORE_H_
#define DE_GEO_PROJECTOR_CORE_H_

/**
 * @brief Shared geo-referenced detection projector (was two copies:
 * de_mavlink CGeoProjector P3-07 and de_comm CStaticGeoProjector SU-04).
 *
 * Takes observation frames (obs 6546, helpers/obs.hpp), projects every
 * observation's box centre onto the ground and fires "detect.detected".
 * The only thing that differs between users is WHERE THE POSE COMES FROM,
 * which is the IPoseSource they implement:
 *
 *   de_mavlink  FcbPoseRing   ring buffer of ATTITUDE/GLOBAL_POSITION_INT
 *                             samples aligned to the frame time, plus the
 *                             downward rangefinder
 *   de_comm     FixedPose     the configured static location + heading,
 *                             mount height above flat ground, optional live
 *                             gimbal angles
 *
 * Behaviour owned here, once:
 *   - class comes from obs.cls only (no 1076 selection, no class list);
 *   - observations below min_conf are skipped;
 *   - camera model/mount are looked up by the frame's cam_id. The lookup
 *     table comes from the per-unit camera registry file
 *     ("camera_registry" config key, helpers/camera_registry.hpp) when
 *     one is configured - an obs whose cam_id is not in the registry is
 *     dropped with a logged error; without a registry the "cameras"
 *     config map is used, falling back to the top-level defaults;
 *   - throttle key is the track (cam_id + trk) when the observation has a
 *     trk, else the class: rate_hz cap + moved-or-5s gate;
 *   - the "detect" advert, enable/disable invoke, "ai" and
 *     "last_fix_age_s" state.
 *
 * Pure header: no mavlink, no bus, no console. Outbound traffic goes
 * through injected callbacks so tests can record it.
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json_nlohmann.hpp"
#include "obs.hpp"
#include "geo_projection.hpp"
#include "camera_registry.hpp"
#include "../de_databus/messages.hpp"

namespace de
{
namespace geo
{

    /// camera model + mount for one cam_id
    struct GeoCameraEntry
    {
        GeoCameraParams camera;
        GeoMountParams  mount = {-90.0, 0.0, 0.0};
    };

    struct GeoProjectorConfig
    {
        bool   enabled        = false;
        GeoCameraEntry defaults;               ///< hfov/vfov/k1/k2/mount_* at the top level
        std::map<std::string, GeoCameraEntry> cameras;   ///< per cam_id overrides
        double mount_height_m = 0.0;           ///< static units only
        int    latency_ms     = 150;           ///< used when the frame time is unusable
        double min_conf       = 0.6;
        double rate_hz        = 1.0;
        double sigma_att_deg  = 2.0;
        double sigma_alt_m    = 2.0;
    };

    /// vehicle/unit pose at the time a frame was captured
    struct GeoPoseFix
    {
        GeoAttitude att  = {0.0, 0.0, 0.0};
        GeoPose     pose = {0.0, 0.0, 0.0};
        double rangefinder_m = -1.0;           ///< downward range, <0 = none
        bool   has_mount_pitch = false;        ///< live gimbal angle replaces the camera entry's mount pitch
        bool   has_mount_yaw   = false;        ///< ... and yaw (roll always comes from the camera entry)
        double mount_pitch_deg = 0.0;
        double mount_yaw_deg   = 0.0;
    };

    class IPoseSource
    {
        public:
            virtual ~IPoseSource() = default;

            /// pose at t_frame_us (monotonic us); false when none is available
            virtual bool poseAt (const uint64_t t_frame_us, GeoPoseFix& out) = 0;
    };


    class CGeoProjectorCore
    {
        public:

            /// reject a frame time older than this / in the future and use now - latency
            static constexpr uint64_t FRAME_TIME_WINDOW_US = 2000000;
            /// re-report a steady detection after this gap
            static constexpr uint64_t STEADY_RESEND_US     = 5000000;

            /// read the "geo_projection" block; keeps the previous value for a missing key
            void init (const nlohmann::json& c)
            {
                if (!c.is_object()) return;

                m_config.enabled        = c.value("enabled",        m_config.enabled);
                readEntry(c, m_config.defaults, m_config.defaults);
                m_config.mount_height_m = c.value("mount_height_m", m_config.mount_height_m);
                m_config.latency_ms     = c.value("latency_ms",     m_config.latency_ms);
                m_config.min_conf       = c.value("min_conf",       m_config.min_conf);
                m_config.rate_hz        = c.value("rate_hz",        m_config.rate_hz);
                m_config.sigma_att_deg  = c.value("sigma_att_deg",  m_config.sigma_att_deg);
                m_config.sigma_alt_m    = c.value("sigma_alt_m",    m_config.sigma_alt_m);

                m_config.cameras.clear();
                if (c.contains("cameras") && c["cameras"].is_object())
                {
                    for (auto it = c["cameras"].begin(); it != c["cameras"].end(); ++it)
                    {
                        if (!it.value().is_object()) continue;
                        GeoCameraEntry e = m_config.defaults;
                        readEntry(it.value(), e, m_config.defaults);
                        m_config.cameras[it.key()] = e;
                    }
                }

                // CA-0012: the per-unit registry file, when configured, is
                // the authoritative cam_id table - unknown cam_ids are
                // dropped in onObservations.
                m_registry_active = false;
                if (c.contains("camera_registry") && c["camera_registry"].is_string())
                {
                    loadCameraRegistry(c["camera_registry"].get<std::string>());
                }
            }

            /**
             * @brief load the per-unit camera registry file; each entry
             * becomes a cam_id in the lookup table (mount + hfov/vfov +
             * k1/k2 lens model). Once a registry is loaded, observations
             * for a cam_id that is not in it are dropped with an error
             * via fn_log_error - a wrong-camera projection is worse than
             * none.
             */
            bool loadCameraRegistry (const std::string& path)
            {
                de::camera::CCameraRegistry reg;
                std::string err;
                if (!reg.load(path, err))
                {
                    logError("geo_projection: " + err);
                    return false;
                }

                for (const auto& kv : reg.cameras())
                {
                    m_config.cameras[kv.first] = entryFromRegistry(kv.second);
                }
                if (!err.empty())
                {
                    // some entries were skipped - surface it but keep going
                    logError("geo_projection: " + err);
                }
                m_registry_active = true;
                return true;
            }

            /// true when a registry file drives the cam_id table
            bool registryActive () const { return m_registry_active; }

            const GeoProjectorConfig& config () const { return m_config; }
            bool isEnabled () const { return m_config.enabled; }

            /// runtime toggle (detect.enable/disable); returns true when it changed
            bool setEnabled (const bool enabled)
            {
                if (m_config.enabled == enabled) return false;
                m_config.enabled = enabled;
                publishState({{"enabled", enabled}});
                return true;
            }

            /// the advert is registered / withdrawn (enables state publishing)
            void onAdvertised (const bool advertised)
            {
                m_caps_advertised = advertised;
                if (!advertised) m_last_fix_us = 0;
            }
            bool advertised () const { return m_caps_advertised; }

            /// ns "detect" advert - identical for every owner but module/version
            nlohmann::json buildAdvert (const std::string& module, const std::string& version) const
            {
                using J = nlohmann::json;

                const J actions =
                {
                    {"enable",  {{"desc", "Turn the AI-detection ground projector on"}}},
                    {"disable", {{"desc", "Turn the AI-detection ground projector off"}}}
                };

                const J events =
                {
                    {"detected", {
                        {"desc", "An observation was projected to the ground"},
                        {"payload", {
                            {"class",   {{"type", "string"}, {"optional", true},
                                         {"desc", "observation class (obs.cls)"}}},
                            {"conf",    {{"type", "number"}}},
                            {"pos",     {{"type", "geo"}, {"desc", "[lat,lng,0]"}}},
                            {"unc_m",   {{"type", "number"}, {"desc", "~95% radius (m)"}}},
                            {"range_m", {{"type", "number"}, {"desc", "horizontal distance (m)"}}},
                            {"src",     {{"type", "enum"}, {"values", {"ai"}}}},
                            {"trk",     {{"type", "int"}, {"optional", true},
                                         {"desc", "track id of the observation"}}},
                            {"cam_id",  {{"type", "string"}, {"optional", true},
                                         {"desc", "camera the observation came from"}}}
                        }}
                    }}
                };

                const J state =
                {
                    {"enabled",        {{"type", "bool"}}},
                    {"ai",             {{"type", "enum"},
                                        {"values", {"lost", "detected", "enabled", "disabled"}},
                                        {"optional", true},
                                        {"desc", "last AI_Recognition_STATUS seen"}}},
                    {"last_fix_age_s", {{"type", "number"},
                                        {"desc", "seconds since the last projected detection"}}}
                };

                return
                {
                    {"schema",  "de.cap/1"},
                    {"ns",      "detect"},
                    {"module",  module},
                    {"ver",     version},
                    {"actions", actions},
                    {"events",  events},
                    {"state",   state}
                };
            }

            /// detect.enable / detect.disable; err is set for anything else
            nlohmann::json invoke (const std::string& ns, const std::string& act, std::string& err)
            {
                if (ns != "detect")
                {
                    err = "unknown namespace " + ns;
                    return nlohmann::json::object();
                }

                if (act == "enable")  { setEnabled(true);  return {{"enabled", true}}; }
                if (act == "disable") { setEnabled(false); return {{"enabled", false}}; }

                err = "unknown action " + ns + "." + act;
                return nlohmann::json::object();
            }

            /// AI_Recognition_STATUS (1077): mirrors the AI module's state; class lists are ignored
            void onAIStatus (const nlohmann::json& cmd)
            {
                if (!cmd.contains("a") || !cmd["a"].is_number_integer()) return;
                const int a = cmd["a"].get<int>();

                if (a == TrackingTarget_STATUS_AI_Recognition_CLASS_LIST) return;
                if (a == m_ai_status) return;
                m_ai_status = a;

                publishState({{"ai", aiStateName(a)}});
            }

            /// ~1 Hz housekeeping: keeps "last_fix_age_s" current
            void tick ()
            {
                if (!m_caps_advertised || !m_config.enabled) return;

                const uint64_t now = nowUs();
                if (now - m_last_state_pub_us < 1000000) return;
                m_last_state_pub_us = now;

                if (m_last_fix_us == 0) return;
                publishState({{"last_fix_age_s", (now - m_last_fix_us) / 1e6}});
            }

            /**
             * @brief Project one observation frame.
             * @param ms     the 6546 "ms" object
             * @return number of detect.detected events fired (0 for a frame
             *         that is invalid, below min_conf, has no pose, or is throttled)
             */
            int onObservations (const nlohmann::json& ms, IPoseSource& source)
            {
                if (!m_config.enabled) return 0;

                de::ai::ObservationFrame frame;
                std::string err;
                if (!de::ai::obsParse(ms, frame, err)) return 0;
                if (frame.obs.empty()) return 0;

                const uint64_t now = nowUs();

                // frame time on the pose clock; an unusable stamp falls back to now - latency
                uint64_t t_frame = (uint64_t) frame.tm_capture;
                if (t_frame == 0 || t_frame > now + 200000 || now - t_frame > FRAME_TIME_WINDOW_US)
                {
                    t_frame = now - (uint64_t) m_config.latency_ms * 1000;
                }

                GeoPoseFix fix;
                if (!source.poseAt(t_frame, fix)) return 0;

                const auto cam_it = m_config.cameras.find(frame.cam_id);
                if (cam_it == m_config.cameras.end() && m_registry_active)
                {
                    // CA-0012: the registry is authoritative - an obs from a
                    // camera it does not know is dropped, not projected with
                    // the wrong mount. Logged once per cam_id.
                    if (m_dropped_cameras.insert(frame.cam_id).second)
                    {
                        logError("geo_projection: dropping obs for cam_id '" + frame.cam_id
                                 + "' - not in the camera registry");
                    }
                    return 0;
                }
                const GeoCameraEntry& cam = (cam_it != m_config.cameras.end()) ? cam_it->second : m_config.defaults;
                GeoMountParams mount = cam.mount;
                if (fix.has_mount_pitch) mount.pitch_deg = fix.mount_pitch_deg;
                if (fix.has_mount_yaw)   mount.yaw_deg   = fix.mount_yaw_deg;

                int fired = 0;
                for (const de::ai::Observation& o : frame.obs)
                {
                    if (o.conf < m_config.min_conf) continue;

                    const double u = geoBoxToCentered(o.x) + o.w / 2.0;
                    const double v = geoBoxToCentered(o.y) + o.h / 2.0;

                    const GeoProjectionResult r = geoProjectToGround(
                        u, v, o.w, o.h, cam.camera, mount, fix.att, fix.pose,
                        m_config.sigma_att_deg, m_config.sigma_alt_m, fix.rangefinder_m);
                    if (!r.ok) continue;

                    // a successful projection counts as a fix even if the event is throttled
                    m_last_fix_us = now;

                    if (!passesThrottle(frame.cam_id, o, r, now)) continue;

                    nlohmann::json payload =
                    {
                        {"conf",    o.conf},
                        {"pos",     {r.lat, r.lng, 0}},
                        {"unc_m",   r.unc_m},
                        {"range_m", r.ground_range_m},
                        {"src",     "ai"},
                        {"class",   o.cls},
                        {"cam_id",  frame.cam_id}
                    };
                    if (o.has_trk) payload["trk"] = o.trk;

                    if (fn_fire_event) fn_fire_event(payload);
                    m_last_state_pub_us = now;
                    ++fired;
                }

                return fired;
            }

            /// drop all runtime state (tests, glue reset)
            void reset ()
            {
                m_config = GeoProjectorConfig();
                m_throttle.clear();
                m_dropped_cameras.clear();
                m_ai_status = -1;
                m_last_fix_us = 0;
                m_last_state_pub_us = 0;
                m_caps_advertised = false;
                m_registry_active = false;
            }

            uint64_t lastFixUs () const { return m_last_fix_us; }

            // ------------ injected plumbing ------------------------------

            /// MODULE_STATE for ns "detect" (changed keys)
            std::function<void(const nlohmann::json& changed)> fn_publish_state;
            /// detect.detected payload
            std::function<void(const nlohmann::json& payload)> fn_fire_event;
            /// error lines (dropped obs, registry problems)
            std::function<void(const std::string& line)> fn_log_error;
            /// monotonic us; tests inject a manual clock
            std::function<uint64_t()> fn_now_us;

        private:

            static const char* aiStateName (const int status)
            {
                switch (status)
                {
                    case TrackingTarget_STATUS_AI_Recognition_LOST:     return "lost";
                    case TrackingTarget_STATUS_AI_Recognition_DETECTED: return "detected";
                    case TrackingTarget_STATUS_AI_Recognition_ENABLED:  return "enabled";
                    case TrackingTarget_STATUS_AI_Recognition_DISABLED: return "disabled";
                    default:                                            return "disabled";
                }
            }

            /// registry intrinsics/mount -> projector camera entry.
            /// hfov comes from the registry (stored or derived from fx);
            /// vfov is derived from fy/height; k1/k2 are the first two
            /// OpenCV distortion coefficients (radial - tangential and k3
            /// are outside the projector's lens model).
            static GeoCameraEntry entryFromRegistry (const de::camera::CameraRegistryEntry& e)
            {
                GeoCameraEntry out;
                out.camera.hfov_deg  = e.hfovDeg();
                out.camera.vfov_deg  = e.vfovDeg();
                if (e.distortion.size() > 0) out.camera.k1 = e.distortion[0];
                if (e.distortion.size() > 1) out.camera.k2 = e.distortion[1];
                out.mount.pitch_deg = e.mount_pitch_deg;
                out.mount.yaw_deg   = e.mount_yaw_deg;
                out.mount.roll_deg  = e.mount_roll_deg;
                return out;
            }

            void logError (const std::string& line) const
            {
                if (fn_log_error) fn_log_error(line);
            }

            static void readEntry (const nlohmann::json& c, GeoCameraEntry& e, const GeoCameraEntry& base)
            {
                e.camera.hfov_deg = c.value("hfov_deg",        base.camera.hfov_deg);
                e.camera.vfov_deg = c.value("vfov_deg",        base.camera.vfov_deg);
                e.camera.k1       = c.value("k1",              base.camera.k1);
                e.camera.k2       = c.value("k2",              base.camera.k2);
                e.mount.pitch_deg = c.value("mount_pitch_deg", base.mount.pitch_deg);
                e.mount.yaw_deg   = c.value("mount_yaw_deg",   base.mount.yaw_deg);
                e.mount.roll_deg  = c.value("mount_roll_deg",  base.mount.roll_deg);
            }

            uint64_t nowUs () const
            {
                if (fn_now_us) return fn_now_us();
                return (uint64_t) std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            }

            void publishState (const nlohmann::json& changed) const
            {
                if (m_caps_advertised && fn_publish_state) fn_publish_state(changed);
            }

            struct ThrottleEntry
            {
                uint64_t last_sent_us = 0;
                double   last_lat = 0.0;
                double   last_lng = 0.0;
                double   last_unc_m = 0.0;
            };

            /// rate_hz cap + moved-or-5s gate, per track (cam_id + trk) or per class when untracked
            bool passesThrottle (const std::string& cam_id, const de::ai::Observation& o,
                                 const GeoProjectionResult& r, const uint64_t now)
            {
                const std::string key = o.has_trk
                    ? cam_id + "|t" + std::to_string(o.trk)
                    : cam_id + "|c" + (o.cls.empty() ? std::string("*") : o.cls);

                if (m_throttle.size() > 256)
                {
                    for (auto it = m_throttle.begin(); it != m_throttle.end();)
                    {
                        if (now - it->second.last_sent_us > 60000000) it = m_throttle.erase(it);
                        else ++it;
                    }
                }

                ThrottleEntry& te = m_throttle[key];

                const uint64_t period_us = (m_config.rate_hz > 0.0)
                    ? (uint64_t) (1000000.0 / m_config.rate_hz) : 0;
                // re-emit when the fix moved by more than the PREVIOUS uncertainty
                const double move_gate_m = (te.last_unc_m > 0.0) ? te.last_unc_m : r.unc_m;
                const bool moved = geoDistanceM(te.last_lat, te.last_lng, r.lat, r.lng) > move_gate_m;
                const bool steady_timeout = (now - te.last_sent_us) >= STEADY_RESEND_US;

                if ((te.last_sent_us != 0) &&
                    ((now - te.last_sent_us < period_us) || (!moved && !steady_timeout)))
                {
                    return false;
                }

                te.last_sent_us = now;
                te.last_lat = r.lat;
                te.last_lng = r.lng;
                te.last_unc_m = r.unc_m;
                return true;
            }

        private:
            GeoProjectorConfig m_config;
            std::map<std::string, ThrottleEntry> m_throttle;
            std::set<std::string> m_dropped_cameras;   ///< registry-active drops, logged once each
            int      m_ai_status = -1;
            uint64_t m_last_fix_us = 0;
            uint64_t m_last_state_pub_us = 0;
            bool     m_caps_advertised = false;
            bool     m_registry_active = false;
    };

}
}

#endif
