/**
 * @file test_camera_registry.cpp
 * @brief tests for helpers/camera_registry.hpp (CA-0012): load a
 * multi-camera registry, per-cam_id intrinsics, atomic entry update.
 *
 * Build & run:  make -C test && ./test/test_camera_registry
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "../helpers/camera_registry.hpp"

using namespace de::camera;

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "ASSERTION FAILED: " << #condition << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::fabs((a) - (b)) <= (eps))


static std::string writeFile (const char* path, const std::string& body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
    out.close();
    return path;
}


/// two cameras with different intrinsics + mounts
static const char* TWO_CAMERAS = R"({
  "cameras": {
    "DOWN": {
      "image_width": 640, "image_height": 480,
      "camera_matrix": [700.0, 0.0, 320.0, 0.0, 701.0, 240.0, 0.0, 0.0, 1.0],
      "distortion": [-0.1, 0.5, 0.001, 0.002, -0.2],
      "mount_pitch_deg": -90.0,
      "hfov_deg": 48.9
    },
    "FWD": {
      "image_width": 1280, "image_height": 720,
      "camera_matrix": [900.0, 0.0, 640.0, 0.0, 902.0, 360.0, 0.0, 0.0, 1.0],
      "distortion": [0.0, 0.0],
      "mount_pitch_deg": -30.0, "mount_yaw_deg": 15.0,
      "hfov_deg": 70.8
    }
  }
})";


bool test_load_two_cameras ()
{
    std::cout << "Testing: a 2-camera registry returns the right intrinsics per cam_id..." << std::endl;

    const std::string path = "/tmp/de_test_camera_registry_two.json";
    writeFile(path.c_str(), TWO_CAMERAS);

    CCameraRegistry reg;
    std::string err;
    ASSERT_TRUE(reg.load(path, err));
    ASSERT_TRUE(reg.size() == 2);

    const CameraRegistryEntry* down = reg.find("DOWN");
    const CameraRegistryEntry* fwd  = reg.find("FWD");
    ASSERT_TRUE(down != nullptr && fwd != nullptr);
    ASSERT_TRUE(reg.find("GHOST") == nullptr);

    // intrinsics per cam_id
    ASSERT_NEAR(down->fx(), 700.0, 1e-9);
    ASSERT_NEAR(down->fy(), 701.0, 1e-9);
    ASSERT_NEAR(down->cx(), 320.0, 1e-9);
    ASSERT_TRUE(down->image_width == 640 && down->image_height == 480);
    ASSERT_TRUE(down->distortion.size() == 5);
    ASSERT_NEAR(down->distortion[0], -0.1, 1e-9);
    ASSERT_NEAR(down->distortion[1], 0.5, 1e-9);

    ASSERT_NEAR(fwd->fx(), 900.0, 1e-9);
    ASSERT_TRUE(fwd->image_width == 1280 && fwd->image_height == 720);

    // mounts
    ASSERT_NEAR(down->mount_pitch_deg, -90.0, 1e-9);
    ASSERT_NEAR(down->mount_yaw_deg,     0.0, 1e-9);   // missing -> default
    ASSERT_NEAR(fwd->mount_pitch_deg, -30.0, 1e-9);
    ASSERT_NEAR(fwd->mount_yaw_deg,    15.0, 1e-9);

    // stored hfov honoured
    ASSERT_NEAR(down->hfovDeg(), 48.9, 1e-6);
    ASSERT_NEAR(fwd->hfovDeg(),  70.8, 1e-6);
    // vfov derived from fy/height
    ASSERT_NEAR(down->vfovDeg(), 2.0 * std::atan(480.0 / (2.0 * 701.0)) * 180.0 / M_PI, 1e-9);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_derived_hfov ()
{
    std::cout << "Testing: hfov derives from fx/width when not stored..." << std::endl;

    const std::string path = "/tmp/de_test_camera_registry_derived.json";
    writeFile(path.c_str(), R"({"cameras":{"C":{
        "image_width":1000,"image_height":500,
        "camera_matrix":[500.0,0.0,500.0, 0.0,500.0,250.0, 0.0,0.0,1.0]}}})");

    CCameraRegistry reg;
    std::string err;
    ASSERT_TRUE(reg.load(path, err));

    const CameraRegistryEntry* c = reg.find("C");
    ASSERT_TRUE(c != nullptr && c->validIntrinsics());
    // fx = width/2 -> hfov = 90 deg; fy = height -> vfov = 2*atan(0.5)
    ASSERT_NEAR(c->hfovDeg(), 90.0, 1e-9);
    ASSERT_NEAR(c->vfovDeg(), 2.0 * std::atan(0.5) * 180.0 / M_PI, 1e-9);
    ASSERT_NEAR(c->mount_pitch_deg, -90.0, 1e-9);      // mount defaults

    std::cout << "passed" << std::endl;
    return true;
}


bool test_bad_files ()
{
    std::cout << "Testing: missing/invalid files fail with an error, bad entries are skipped..." << std::endl;

    CCameraRegistry reg;
    std::string err;
    ASSERT_FALSE(reg.load("/tmp/de_test_camera_registry_missing.json", err));
    ASSERT_TRUE(!err.empty());

    writeFile("/tmp/de_test_camera_registry_notjson.json", "not json at all {{{");
    ASSERT_FALSE(reg.load("/tmp/de_test_camera_registry_notjson.json", err));

    writeFile("/tmp/de_test_camera_registry_nocams.json", R"({"other":1})");
    ASSERT_FALSE(reg.load("/tmp/de_test_camera_registry_nocams.json", err));

    // one bad entry does not sink the file
    writeFile("/tmp/de_test_camera_registry_badentry.json", R"({"cameras":{
        "GOOD":{"image_width":640,"image_height":480,
                "camera_matrix":[700,0,320, 0,700,240, 0,0,1]},
        "BAD":{"camera_matrix":[1,2,3]}}})");
    ASSERT_TRUE(reg.load("/tmp/de_test_camera_registry_badentry.json", err));
    ASSERT_TRUE(reg.size() == 1 && reg.find("GOOD") != nullptr);
    ASSERT_TRUE(!err.empty());   // the skipped entry's error is reported

    std::cout << "passed" << std::endl;
    return true;
}


bool test_update_entry ()
{
    std::cout << "Testing: cameraRegistryUpdateEntry rewrites one cam_id and keeps the rest..." << std::endl;

    const std::string path = "/tmp/de_test_camera_registry_update.json";
    writeFile(path.c_str(), TWO_CAMERAS);

    CameraRegistryEntry e;
    e.image_width  = 1920; e.image_height = 1080;
    e.camera_matrix = {1200.0, 0.0, 960.0, 0.0, 1200.0, 540.0, 0.0, 0.0, 1.0};
    e.distortion    = {0.01, -0.02, 0.0, 0.0, 0.0};
    e.mount_pitch_deg = -45.0;
    e.hfov_deg = 77.3;

    std::string err;
    ASSERT_TRUE(cameraRegistryUpdateEntry(path, "DOWN", e, err));
    ASSERT_TRUE(err.empty());

    CCameraRegistry reg;
    ASSERT_TRUE(reg.load(path, err));
    ASSERT_TRUE(reg.size() == 2);                       // FWD survived
    const CameraRegistryEntry* down = reg.find("DOWN");
    ASSERT_TRUE(down != nullptr);
    ASSERT_NEAR(down->fx(), 1200.0, 1e-9);
    ASSERT_TRUE(down->image_width == 1920);
    ASSERT_NEAR(down->mount_pitch_deg, -45.0, 1e-9);
    ASSERT_TRUE(reg.find("FWD") != nullptr);
    ASSERT_NEAR(reg.find("FWD")->fx(), 900.0, 1e-9);

    // a brand-new registry file is created by update
    const std::string fresh = "/tmp/de_test_camera_registry_new.json";
    std::remove(fresh.c_str());
    ASSERT_TRUE(cameraRegistryUpdateEntry(fresh, "CAM0", e, err));
    ASSERT_TRUE(reg.load(fresh, err));
    ASSERT_TRUE(reg.size() == 1 && reg.find("CAM0") != nullptr);

    ASSERT_FALSE(cameraRegistryUpdateEntry(path, "", e, err));   // empty cam_id refused

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_load_two_cameras();
    ok &= test_derived_hfov();
    ok &= test_bad_files();
    ok &= test_update_entry();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "SOME TESTS FAILED" << std::endl;
    return 1;
}
