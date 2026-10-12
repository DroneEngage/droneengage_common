#ifndef DE_OBS_H_
#define DE_OBS_H_

/**
 * @brief Builder and strict parser for TYPE_AndruavMessage_AI_OBSERVATIONS
 * (6546) - the per-frame observation list every perceiver (de_perceive
 * backends, de_ir_camera) publishes.
 *
 *     {"cam_id":"DE-CAM1","tm_capture":123456789,
 *      "obs":[{"trk":7,"cls":"person","cls_id":0,"conf":0.91,
 *              "box":[0.40,0.25,0.10,0.30],
 *              "attrs":{"temp_c":36.5}}]}
 *
 * Box convention: [x,y,w,h] normalized to the unrotated sensor frame,
 * TOP-LEFT origin, every value in [0..1] with x+w<=1 and y+h<=1. This is
 * NOT the centred [-0.5..0.5] convention.
 *
 * Strict by design: "cls" (non-empty string) and "conf" ([0..1]) are
 * required on every entry, "cam_id" and "tm_capture" on the frame. There
 * are no aliases and no defaults. Any invalid entry rejects the whole
 * frame so a malformed producer is noticed instead of partially trusted.
 * An empty "obs" array is valid (frame processed, nothing seen).
 *
 * Pure header (nlohmann json only); standalone test in
 * de_common/test/test_obs.cpp.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "json_nlohmann.hpp"

using Json_de = nlohmann::json;

namespace de
{
namespace ai
{

    struct Observation
    {
        bool        has_trk = false;     // stable track id assigned
        int64_t     trk = -1;
        std::string cls;                 // REQUIRED, non-empty
        bool        has_cls_id = false;
        int         cls_id = -1;
        double      conf = 0.0;          // REQUIRED, [0..1]
        double      x = 0.0;             // normalized top-left corner
        double      y = 0.0;
        double      w = 0.0;
        double      h = 0.0;
        Json_de     attrs;               // null when absent, else an object
    };

    struct ObservationFrame
    {
        std::string cam_id;              // REQUIRED, non-empty
        int64_t     tm_capture = 0;      // REQUIRED, monotonic us of frame capture
        std::vector<Observation> obs;
    };


    inline bool obsBoxValid (const Observation& o)
    {
        const double eps = 1e-6;
        if (o.x < 0.0 || o.x > 1.0) return false;
        if (o.y < 0.0 || o.y > 1.0) return false;
        if (o.w <= 0.0 || o.w > 1.0) return false;
        if (o.h <= 0.0 || o.h > 1.0) return false;
        if (o.x + o.w > 1.0 + eps) return false;
        if (o.y + o.h > 1.0 + eps) return false;
        return true;
    }


    /**
     * @brief Build the wire object for a frame. Returns false (and sets
     * err) when the frame violates the contract, so producers cannot emit
     * what consumers would reject.
     */
    inline bool obsBuild (const ObservationFrame& frame, Json_de& out, std::string& err)
    {
        if (frame.cam_id.empty()) { err = "cam_id empty"; return false; }
        if (frame.tm_capture < 0) { err = "tm_capture negative"; return false; }

        Json_de arr = Json_de::array();
        for (size_t i = 0; i < frame.obs.size(); ++i)
        {
            const Observation& o = frame.obs[i];
            if (o.cls.empty()) { err = "obs[" + std::to_string(i) + "] cls empty"; return false; }
            if (!(o.conf >= 0.0 && o.conf <= 1.0)) { err = "obs[" + std::to_string(i) + "] conf out of [0..1]"; return false; }
            if (!obsBoxValid(o)) { err = "obs[" + std::to_string(i) + "] box outside [0..1]"; return false; }
            if (!o.attrs.is_null() && !o.attrs.is_object()) { err = "obs[" + std::to_string(i) + "] attrs not an object"; return false; }

            Json_de e = Json_de::object();
            if (o.has_trk) e["trk"] = o.trk;
            e["cls"] = o.cls;
            if (o.has_cls_id) e["cls_id"] = o.cls_id;
            e["conf"] = o.conf;
            e["box"] = Json_de::array({o.x, o.y, o.w, o.h});
            if (o.attrs.is_object()) e["attrs"] = o.attrs;
            arr.push_back(e);
        }

        out = Json_de::object();
        out["cam_id"] = frame.cam_id;
        out["tm_capture"] = frame.tm_capture;
        out["obs"] = arr;
        return true;
    }


    /**
     * @brief Strictly parse a frame. Returns false (and sets err) on any
     * contract violation; out is only meaningful when true is returned.
     */
    inline bool obsParse (const Json_de& ms, ObservationFrame& out, std::string& err)
    {
        out = ObservationFrame();

        if (!ms.is_object()) { err = "not an object"; return false; }

        if (!ms.contains("cam_id") || !ms["cam_id"].is_string()) { err = "cam_id missing"; return false; }
        out.cam_id = ms["cam_id"].get<std::string>();
        if (out.cam_id.empty()) { err = "cam_id empty"; return false; }

        if (!ms.contains("tm_capture") || !ms["tm_capture"].is_number_integer()) { err = "tm_capture missing"; return false; }
        out.tm_capture = ms["tm_capture"].get<int64_t>();
        if (out.tm_capture < 0) { err = "tm_capture negative"; return false; }

        if (!ms.contains("obs") || !ms["obs"].is_array()) { err = "obs missing"; return false; }

        const Json_de& arr = ms["obs"];
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const Json_de& e = arr[i];
            const std::string p = "obs[" + std::to_string(i) + "] ";
            if (!e.is_object()) { err = p + "not an object"; return false; }

            Observation o;

            if (!e.contains("cls") || !e["cls"].is_string()) { err = p + "cls missing"; return false; }
            o.cls = e["cls"].get<std::string>();
            if (o.cls.empty()) { err = p + "cls empty"; return false; }

            if (!e.contains("conf") || !e["conf"].is_number()) { err = p + "conf missing"; return false; }
            o.conf = e["conf"].get<double>();
            if (!(o.conf >= 0.0 && o.conf <= 1.0)) { err = p + "conf out of [0..1]"; return false; }

            if (!e.contains("box") || !e["box"].is_array() || e["box"].size() != 4) { err = p + "box missing"; return false; }
            for (int k = 0; k < 4; ++k)
            {
                if (!e["box"][k].is_number()) { err = p + "box not numeric"; return false; }
            }
            o.x = e["box"][0].get<double>();
            o.y = e["box"][1].get<double>();
            o.w = e["box"][2].get<double>();
            o.h = e["box"][3].get<double>();
            if (!obsBoxValid(o)) { err = p + "box outside [0..1]"; return false; }

            if (e.contains("trk"))
            {
                if (!e["trk"].is_number_integer()) { err = p + "trk not an integer"; return false; }
                o.has_trk = true;
                o.trk = e["trk"].get<int64_t>();
            }

            if (e.contains("cls_id"))
            {
                if (!e["cls_id"].is_number_integer()) { err = p + "cls_id not an integer"; return false; }
                o.has_cls_id = true;
                o.cls_id = e["cls_id"].get<int>();
            }

            if (e.contains("attrs"))
            {
                if (!e["attrs"].is_object()) { err = p + "attrs not an object"; return false; }
                o.attrs = e["attrs"];
            }

            out.obs.push_back(o);
        }

        return true;
    }

}
}

#endif
