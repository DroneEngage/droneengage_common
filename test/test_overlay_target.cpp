/**
 * @file test_overlay_target.cpp
 * @brief tests for helpers/overlay_target.hpp (CA-0023, epic CA-0022): the
 * `t` target a module stamps on CAMERA_OVERLAY_ACTION resolves from its
 * "camera" config section as overlay_target > cam_id (arg or section) >
 * output_video_device_name > output_video_device > source_video_device_name
 * > source_video_device; "" = legacy draw-on-all-cameras.
 *
 * Build & run:  make -C test && ./test/test_overlay_target
 */

#include <iostream>
#include <string>

#include "../helpers/overlay_target.hpp"

using de::camera::resolveOverlayTarget;

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "ASSERTION FAILED: " << #condition << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_EQ_STR(actual, expected) \
    do { \
        const std::string a_ = (actual); \
        if (a_ != (expected)) { \
            std::cerr << "ASSERTION FAILED: got \"" << a_ << "\", expected \"" \
                      << (expected) << "\" at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)


bool test_full_precedence ()
{
    std::cout << "Testing: each precedence step wins over the ones below it..." << std::endl;

    // overlay_target beats everything
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "overlay_target": "DE-CAM-FWD",
        "output_video_device_name": "DE-TRK",
        "output_video_device": "/dev/video3",
        "source_video_device_name": "C270 HD WEBCAM",
        "source_video_device": "/dev/video0"
    })")), "DE-CAM-FWD");

    // no overlay_target: output_video_device_name wins
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "output_video_device_name": "DE-TRK",
        "output_video_device": "/dev/video3",
        "source_video_device_name": "C270 HD WEBCAM",
        "source_video_device": "/dev/video0"
    })")), "DE-TRK");

    // no names: output_video_device wins over the source fields
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "output_video_device": "/dev/video3",
        "source_video_device_name": "C270 HD WEBCAM",
        "source_video_device": "/dev/video0"
    })")), "/dev/video3");

    // only source configured: its card name beats its device path
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "source_video_device_name": "DE-IR",
        "source_video_device": "/dev/video2"
    })")), "DE-IR");

    // last resort: the source device path itself
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "source_video_device": "/dev/video2"
    })")), "/dev/video2");

    std::cout << "passed" << std::endl;
    return true;
}


bool test_cam_id_precedence ()
{
    std::cout << "Testing: cam_id beats device names, loses to overlay_target..." << std::endl;

    // camera.cam_id inside the section beats the device names
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "cam_id": "cam0",
        "output_video_device_name": "DE-TRK",
        "output_video_device": "/dev/video3"
    })")), "cam0");

    // a caller-supplied cam_id (module keeps it at config top level) does
    // the same when the section has none
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "output_video_device_name": "DE-IR-OUT"
    })"), "DE-THERMAL"), "DE-THERMAL");

    // ...but overlay_target still wins over every cam_id
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "overlay_target": "camera2.front",
        "cam_id": "cam0",
        "output_video_device_name": "DE-TRK"
    })"), "OTHER"), "camera2.front");

    // the arg beats a section cam_id it shadows
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "cam_id": "cam9"
    })"), "cam0"), "cam0");

    std::cout << "passed" << std::endl;
    return true;
}


bool test_empty_means_all_cameras ()
{
    std::cout << "Testing: all-empty/missing resolves to \"\" (legacy all-cameras)..." << std::endl;

    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse("{}")), "");
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json()), "");
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse("[1,2]")), "");
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "overlay_target": "",
        "output_video_device_name": "",
        "source_video_device": ""
    })")), "");
    // an empty cam_id arg does not manufacture a target
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse("{}"), ""), "");

    std::cout << "passed" << std::endl;
    return true;
}


bool test_bad_values_are_skipped ()
{
    std::cout << "Testing: empty/non-string values fall through to the next key..." << std::endl;

    // empty overlay_target falls through to output_video_device_name
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "overlay_target": "",
        "output_video_device_name": "DE-TRK"
    })")), "DE-TRK");

    // a numeric overlay_target is not a name: skipped
    ASSERT_EQ_STR(resolveOverlayTarget(nlohmann::json::parse(R"({
        "overlay_target": 57,
        "output_video_device": "/dev/video3"
    })")), "/dev/video3");

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_full_precedence();
    ok &= test_cam_id_precedence();
    ok &= test_empty_means_all_cameras();
    ok &= test_bad_values_are_skipped();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "SOME TESTS FAILED" << std::endl;
    return 1;
}
