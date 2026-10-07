#ifndef DE_AI_BOX_H_
#define DE_AI_BOX_H_

/**
 * @brief Shared reader for the best-object box "b" of
 * TYPE_AndruavMessage_AI_Recognition_TargetLocation (1078) - TASK-B4-01.
 *
 * The box carries normalized top-left geometry {x,y,w,h} plus, since
 * B4-01, the detector's own class metadata and confidence:
 *
 *     {"b":{"x":..,"y":..,"w":..,"h":..,"conf":..,
 *           "class_id":0,"class_name":"person"}}
 *
 * Legacy producers (pre-B4-01 stubs, old de_ai builds) send geometry only;
 * droneengage_yolo_ai_generic already emits "confidence" instead of
 * "conf" - both names are read. The resolver owns the legacy defaults so
 * the two geo projectors (de_mavlink, de_comm SU-04) stay identical:
 * confidence defaults to 1.0 when absent, and the resolved class falls
 * back to the selected (1076 SEARCH) class name when the box carries no
 * usable class.
 *
 * Class-name precedence: a non-empty "class_name" wins; otherwise an
 * in-range "class_id" resolves through the receiver's class list (1077
 * CLASS_LIST); otherwise the fallback is used. An empty-string or
 * non-string "class_name" counts as absent; when name and id disagree the
 * name wins (the name is what downstream class matching compares).
 *
 * Pure header (nlohmann json only) so every consumer sees the same wire
 * semantics; standalone test in de_common/test/test_ai_box.cpp.
 */

#include <string>
#include <vector>

#include "json_nlohmann.hpp"

using Json_de = nlohmann::json;

namespace de
{
namespace ai
{

    /**
     * @brief Parsed fields of a 1078 best-object box. Geometry is only
     * meaningful when aiBoxParse returned true; metadata keeps its
     * defaults otherwise.
     */
    struct AiBox
    {
        double x = 0.0;                  // normalized top-left corner
        double y = 0.0;
        double w = 0.0;
        double h = 0.0;
        double confidence = 1.0;         // "conf"/"confidence"; 1.0 legacy default
        bool   has_class_id = false;     // a numeric "class_id" was present
        int    class_id = -1;
        std::string class_name;          // empty when absent/empty/non-string
    };


    /**
     * @brief Read a 1078 "b" object. Returns false when the box is not a
     * usable detection (not an object, or missing/non-number geometry) -
     * the same guard the projectors applied before B4-01.
     */
    inline bool aiBoxParse (const Json_de& box, AiBox& out)
    {
        if (!box.is_object()) return false;

        if (!box.contains("x") || !box["x"].is_number()) return false;
        if (!box.contains("y") || !box["y"].is_number()) return false;
        if (!box.contains("w") || !box["w"].is_number()) return false;
        if (!box.contains("h") || !box["h"].is_number()) return false;

        out.x = box["x"].get<double>();
        out.y = box["y"].get<double>();
        out.w = box["w"].get<double>();
        out.h = box["h"].get<double>();

        out.confidence = 1.0;
        if (box.contains("conf") && box["conf"].is_number())
        {
            out.confidence = box["conf"].get<double>();
        }
        else if (box.contains("confidence") && box["confidence"].is_number())
        {
            out.confidence = box["confidence"].get<double>();
        }

        out.has_class_id = false;
        out.class_id = -1;
        if (box.contains("class_id") && box["class_id"].is_number())
        {
            out.has_class_id = true;
            out.class_id = box["class_id"].get<int>();
        }

        out.class_name.clear();
        if (box.contains("class_name") && box["class_name"].is_string())
        {
            out.class_name = box["class_name"].get<std::string>();
        }

        return true;
    }


    /**
     * @brief The detected class name for a parsed box: the box's own
     * "class_name" when present, else "class_id" mapped through
     * class_names (the 1077 CLASS_LIST the projector already tracks),
     * else fallback (the legacy 1076-selected class name, "" when none).
     */
    inline std::string aiBoxClassName (const AiBox& box,
                                       const std::vector<std::string>& class_names,
                                       const std::string& fallback)
    {
        if (!box.class_name.empty()) return box.class_name;

        if (box.has_class_id && (box.class_id >= 0) &&
            (box.class_id < (int) class_names.size()))
        {
            return class_names[box.class_id];
        }

        return fallback;
    }

}
}

#endif
