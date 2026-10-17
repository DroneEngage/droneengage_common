/**
 * @file test_geo_projector_core.cpp
 * @brief tests for helpers/geo_projector_core.hpp (obs -> detect.detected).
 *
 * Build & run:  make -C test && ./test/test_geo_projector_core
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../helpers/geo_projector_core.hpp"
#include "../de_databus/cap_schema.hpp"

using namespace de::geo;
using Json = nlohmann::json;

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "ASSERTION FAILED: " << #condition << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::fabs((a) - (b)) <= (eps))

static const double LAT = 30.0444;
static const double LNG = 31.2357;

/// fixed pose, records what the core asked for
struct FixedPose : IPoseSource
{
    GeoPoseFix fix;
    bool available = true;
    uint64_t last_t = 0;
    int calls = 0;

    FixedPose ()
    {
        fix.att  = {0.0, 0.0, 0.0};
        fix.pose = {LAT, LNG, 60.0};
    }

    bool poseAt (const uint64_t t, GeoPoseFix& out) override
    {
        ++calls; last_t = t; out = fix;
        return available;
    }
};

struct Rig
{
    CGeoProjectorCore core;
    FixedPose pose;
    uint64_t now = 100000000;     // 100 s
    std::vector<Json> events;
    std::vector<Json> states;
    std::vector<std::string> errors;

    explicit Rig (const std::string& cfg = R"({"enabled":true})")
    {
        core.fn_now_us = [this]() { return now; };
        core.fn_fire_event = [this](const Json& p) { events.push_back(p); };
        core.fn_publish_state = [this](const Json& s) { states.push_back(s); };
        core.fn_log_error = [this](const std::string& e) { errors.push_back(e); };
        core.init(Json::parse(cfg));
    }

    /// obs frame; boxes are [x,y,w,h]
    Json frame (const std::string& cam, const std::vector<Json>& obs, const uint64_t tm = 0) const
    {
        return {{"cam_id", cam}, {"tm_capture", tm ? tm : now}, {"obs", obs}};
    }

    static Json ob (const std::string& cls, const double x, const double y, const double conf = 0.9, const int trk = -1)
    {
        Json o = {{"cls", cls}, {"conf", conf}, {"box", {x, y, 0.1, 0.1}}};
        if (trk >= 0) o["trk"] = trk;
        return o;
    }
};


bool test_two_tracks_two_events ()
{
    std::cout << "Testing: two persons (trk 1, trk 2) in one frame -> two events..." << std::endl;

    Rig r;
    const int n = r.core.onObservations(r.frame("DE-CAM1", { Rig::ob("person", 0.20, 0.20, 0.9, 1), Rig::ob("person", 0.70, 0.60, 0.8, 2) }), r.pose);
    ASSERT_TRUE(n == 2);
    ASSERT_TRUE(r.events.size() == 2);

    ASSERT_TRUE(r.events[0]["class"] == "person");
    ASSERT_TRUE(r.events[0]["trk"] == 1 && r.events[1]["trk"] == 2);
    ASSERT_TRUE(r.events[0]["cam_id"] == "DE-CAM1");
    ASSERT_TRUE(r.events[0]["src"] == "ai");
    ASSERT_TRUE(r.events[0]["unc_m"].get<double>() > 0.0);
    // different image positions -> different ground positions
    ASSERT_TRUE(std::fabs(r.events[0]["pos"][0].get<double>() - r.events[1]["pos"][0].get<double>()) > 1e-6 ||
                std::fabs(r.events[0]["pos"][1].get<double>() - r.events[1]["pos"][1].get<double>()) > 1e-6);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_throttle_per_track ()
{
    std::cout << "Testing: same trk not moved within 5 s is throttled; per track, not per class..." << std::endl;

    Rig r;
    auto f = [&](const int trk) { return r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, trk) }); };

    ASSERT_TRUE(r.core.onObservations(f(7), r.pose) == 1);

    r.now += 1500000;                                   // past rate_hz = 1, not moved, < 5 s
    ASSERT_TRUE(r.core.onObservations(f(7), r.pose) == 0);

    // a different track of the same class is NOT suppressed by trk 7
    ASSERT_TRUE(r.core.onObservations(f(8), r.pose) == 1);

    r.now += 4000000;                                   // 5.5 s since the first one
    ASSERT_TRUE(r.core.onObservations(f(7), r.pose) == 1);

    // moved by more than the uncertainty re-emits right after the rate period
    r.now += 1100000;
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("person", 0.9, 0.9, 0.9, 7) }), r.pose) == 1);

    // same trk id on another camera is another key
    r.now += 100000;
    ASSERT_TRUE(r.core.onObservations(r.frame("OTHER", { Rig::ob("person", 0.9, 0.9, 0.9, 7) }), r.pose) == 1);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_untracked_falls_back_to_class ()
{
    std::cout << "Testing: observations without trk are throttled per class..." << std::endl;

    Rig r;
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4), Rig::ob("person", 0.4, 0.4) }), r.pose) == 1);
    ASSERT_TRUE(r.events[0].contains("trk") == false);

    // a different class is independent
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("car", 0.4, 0.4) }), r.pose) == 1);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_gates ()
{
    std::cout << "Testing: min_conf, class only from obs.cls, disabled, bad frames, no pose..." << std::endl;

    Rig r;
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.5, 1) }), r.pose) == 0);   // < 0.6
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.6, 1) }), r.pose) == 1);   // == min_conf passes

    // the class is exactly the observation's, whatever it is
    r.now += 6000000;
    ASSERT_TRUE(r.core.onObservations(r.frame("C", { Rig::ob("hotspot", 0.4, 0.4, 0.9, 5) }), r.pose) == 1);
    ASSERT_TRUE(r.events.back()["class"] == "hotspot");

    // empty and invalid frames are harmless
    ASSERT_TRUE(r.core.onObservations(r.frame("C", {}), r.pose) == 0);
    ASSERT_TRUE(r.core.onObservations(Json::parse(R"({"b":{"x":0.1,"y":0.1,"w":0.2,"h":0.2}})"), r.pose) == 0);   // the removed 1078 shape
    ASSERT_TRUE(r.core.onObservations(Json::parse(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"x","box":[0.1,0.1,0.2,0.2]}]})"), r.pose) == 0);   // no conf

    // above the horizon: no ground point
    Rig h; h.pose.fix.att = {0.0, 1.2, 0.0};
    h.core.init(Json::parse(R"({"enabled":true,"mount_pitch_deg":0})"));
    ASSERT_TRUE(h.core.onObservations(h.frame("C", { Rig::ob("person", 0.4, 0.1, 0.9, 1) }), h.pose) == 0);

    // no pose available
    Rig np; np.pose.available = false;
    ASSERT_TRUE(np.core.onObservations(np.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }), np.pose) == 0);

    // disabled
    Rig off(R"({"enabled":false})");
    ASSERT_TRUE(off.core.onObservations(off.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }), off.pose) == 0);
    ASSERT_TRUE(off.pose.calls == 0);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_camera_selected_by_cam_id ()
{
    std::cout << "Testing: the same box projects differently for two cam_ids with different mounts..." << std::endl;

    const char* cfg = R"({"enabled":true,"mount_pitch_deg":-90,
        "cameras":{"DOWN":{"mount_pitch_deg":-90},"FWD":{"mount_pitch_deg":-45}}})";

    Rig a(cfg), b(cfg), c(cfg);
    // centre-low box: a down camera sees the point below the unit, a forward-pitched one sees it far ahead
    a.core.onObservations(a.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), a.pose);
    b.core.onObservations(b.frame("FWD",  { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), b.pose);
    c.core.onObservations(c.frame("UNKNOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), c.pose);

    ASSERT_TRUE(a.events.size() == 1 && b.events.size() == 1 && c.events.size() == 1);
    ASSERT_TRUE(b.events[0]["range_m"].get<double>() > a.events[0]["range_m"].get<double>() + 10.0);
    ASSERT_NEAR(c.events[0]["range_m"].get<double>(), a.events[0]["range_m"].get<double>(), 1e-9);   // unknown cam = defaults (-90)

    std::cout << "passed" << std::endl;
    return true;
}


/// CA-0012: two registry cameras with different mounts + an unknown cam_id
static const char* REGISTRY_PATH = "/tmp/de_test_geo_registry.json";
static void writeTestRegistry ()
{
    std::ofstream out(REGISTRY_PATH, std::ios::trunc);
    out << R"({"cameras":{
        "DOWN":{"image_width":640,"image_height":480,
                "camera_matrix":[700,0,320, 0,700,240, 0,0,1],
                "distortion":[0,0],"mount_pitch_deg":-90,"hfov_deg":62.2},
        "FWD":{"image_width":640,"image_height":480,
               "camera_matrix":[700,0,320, 0,700,240, 0,0,1],
               "distortion":[0,0],"mount_pitch_deg":-45,"hfov_deg":62.2}
    }})";
    out.close();
}


bool test_registry_cameras_and_drop ()
{
    std::cout << "Testing: registry drives per-cam mounts; unknown cam_id is dropped with an error..." << std::endl;

    writeTestRegistry();
    const std::string cfg = std::string(R"({"enabled":true,"camera_registry":")") + REGISTRY_PATH + "\"}";

    Rig a(cfg), b(cfg), c(cfg);
    ASSERT_TRUE(a.core.registryActive());

    // same box, two cam_ids, different mounts from the registry file
    a.core.onObservations(a.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), a.pose);
    b.core.onObservations(b.frame("FWD",  { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), b.pose);
    ASSERT_TRUE(a.events.size() == 1 && b.events.size() == 1);
    ASSERT_TRUE(b.events[0]["range_m"].get<double>() > a.events[0]["range_m"].get<double>() + 10.0);

    // no registry entry -> the obs is dropped and an error is logged (once)
    ASSERT_TRUE(c.core.onObservations(c.frame("GHOST", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), c.pose) == 0);
    ASSERT_TRUE(c.events.empty());
    ASSERT_TRUE(c.errors.size() == 1);
    ASSERT_TRUE(c.core.onObservations(c.frame("GHOST", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), c.pose) == 0);
    ASSERT_TRUE(c.errors.size() == 1);                                  // still once

    // without a registry the old behaviour stands: unknown cam_id uses defaults
    Rig d(R"({"enabled":true,"mount_pitch_deg":-90})");
    ASSERT_FALSE(d.core.registryActive());
    ASSERT_TRUE(d.core.onObservations(d.frame("GHOST", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), d.pose) == 1);
    ASSERT_TRUE(d.errors.empty());

    // a missing registry file fails closed: strict, error logged, every obs dropped
    Rig e(R"({"enabled":true,"camera_registry":"/tmp/de_test_geo_registry_missing.json"})");
    ASSERT_TRUE(e.core.registryActive());
    ASSERT_TRUE(e.errors.size() == 1);
    ASSERT_TRUE(e.core.onObservations(e.frame("ANY", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), e.pose) == 0);
    ASSERT_TRUE(e.events.empty());

    // a corrupt registry file fails closed the same way
    const char* bad_path = "/tmp/de_test_geo_registry_corrupt.json";
    { std::ofstream out(bad_path, std::ios::trunc); out << "not json {{{"; }
    Rig f(std::string(R"({"enabled":true,"camera_registry":")") + bad_path + "\"}");
    ASSERT_TRUE(f.core.registryActive());
    ASSERT_TRUE(f.errors.size() == 1);
    ASSERT_TRUE(f.core.onObservations(f.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), f.pose) == 0);
    ASSERT_TRUE(f.events.empty());

    // the registry is authoritative: a config-map cam_id absent from the
    // registry is dropped too (the "cameras" map is not a fallback)
    const std::string cfg2 = std::string(R"({"enabled":true,"camera_registry":")") + REGISTRY_PATH +
        R"(","cameras":{"LEGACY":{"mount_pitch_deg":-45}}})";
    Rig g(cfg2);
    ASSERT_TRUE(g.core.registryActive());
    ASSERT_TRUE(g.core.onObservations(g.frame("LEGACY", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), g.pose) == 0);
    ASSERT_TRUE(g.events.empty());
    ASSERT_TRUE(g.core.onObservations(g.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), g.pose) == 1);

    // CA-0037: a non-string camera_registry value is rejected loudly, fail closed
    Rig h(R"({"enabled":true,"camera_registry":{"path":"x"}})");
    ASSERT_TRUE(h.core.registryActive());
    ASSERT_TRUE(h.errors.size() == 1);
    ASSERT_TRUE(h.core.onObservations(h.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), h.pose) == 0);

    // CA-0037: every entry skipped by validIntrinsics -> clear error, fail closed
    const char* inv_path = "/tmp/de_test_geo_registry_invalid.json";
    { std::ofstream out(inv_path, std::ios::trunc);
      out << R"({"cameras":{"DOWN":{"image_width":0,"image_height":0,"camera_matrix":[0,0,0,0,0,0,0,0,1]}}})"; }
    Rig k(std::string(R"({"enabled":true,"camera_registry":")") + inv_path + "\"}");
    ASSERT_TRUE(k.core.registryActive());
    ASSERT_TRUE(k.errors.size() == 1);
    ASSERT_TRUE(k.errors[0].find("no usable cameras") != std::string::npos);
    ASSERT_TRUE(k.core.onObservations(k.frame("DOWN", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), k.pose) == 0);

    // CA-0037: reconfigure clears the drop log so a repeat drop is logged again
    Rig m(cfg);
    m.core.onObservations(m.frame("GHOST", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), m.pose);
    ASSERT_TRUE(m.errors.size() == 1);
    m.core.init(Json::parse(cfg));
    m.core.onObservations(m.frame("GHOST", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), m.pose);
    ASSERT_TRUE(m.errors.size() == 2);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_live_mount_override ()
{
    std::cout << "Testing: a live gimbal pitch/yaw replaces the camera entry's mount..." << std::endl;

    Rig a, b;
    a.core.onObservations(a.frame("C", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), a.pose);

    b.pose.fix.has_mount_pitch = true;
    b.pose.fix.mount_pitch_deg = -45.0;
    b.core.onObservations(b.frame("C", { Rig::ob("person", 0.45, 0.45, 0.9, 1) }), b.pose);

    ASSERT_TRUE(a.events.size() == 1 && b.events.size() == 1);
    ASSERT_TRUE(b.events[0]["range_m"].get<double>() > a.events[0]["range_m"].get<double>() + 10.0);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_frame_time_alignment ()
{
    std::cout << "Testing: tm_capture is used when sane, now - latency otherwise..." << std::endl;

    Rig r;
    r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }, r.now - 300000), r.pose);
    ASSERT_TRUE(r.pose.last_t == r.now - 300000);

    r.now += 6000000;
    r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }, 5), r.pose);            // 100 s old
    ASSERT_TRUE(r.pose.last_t == r.now - 150000);

    r.now += 6000000;
    r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }, r.now + 5000000), r.pose);   // from the future
    ASSERT_TRUE(r.pose.last_t == r.now - 150000);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_advert_invoke_state ()
{
    std::cout << "Testing: advert validates; enable/disable; state only when advertised..." << std::endl;

    Rig r(R"({"enabled":false})");

    Json advert = r.core.buildAdvert("droneengage_test", "1.0.0");
    std::string err;
    ASSERT_TRUE(de::comm::capValidateAdvert(advert, err));
    ASSERT_TRUE(advert["ns"] == "detect" && advert["module"] == "droneengage_test");
    ASSERT_TRUE(advert["events"]["detected"]["payload"].contains("trk"));

    // not advertised: nothing is published
    Json res = r.core.invoke("detect", "enable", err);
    ASSERT_TRUE(err.empty() && res["enabled"] == true && r.core.isEnabled());
    ASSERT_TRUE(r.states.empty());

    r.core.onAdvertised(true);
    r.core.invoke("detect", "disable", err);
    ASSERT_TRUE(r.states.size() == 1 && r.states[0]["enabled"] == false);
    r.core.invoke("detect", "disable", err);                 // no change, no new state
    ASSERT_TRUE(r.states.size() == 1);

    r.core.invoke("detect", "bogus", err);
    ASSERT_TRUE(!err.empty());
    err.clear();
    r.core.invoke("other", "enable", err);
    ASSERT_TRUE(!err.empty());

    r.core.onAIStatus(Json::parse(R"({"a":1})"));
    ASSERT_TRUE(r.states.back()["ai"] == "detected");
    const size_t before = r.states.size();
    r.core.onAIStatus(Json::parse(R"({"a":1})"));             // same status
    r.core.onAIStatus(Json::parse(R"({"a":4,"c":["person"]})"));   // class list is ignored
    ASSERT_TRUE(r.states.size() == before);

    // last_fix_age_s ticks once a fix exists
    r.core.invoke("detect", "enable", err);
    r.core.onObservations(r.frame("C", { Rig::ob("person", 0.4, 0.4, 0.9, 1) }), r.pose);
    r.now += 2000000;
    r.core.tick();
    ASSERT_TRUE(r.states.back().contains("last_fix_age_s"));
    ASSERT_NEAR(r.states.back()["last_fix_age_s"].get<double>(), 2.0, 1e-6);

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_two_tracks_two_events();
    ok &= test_throttle_per_track();
    ok &= test_untracked_falls_back_to_class();
    ok &= test_gates();
    ok &= test_camera_selected_by_cam_id();
    ok &= test_registry_cameras_and_drop();
    ok &= test_live_mount_override();
    ok &= test_frame_time_alignment();
    ok &= test_advert_invoke_state();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
