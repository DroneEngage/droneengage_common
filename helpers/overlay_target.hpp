#ifndef DE_OVERLAY_TARGET_H_
#define DE_OVERLAY_TARGET_H_

/**
 * @brief Per-camera OSD routing (CA-0022): resolve the `t` target string a
 * sender module stamps on TYPE_AndruavMessage_CAMERA_OVERLAY_ACTION so
 * de_camera draws the overlay only on the camera that module is linked to.
 *
 * A video-processing module (de_tracker, de_precland, de_ir_camera, de_ai,
 * ...) is linked to one camera through the virtual video device pipeline:
 * de_camera streams the device the module writes to (or reads from), so the
 * module's own "camera" config section identifies which stream its
 * drawings belong on.
 *
 * Precedence - first non-empty string wins:
 *   camera.overlay_target            explicit override, always honoured
 *   camera.output_video_device_name  V4L2 card name of the output device
 *   camera.output_video_device       /dev/videoN of the output device
 *   camera.source_video_device_name  V4L2 card name of the input device
 *   camera.source_video_device       /dev/videoN of the input device
 *   ""                               legacy: draw on ALL cameras
 *
 * de_camera matches the returned string against each stream's unique_name,
 * configured local_name, V4L2 card name (device_name) and /dev/videoN path.
 * Modules that predate targeting send no `t` and keep the all-cameras
 * behaviour, so callers may stamp the result verbatim (empty included).
 *
 * Pure header: no bus, no video - the caller passes its "camera" config
 * section (m_jsonConfig["camera"]).
 */

#include <string>

#include "json_nlohmann.hpp"

namespace de
{
namespace camera
{
    /**
     * @brief first non-empty string of overlay_target,
     *  output_video_device_name, output_video_device,
     *  source_video_device_name, source_video_device in the module's
     *  "camera" config section; "" when none is set
     *  (= legacy draw-on-all-cameras).
     */
    inline std::string resolveOverlayTarget (const nlohmann::json& camera_config)
    {
        if (!camera_config.is_object()) return "";

        static const char* keys[] =
        {
            "overlay_target",
            "output_video_device_name",
            "output_video_device",
            "source_video_device_name",
            "source_video_device"
        };

        for (const char* key : keys)
        {
            const auto it = camera_config.find(key);
            if ((it != camera_config.end()) && it->is_string())
            {
                const std::string value = it->get<std::string>();
                if (!value.empty()) return value;
            }
        }

        return "";
    }

} // namespace camera
} // namespace de

#endif
