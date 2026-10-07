/**
 * @file test_ai_box.cpp
 * @brief standalone tests for the shared 1078 best-object box reader
 *        (helpers/ai_box.hpp, TASK-B4-01).
 *
 * Covers: geometry guard, "conf"/"confidence" alias + 1.0 legacy default,
 * class_id/class_name extraction, name/id/list precedence, and the
 * legacy selected-class fallback for boxes with no class metadata.
 *
 * Build & run:  make -C test   &&   ./test/test_ai_box
 */

#include <iostream>
#include <string>
#include <vector>

#include "../helpers/json_nlohmann.hpp"
#include "../helpers/ai_box.hpp"

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


static const std::vector<std::string> COCO_NAMES = {
    "person", "bicycle", "car"
};


bool test_geometry_guard ()
{
    std::cout << "Testing: geometry guard..." << std::endl;

    de::ai::AiBox box;

    ASSERT_FALSE(de::ai::aiBoxParse(Json::parse(R"({})"), box));
    ASSERT_FALSE(de::ai::aiBoxParse(Json::parse(R"({"x":0.1,"y":0.2,"w":0.3})"), box));
    ASSERT_FALSE(de::ai::aiBoxParse(Json::parse(R"({"x":"a","y":0.2,"w":0.3,"h":0.4})"), box));
    ASSERT_FALSE(de::ai::aiBoxParse(Json::parse(R"([0.1,0.2,0.3,0.4])"), box));
    ASSERT_FALSE(de::ai::aiBoxParse(Json::parse(R"(42)"), box));

    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0.1,"y":0.2,"w":0.3,"h":0.4})"), box));
    ASSERT_NEAR(box.x, 0.1, 1e-9);
    ASSERT_NEAR(box.y, 0.2, 1e-9);
    ASSERT_NEAR(box.w, 0.3, 1e-9);
    ASSERT_NEAR(box.h, 0.4, 1e-9);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_confidence ()
{
    std::cout << "Testing: confidence read + alias + default..." << std::endl;

    de::ai::AiBox box;

    // legacy box: no confidence -> 1.0
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1})"), box));
    ASSERT_NEAR(box.confidence, 1.0, 1e-9);

    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1,"conf":0.42})"), box));
    ASSERT_NEAR(box.confidence, 0.42, 1e-9);

    // generic producer alias
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1,"confidence":0.4})"), box));
    ASSERT_NEAR(box.confidence, 0.4, 1e-9);

    // canonical "conf" wins when both are present
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1,"conf":0.7,"confidence":0.4})"), box));
    ASSERT_NEAR(box.confidence, 0.7, 1e-9);

    // non-number confidence is ignored -> default
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1,"conf":"high"})"), box));
    ASSERT_NEAR(box.confidence, 1.0, 1e-9);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_class_fields ()
{
    std::cout << "Testing: class_id / class_name extraction..." << std::endl;

    de::ai::AiBox box;

    // legacy box: no class metadata
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1})"), box));
    ASSERT_FALSE(box.has_class_id);
    ASSERT_EQ(box.class_id, -1);
    ASSERT_TRUE(box.class_name.empty());

    // generic-shaped box (extra px/py keys must be ignored)
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0.1,"y":0.2,"w":0.3,"h":0.4,"class_id":2,
            "class_name":"car","confidence":0.9,"px":10,"py":20})"), box));
    ASSERT_TRUE(box.has_class_id);
    ASSERT_EQ(box.class_id, 2);
    ASSERT_EQ(box.class_name, std::string("car"));

    // empty name counts as absent; the id still parses
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":1,"class_name":""})"), box));
    ASSERT_TRUE(box.has_class_id);
    ASSERT_EQ(box.class_id, 1);
    ASSERT_TRUE(box.class_name.empty());

    // non-string name / non-number id count as absent
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":"car","class_name":7})"), box));
    ASSERT_FALSE(box.has_class_id);
    ASSERT_TRUE(box.class_name.empty());

    std::cout << "passed" << std::endl;
    return true;
}


bool test_class_name_resolution ()
{
    std::cout << "Testing: resolved class name precedence..." << std::endl;

    de::ai::AiBox box;

    // box name wins over the selected (fallback) class
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":2,"class_name":"car"})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("car"));

    // id-only box resolves through the receiver's class list
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":1})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("bicycle"));

    // empty name + in-range id still resolves through the list
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":0,"class_name":""})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "bicycle"),
              std::string("person"));

    // out-of-range id with no name -> legacy fallback
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":99})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("person"));

    // name wins when it disagrees with the id
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":0,"class_name":"car"})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("car"));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_legacy_fallback ()
{
    std::cout << "Testing: legacy fallback semantics..." << std::endl;

    de::ai::AiBox box;
    ASSERT_TRUE(de::ai::aiBoxParse(
        Json::parse(R"({"x":0,"y":0,"w":0.1,"h":0.1,"conf":0.8})"), box));

    // old producer + selected class -> selected class (pre-B4-01 behaviour)
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("person"));

    // old producer + nothing selected -> empty (payload "class": null)
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, ""),
              std::string());

    // negative id is not a usable index -> fallback
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":-3})"), box));
    ASSERT_TRUE(box.has_class_id);
    ASSERT_EQ(de::ai::aiBoxClassName(box, COCO_NAMES, "person"),
              std::string("person"));

    // empty receiver class list cannot resolve an id -> fallback
    const std::vector<std::string> no_names;
    ASSERT_TRUE(de::ai::aiBoxParse(Json::parse(
        R"({"x":0,"y":0,"w":0.1,"h":0.1,"class_id":0})"), box));
    ASSERT_EQ(de::ai::aiBoxClassName(box, no_names, "person"),
              std::string("person"));

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_geometry_guard();
    ok &= test_confidence();
    ok &= test_class_fields();
    ok &= test_class_name_resolution();
    ok &= test_legacy_fallback();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
