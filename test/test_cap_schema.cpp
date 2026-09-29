/**
 * @file test_cap_schema.cpp
 * @brief standalone tests for the Phase-3 capability advert dialect
 *        (de.cap/1) and the self-contained SHA-256 helper.
 *
 * Build & run:  make -C test test-cap   &&   ./test/test_cap_schema
 */

#include <iostream>
#include <string>
#include <vector>

#include "../helpers/json_nlohmann.hpp"
#include "../helpers/sha256.hpp"
#include "../de_databus/cap_schema.hpp"
#include "../de_databus/messages.hpp"

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


bool test_sha256_vectors ()
{
    std::cout << "Testing: sha256 FIPS 180-2 vectors..." << std::endl;

    ASSERT_EQ(de::helpers::sha256_hex(""),
        std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    ASSERT_EQ(de::helpers::sha256_hex("abc"),
        std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    // 448-bit message
    ASSERT_EQ(de::helpers::sha256_hex(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_advert_hash ()
{
    std::cout << "Testing: capAdvertHash..." << std::endl;

    const std::vector<std::string> adverts = {
        "{\"schema\":\"de.cap/1\",\"ns\":\"camera\"}",
        "{\"schema\":\"de.cap/1\",\"ns\":\"payload\"}"
    };
    const std::string h = de::comm::capAdvertHash(adverts);
    ASSERT_EQ(h.size(), (size_t)16);

    // order matters
    const std::vector<std::string> swapped = {adverts[1], adverts[0]};
    ASSERT_TRUE(de::comm::capAdvertHash(swapped) != h);

    // same bytes as sent -> same hash (never re-serialize)
    ASSERT_EQ(de::comm::capAdvertHash(adverts), h);

    std::cout << "passed" << std::endl;
    return true;
}


static const char* VALID_ADVERT = R"json(
{ "schema": "de.cap/1",
  "ns": "camera",
  "module": "droneengage_camera", "ver": "3.2.1",
  "actions": {
    "capture": { "desc": "Take photos",
                 "params": { "count":       {"type":"int", "min":1, "max":100, "default":1},
                             "interval_ms": {"type":"int", "min":0, "max":60000, "default":0, "unit":"ms"} },
                 "timeout_s": 10 },
    "record":  { "params": { "on": {"type":"bool"} } }
  },
  "events": {
    "captured": { "payload": { "file": {"type":"string"}, "pos": {"type":"geo"} } }
  },
  "state": { "recording": {"type":"bool"} }
})json";


bool test_advert_valid ()
{
    std::cout << "Testing: valid advert..." << std::endl;

    std::string err;
    ASSERT_TRUE(de::comm::capValidateAdvert(Json::parse(VALID_ADVERT), err));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_advert_invalid ()
{
    std::cout << "Testing: invalid adverts..." << std::endl;

    std::string err;

    // bad schema id
    Json_de a = Json::parse(VALID_ADVERT);
    a["schema"] = "de.cap/9";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // missing ns
    a = Json::parse(VALID_ADVERT);
    a.erase("ns");
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // bad ns name (uppercase / leading digit)
    a = Json::parse(VALID_ADVERT);
    a["ns"] = "Camera";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a["ns"] = "9cam";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // unknown param type
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["count"] = {{"type", "frobnicate"}};
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // enum without values
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["mode"] = {{"type", "enum"}};
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // default out of range
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["count"]["default"] = 500;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // min > max
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["count"]["min"] = 200;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // list-of-types with a composite type is rejected
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["channel"] = {{"type", {"string", "geo"}}};
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // list-of-types with scalars is fine (gpio.write pin: name|number)
    a = Json::parse(VALID_ADVERT);
    a["actions"]["capture"]["params"]["channel"] = {{"type", {"string", "int"}}};
    ASSERT_TRUE(de::comm::capValidateAdvert(a, err));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_check_params ()
{
    std::cout << "Testing: capCheckParams..." << std::endl;

    const Json advert = Json::parse(VALID_ADVERT);
    const Json_de* capture = de::comm::capFindAction(advert, "capture");
    ASSERT_TRUE(capture != nullptr);
    const Json schema = (*capture)["params"];

    Json filled;
    std::vector<std::string> errs;

    // empty with -> defaults filled
    errs = de::comm::capCheckParams(schema, Json::object(), filled);
    ASSERT_TRUE(errs.empty());
    ASSERT_EQ(filled["count"], 1);
    ASSERT_EQ(filled["interval_ms"], 0);

    // valid values kept
    errs = de::comm::capCheckParams(schema, {{"count", 3}, {"interval_ms", 500}}, filled);
    ASSERT_TRUE(errs.empty());
    ASSERT_EQ(filled["count"], 3);

    // over max
    errs = de::comm::capCheckParams(schema, {{"count", 500}}, filled);
    ASSERT_FALSE(errs.empty());

    // wrong type
    errs = de::comm::capCheckParams(schema, {{"count", "three"}}, filled);
    ASSERT_FALSE(errs.empty());

    // unknown param rejected
    errs = de::comm::capCheckParams(schema, {{"count", 3}, {"bogus", 1}}, filled);
    ASSERT_FALSE(errs.empty());

    // record: "on" required
    const Json_de* record = de::comm::capFindAction(advert, "record");
    ASSERT_TRUE(record != nullptr);
    errs = de::comm::capCheckParams((*record)["params"], Json::object(), filled);
    ASSERT_FALSE(errs.empty());   // missing required "on"
    errs = de::comm::capCheckParams((*record)["params"], {{"on", true}}, filled);
    ASSERT_TRUE(errs.empty());

    // list-of-types param accepts either form
    const Json gpio_schema = {{"pin", {{"type", {"string", "int"}}}},
                              {"value", {{"type", "int"}}}};
    errs = de::comm::capCheckParams(gpio_schema, {{"pin", "led1"}, {"value", 1}}, filled);
    ASSERT_TRUE(errs.empty());
    errs = de::comm::capCheckParams(gpio_schema, {{"pin", 17}, {"value", 1}}, filled);
    ASSERT_TRUE(errs.empty());
    errs = de::comm::capCheckParams(gpio_schema, {{"pin", true}, {"value", 1}}, filled);
    ASSERT_FALSE(errs.empty());

    // geo param
    const Json geo_schema = {{"pos", {{"type", "geo"}}}};
    errs = de::comm::capCheckParams(geo_schema, {{"pos", {30.0, 31.0, 100.0}}}, filled);
    ASSERT_TRUE(errs.empty());
    errs = de::comm::capCheckParams(geo_schema, {{"pos", {30.0, 31.0}}}, filled);
    ASSERT_FALSE(errs.empty());

    // enum param
    const Json enum_schema = {{"class", {{"type", "enum"}, {"values", {"person", "car"}}}}};
    errs = de::comm::capCheckParams(enum_schema, {{"class", "person"}}, filled);
    ASSERT_TRUE(errs.empty());
    errs = de::comm::capCheckParams(enum_schema, {{"class", "alien"}}, filled);
    ASSERT_FALSE(errs.empty());

    std::cout << "passed" << std::endl;
    return true;
}


bool test_message_ids ()
{
    std::cout << "Testing: Phase-3 message IDs defined + free..." << std::endl;

    ASSERT_EQ(TYPE_AndruavMessage_MODULE_CAPABILITIES, 6542);
    ASSERT_EQ(TYPE_AndruavMessage_CAPABILITY_INVOKE, 6543);
    ASSERT_EQ(TYPE_AndruavMessage_CAPABILITY_RESULT, 6544);
    ASSERT_EQ(TYPE_AndruavMessage_MODULE_STATE, 6545);
    ASSERT_EQ(TYPE_AndruavMessage_UNIT_CAPABILITIES, 1089);
    // neighbors still belong to Phase 2 / earlier phases
    ASSERT_EQ(TYPE_AndruavMessage_NAV_ADAPTER_STATUS, 6541);
    ASSERT_EQ(TYPE_AndruavMessage_DE_Supervisor_Journal, 1088);

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_sha256_vectors();
    ok &= test_advert_hash();
    ok &= test_advert_valid();
    ok &= test_advert_invalid();
    ok &= test_check_params();
    ok &= test_message_ids();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
