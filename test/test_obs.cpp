/**
 * @file test_obs.cpp
 * @brief standalone tests for the observation message builder/parser
 *        (helpers/obs.hpp, TYPE_AndruavMessage_AI_OBSERVATIONS 6546).
 *
 * Build & run:  make -C test   &&   ./test/test_obs
 */

#include <cmath>
#include <iostream>
#include <string>

#include "../helpers/json_nlohmann.hpp"
#include "../helpers/obs.hpp"

using Json = nlohmann::json;

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "ASSERTION FAILED: " << #condition << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))
#define ASSERT_EQ(val1, val2) ASSERT_TRUE((val1) == (val2))
#define ASSERT_NEAR(val1, val2, eps) ASSERT_TRUE(std::fabs((val1) - (val2)) <= (eps))


static bool parses (const char* text)
{
    de::ai::ObservationFrame f;
    std::string err;
    return de::ai::obsParse(Json::parse(text), f, err);
}


bool test_round_trip ()
{
    std::cout << "Testing: build -> parse round trip..." << std::endl;

    de::ai::ObservationFrame in;
    in.cam_id = "DE-CAM1";
    in.tm_capture = 123456789;

    de::ai::Observation a;
    a.cls = "person"; a.conf = 0.91;
    a.x = 0.40; a.y = 0.25; a.w = 0.10; a.h = 0.30;
    a.has_trk = true; a.trk = 7;
    a.has_cls_id = true; a.cls_id = 0;
    a.attrs = Json::parse(R"({"temp_c":36.5})");
    in.obs.push_back(a);

    de::ai::Observation b;
    b.cls = "car"; b.conf = 0.5;
    b.x = 0.0; b.y = 0.0; b.w = 1.0; b.h = 1.0;
    in.obs.push_back(b);

    Json wire;
    std::string err;
    ASSERT_TRUE(de::ai::obsBuild(in, wire, err));

    ASSERT_EQ(wire["cam_id"].get<std::string>(), std::string("DE-CAM1"));
    ASSERT_FALSE(wire["obs"][1].contains("trk"));
    ASSERT_FALSE(wire["obs"][1].contains("attrs"));
    ASSERT_FALSE(wire.contains("b"));

    de::ai::ObservationFrame out;
    ASSERT_TRUE(de::ai::obsParse(wire, out, err));
    ASSERT_EQ(out.cam_id, in.cam_id);
    ASSERT_EQ(out.tm_capture, in.tm_capture);
    ASSERT_EQ(out.obs.size(), (size_t) 2);

    ASSERT_TRUE(out.obs[0].has_trk);
    ASSERT_EQ(out.obs[0].trk, (int64_t) 7);
    ASSERT_TRUE(out.obs[0].has_cls_id);
    ASSERT_EQ(out.obs[0].cls_id, 0);
    ASSERT_EQ(out.obs[0].cls, std::string("person"));
    ASSERT_NEAR(out.obs[0].conf, 0.91, 1e-9);
    ASSERT_NEAR(out.obs[0].x, 0.40, 1e-9);
    ASSERT_NEAR(out.obs[0].h, 0.30, 1e-9);
    ASSERT_NEAR(out.obs[0].attrs["temp_c"].get<double>(), 36.5, 1e-9);

    ASSERT_FALSE(out.obs[1].has_trk);
    ASSERT_FALSE(out.obs[1].has_cls_id);
    ASSERT_TRUE(out.obs[1].attrs.is_null());

    std::cout << "passed" << std::endl;
    return true;
}


bool test_required_fields ()
{
    std::cout << "Testing: required fields..." << std::endl;

    const char* good = R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"person","conf":0.9,"box":[0.1,0.1,0.2,0.2]}]})";
    ASSERT_TRUE(parses(good));

    ASSERT_TRUE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[]})"));

    // missing cls / conf / box
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"conf":0.9,"box":[0.1,0.1,0.2,0.2]}]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"person","box":[0.1,0.1,0.2,0.2]}]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"person","conf":0.9}]})"));

    // no aliases, no defaults
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"person","confidence":0.9,"box":[0.1,0.1,0.2,0.2]}]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"class_name":"person","conf":0.9,"box":[0.1,0.1,0.2,0.2]}]})"));

    // empty / non-string cls
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"","conf":0.9,"box":[0.1,0.1,0.2,0.2]}]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":3,"conf":0.9,"box":[0.1,0.1,0.2,0.2]}]})"));

    // frame-level fields
    ASSERT_FALSE(parses(R"({"tm_capture":1,"obs":[]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"","tm_capture":1,"obs":[]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","obs":[]})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1})"));
    ASSERT_FALSE(parses(R"({"cam_id":"C","tm_capture":1,"b":{"x":0.1,"y":0.1,"w":0.2,"h":0.2}})"));
    ASSERT_FALSE(parses(R"([])"));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_conf_and_box_range ()
{
    std::cout << "Testing: conf and box range..." << std::endl;

    const char* head = R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"person",)";
    auto frame = [&](const std::string& tail) { return std::string(head) + tail + "}]}"; };

    ASSERT_TRUE(parses(frame(R"("conf":0,"box":[0,0,1,1])").c_str()));
    ASSERT_TRUE(parses(frame(R"("conf":1,"box":[0.9,0.9,0.1,0.1])").c_str()));

    ASSERT_FALSE(parses(frame(R"("conf":1.01,"box":[0.1,0.1,0.2,0.2])").c_str()));
    ASSERT_FALSE(parses(frame(R"("conf":-0.1,"box":[0.1,0.1,0.2,0.2])").c_str()));

    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[-0.1,0.1,0.2,0.2])").c_str()));
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[0.1,1.2,0.2,0.2])").c_str()));
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[0.9,0.1,0.2,0.2])").c_str()));   // x+w > 1
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[0.1,0.9,0.2,0.2])").c_str()));   // y+h > 1
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[0.1,0.1,0,0.2])").c_str()));     // w == 0
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[-0.05,-0.05,0.1,0.1])").c_str())); // centred convention
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":[0.1,0.1,0.2])").c_str()));       // 3 values
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":{"x":0.1,"y":0.1,"w":0.2,"h":0.2})").c_str()));
    ASSERT_FALSE(parses(frame(R"("conf":0.9,"box":["a",0.1,0.2,0.2])").c_str()));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_optional_fields ()
{
    std::cout << "Testing: optional trk/cls_id/attrs..." << std::endl;

    const char* head = R"({"cam_id":"C","tm_capture":1,"obs":[{"cls":"hotspot","conf":0.8,"box":[0.1,0.1,0.2,0.2],)";
    auto frame = [&](const std::string& tail) { return std::string(head) + tail + "}]}"; };

    ASSERT_TRUE(parses(frame(R"("trk":3)").c_str()));
    ASSERT_TRUE(parses(frame(R"("cls_id":2)").c_str()));
    ASSERT_TRUE(parses(frame(R"("attrs":{"temp_c":41.2})").c_str()));
    ASSERT_TRUE(parses(frame(R"("trk":3,"cls_id":2,"attrs":{})").c_str()));

    ASSERT_FALSE(parses(frame(R"("trk":"3")").c_str()));
    ASSERT_FALSE(parses(frame(R"("trk":1.5)").c_str()));
    ASSERT_FALSE(parses(frame(R"("cls_id":"x")").c_str()));
    ASSERT_FALSE(parses(frame(R"("attrs":[1,2])").c_str()));
    ASSERT_FALSE(parses(frame(R"("attrs":"hot")").c_str()));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_build_rejects_bad_frames ()
{
    std::cout << "Testing: builder rejects bad frames..." << std::endl;

    Json wire;
    std::string err;

    de::ai::ObservationFrame f;
    ASSERT_FALSE(de::ai::obsBuild(f, wire, err));       // empty cam_id

    f.cam_id = "C";
    ASSERT_TRUE(de::ai::obsBuild(f, wire, err));        // no observations is fine
    ASSERT_EQ(wire["obs"].size(), (size_t) 0);

    de::ai::Observation o;
    o.cls = "person"; o.conf = 0.9;
    o.x = 0.1; o.y = 0.1; o.w = 0.2; o.h = 0.2;
    f.obs.push_back(o);
    ASSERT_TRUE(de::ai::obsBuild(f, wire, err));

    f.obs[0].conf = 1.5;
    ASSERT_FALSE(de::ai::obsBuild(f, wire, err));
    f.obs[0].conf = 0.9;

    f.obs[0].cls.clear();
    ASSERT_FALSE(de::ai::obsBuild(f, wire, err));
    f.obs[0].cls = "person";

    f.obs[0].x = 0.95;                                  // x+w > 1
    ASSERT_FALSE(de::ai::obsBuild(f, wire, err));
    f.obs[0].x = 0.1;

    f.obs[0].attrs = Json::parse("[1]");
    ASSERT_FALSE(de::ai::obsBuild(f, wire, err));

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_round_trip();
    ok &= test_required_fields();
    ok &= test_conf_and_box_range();
    ok &= test_optional_fields();
    ok &= test_build_rejects_bad_frames();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
