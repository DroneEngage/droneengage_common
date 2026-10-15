#ifndef DE_CAMERA_REGISTRY_H_
#define DE_CAMERA_REGISTRY_H_

/**
 * @brief Per-unit camera registry (CA-0012): ONE file per vehicle maps
 * a cam_id to everything a consumer needs about that physical camera -
 * intrinsics, distortion, mount attitude, HFOV and the calibration
 * resolution.
 *
 * Readers:
 *   de_precland      intrinsics + resolution for the AprilTag estimator
 *                    (replaces the per-drone camera.yaml)
 *   de_geo projector (geo_projector_core.hpp, de_mavlink + de_comm)
 *                    mount + lens model per obs cam_id; an obs whose
 *                    cam_id is not in the registry is dropped
 * Writers:
 *   de_precland_calibrate / de_precland 'calibrate' mode update the
 *                    cam_id entry in place (other cameras survive)
 *
 * Producers (de_perceive, de_ir_camera) stamp obs.cam_id; the stamped
 * id must match a registry key for the frame to be projectable.
 *
 * File schema (plain JSON, machine-written):
 * {
 *   "cameras": {
 *     "<cam_id>": {
 *       "image_width":  640,
 *       "image_height": 480,
 *       "camera_matrix": [fx,0,cx, 0,fy,cy, 0,0,1],   // row-major 3x3
 *       "distortion":    [k1,k2,p1,p2,k3],            // OpenCV order
 *       "mount_yaw_deg":   0.0,
 *       "mount_pitch_deg": -90.0,
 *       "mount_roll_deg":  0.0,
 *       "hfov_deg":       62.2                        // optional, derived
 *                                                     // from fx when absent
 *     }
 *   }
 * }
 *
 * Pure header: no OpenCV, no bus. Readers convert intrinsics to whatever
 * matrix type they use; writers go through cameraRegistryUpdateEntry so
 * the on-disk file is always complete and atomically replaced.
 */

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "json_nlohmann.hpp"

namespace de
{
namespace camera
{

    /// everything known about one physical camera
    struct CameraRegistryEntry
    {
        int    image_width  = 0;
        int    image_height = 0;
        /// row-major 3x3 pinhole intrinsics [fx,0,cx, 0,fy,cy, 0,0,1]
        std::array<double, 9> camera_matrix = {0,0,0, 0,0,0, 0,0,0};
        /// OpenCV order [k1,k2,p1,p2,k3]; may be shorter
        std::vector<double> distortion;
        double mount_yaw_deg   = 0.0;
        double mount_pitch_deg = -90.0;
        double mount_roll_deg  = 0.0;
        /// stored HFOV; 0 = derive from fx (below)
        double hfov_deg = 0.0;

        double fx () const { return camera_matrix[0]; }
        double fy () const { return camera_matrix[4]; }
        double cx () const { return camera_matrix[2]; }
        double cy () const { return camera_matrix[5]; }

        bool validIntrinsics () const
        {
            return (image_width > 0) && (image_height > 0)
                && (fx() > 0.0) && (fy() > 0.0);
        }

        /// horizontal FOV: the stored value, else derived from fx/width
        double hfovDeg () const
        {
            if (hfov_deg > 0.0) return hfov_deg;
            if (!validIntrinsics()) return 0.0;
            return 2.0 * std::atan(image_width / (2.0 * fx())) * 180.0 / M_PI;
        }

        /// vertical FOV: always derived from fy/height
        double vfovDeg () const
        {
            if (!validIntrinsics()) return 0.0;
            return 2.0 * std::atan(image_height / (2.0 * fy())) * 180.0 / M_PI;
        }

        nlohmann::json toJson () const
        {
            return
            {
                {"image_width",     image_width},
                {"image_height",    image_height},
                {"camera_matrix",   camera_matrix},
                {"distortion",      distortion},
                {"mount_yaw_deg",   mount_yaw_deg},
                {"mount_pitch_deg", mount_pitch_deg},
                {"mount_roll_deg",  mount_roll_deg},
                {"hfov_deg",        hfov_deg}
            };
        }

        /// fill from one registry entry; missing keys keep current values
        bool fromJson (const nlohmann::json& j, std::string& err)
        {
            if (!j.is_object()) { err = "entry is not an object"; return false; }

            if (j.contains("image_width"))  image_width  = j["image_width"].get<int>();
            if (j.contains("image_height")) image_height = j["image_height"].get<int>();

            if (j.contains("camera_matrix"))
            {
                const auto& m = j["camera_matrix"];
                if (!m.is_array() || m.size() != 9)
                {
                    err = "camera_matrix must be a 9-element array (row-major 3x3)";
                    return false;
                }
                for (int i = 0; i < 9; ++i) camera_matrix[i] = m[i].get<double>();
            }

            if (j.contains("distortion"))
            {
                const auto& d = j["distortion"];
                if (!d.is_array()) { err = "distortion must be an array"; return false; }
                distortion.clear();
                for (const auto& v : d) distortion.push_back(v.get<double>());
            }

            if (j.contains("mount_yaw_deg"))   mount_yaw_deg   = j["mount_yaw_deg"].get<double>();
            if (j.contains("mount_pitch_deg")) mount_pitch_deg = j["mount_pitch_deg"].get<double>();
            if (j.contains("mount_roll_deg"))  mount_roll_deg  = j["mount_roll_deg"].get<double>();
            if (j.contains("hfov_deg"))        hfov_deg        = j["hfov_deg"].get<double>();
            return true;
        }
    };


    class CCameraRegistry
    {
        public:

            /// load a registry file; false + err on missing/invalid file.
            /// Individual entries that fail fromJson - or parse but carry
            /// intrinsics no consumer can project with (width/height/fx/fy
            /// <= 0) - are skipped with the first error kept in err
            /// (registry stays usable; a dropped cam_id fails closed).
            bool load (const std::string& path, std::string& err)
            {
                m_cameras.clear();
                m_path.clear();

                std::ifstream in(path);
                if (!in.is_open())
                {
                    err = "cannot open camera registry '" + path + "'";
                    return false;
                }

                nlohmann::json j;
                try
                {
                    in >> j;
                }
                catch (const std::exception& e)
                {
                    err = "camera registry '" + path + "' is not valid JSON: " + e.what();
                    return false;
                }

                if (!j.is_object() || !j.contains("cameras") || !j["cameras"].is_object())
                {
                    err = "camera registry '" + path + "' has no 'cameras' object";
                    return false;
                }

                err.clear();
                for (auto it = j["cameras"].begin(); it != j["cameras"].end(); ++it)
                {
                    CameraRegistryEntry e;
                    std::string entry_err;
                    if (!e.fromJson(it.value(), entry_err))
                    {
                        if (err.empty()) err = "camera '" + it.key() + "': " + entry_err;
                        continue;
                    }
                    if (!e.validIntrinsics())
                    {
                        if (err.empty())
                        {
                            err = "camera '" + it.key() + "': invalid intrinsics "
                                  "(image_width/image_height/fx/fy must be > 0)";
                        }
                        continue;
                    }
                    m_cameras[it.key()] = e;
                }

                m_path = path;
                return true;
            }

            /// entry for a cam_id, nullptr when unknown
            const CameraRegistryEntry* find (const std::string& cam_id) const
            {
                const auto it = m_cameras.find(cam_id);
                return (it != m_cameras.end()) ? &it->second : nullptr;
            }

            size_t size () const { return m_cameras.size(); }
            const std::map<std::string, CameraRegistryEntry>& cameras () const { return m_cameras; }
            const std::string& path () const { return m_path; }

        private:
            std::map<std::string, CameraRegistryEntry> m_cameras;
            std::string m_path;
    };


    /**
     * @brief insert/replace one cam_id entry in a registry file and write
     * it back atomically (tmp + fsync + timestamped .bak + rename - the
     * same discipline writeYamlAtomic uses). Other cameras survive;
     * the file is created when it does not exist.
     */
    inline bool cameraRegistryUpdateEntry (const std::string& path,
                                           const std::string& cam_id,
                                           const CameraRegistryEntry& entry,
                                           std::string& err)
    {
        if (cam_id.empty()) { err = "cam_id empty"; return false; }

        nlohmann::json j = nlohmann::json::object();
        j["cameras"] = nlohmann::json::object();

        {
            std::ifstream in(path);
            if (in.is_open())
            {
                try
                {
                    nlohmann::json old;
                    in >> old;
                    if (old.is_object() && old.contains("cameras") && old["cameras"].is_object())
                    {
                        j = old;
                    }
                }
                catch (const std::exception&)
                {
                    // unparseable existing file: it is backed up below and
                    // replaced rather than silently merged
                }
            }
        }

        j["cameras"][cam_id] = entry.toJson();

        const std::string tmp_path = path + ".new";
        {
            std::ofstream out(tmp_path, std::ios::trunc);
            if (!out.is_open())
            {
                err = "cannot write " + tmp_path;
                return false;
            }
            out << j.dump(2) << '\n';
            out.flush();
            if (out.fail())
            {
                err = "cannot write " + tmp_path;
                return false;
            }
        }

        // fsync the temp file so a crash after rename doesn't leave a partial file
        FILE* fp = fopen(tmp_path.c_str(), "r");
        if (fp) { fflush(fp); const int fd = fileno(fp); if (fd >= 0) fsync(fd); fclose(fp); }

        // back up the existing file before replacing it
        if (access(path.c_str(), F_OK) == 0)
        {
            const int64_t ts = (int64_t) std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const std::string bak_path = path + "." + std::to_string(ts) + ".bak";
            rename(path.c_str(), bak_path.c_str());   // best-effort, like writeYamlAtomic
        }

        if (rename(tmp_path.c_str(), path.c_str()) != 0)
        {
            err = "rename " + tmp_path + " -> " + path + " failed: " + strerror(errno);
            return false;
        }

        return true;
    }

} // namespace camera
} // namespace de

#endif
