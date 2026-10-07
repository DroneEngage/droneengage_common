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


bool test_type_list_limits_and_alias ()
{
    std::cout << "Testing: type-list min/max + deprecated_alias lookup..." << std::endl;

    // ["string","int"]: an int value must honour min/max, a name passes
    const Json schema = Json::parse(R"({"pin": {"type": ["string","int"], "min": 0, "max": 40}})");
    Json filled;
    ASSERT_TRUE(de::comm::capCheckParams(schema, Json::parse(R"({"pin": 7})"), filled).empty());
    ASSERT_TRUE(de::comm::capCheckParams(schema, Json::parse(R"({"pin": "drop_servo"})"), filled).empty());
    ASSERT_FALSE(de::comm::capCheckParams(schema, Json::parse(R"({"pin": 99})"), filled).empty());

    const Json advert = Json::parse(R"({"schema":"de.cap/1","ns":"camera","module":"m","ver":"1",
        "actions":{"capture":{"deprecated_alias":"take_photo"}}})");
    ASSERT_TRUE(de::comm::capFindAction(advert, "capture") != nullptr);
    ASSERT_TRUE(de::comm::capFindAction(advert, "take_photo") == de::comm::capFindAction(advert, "capture"));
    ASSERT_TRUE(de::comm::capFindAction(advert, "nope") == nullptr);
    ASSERT_EQ(de::comm::capCanonicalAction(advert, "take_photo"), std::string("capture"));
    ASSERT_EQ(de::comm::capCanonicalAction(advert, "capture"), std::string("capture"));
    ASSERT_EQ(de::comm::capCanonicalAction(advert, "nope"), std::string());

    std::cout << "passed" << std::endl;
    return true;
}


/* ------------------------------------------------------------------ *
 *  TASK-PF-02 — resources + event_patterns + capMatchEventPattern      *
 * ------------------------------------------------------------------ */

static const char* PF02_ADVERT = R"json(
{ "schema": "de.cap/1",
  "ns": "gpio",
  "module": "droneengage_rpi_gpio", "ver": "1.4.1",
  "actions": {
    "write": { "params": { "pin": {"type":["string","int"]}, "value": {"type":"int","min":0} } },
    "read":  { "params": { "pin": {"type":["string","int"]} } }
  },
  "resources": {
    "led":   "out",
    "servo": "pwm",
    "btn":   "in"
  },
  "event_patterns": {
    "*.went_on":  { "kind": "fact",    "payload": { "v": {"type":"int"}, "src": {"type":"string"} } },
    "*.went_off": { "kind": "fact",    "payload": { "v": {"type":"int"}, "src": {"type":"string"} } },
    "*.changed":  { "kind": "fact",    "res": ["pwm"], "payload": { "v": {"type":"int"}, "src": {"type":"string"} } },
    "*.on":       { "kind": "command", "res": ["out","pwm"] },
    "*.off":      { "kind": "command", "res": ["out","pwm"] },
    "*.pulse":    { "kind": "command", "res": ["out"],
                    "payload": { "ms": {"type":"int","optional":true,"min":1,"max":4000} } },
    "*.set":      { "kind": "command", "res": ["pwm"],
                    "payload": { "v": {"type":"int","min":0,"max":1024} } }
  },
  "state": { "led": {"type":"int"}, "servo": {"type":"int"}, "btn": {"type":"int"} }
})json";


bool test_event_patterns_valid ()
{
    std::cout << "Testing: resources + event_patterns validation (PF-02)..." << std::endl;

    std::string err;
    const Json_de advert = Json::parse(PF02_ADVERT);
    ASSERT_TRUE(de::comm::capValidateAdvert(advert, err));

    // advert carrying ONLY the new keys (no actions/events/state)
    const Json_de only_new = Json::parse(
        R"({"schema":"de.cap/1","ns":"gpio","module":"m","ver":"1",
            "resources":{"led":"out"},
            "event_patterns":{"*.went_on":{"kind":"fact"}}})");
    ASSERT_TRUE(de::comm::capValidateAdvert(only_new, err));

    // a pre-PF-02 advert is untouched by the new checks
    ASSERT_TRUE(de::comm::capValidateAdvert(Json::parse(VALID_ADVERT), err));

    // resources: must be an object of name -> name
    Json_de a = advert;
    a["resources"] = Json::array({"led"});
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["resources"]["Bad Key"] = "out";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["resources"]["led"] = 7;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["resources"]["led"] = "Out";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // event_patterns: keys are exactly "*.<verb>"
    a = advert; a["event_patterns"]["went_on"] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.*.*"] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.Went_on"] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*." + std::string(33, 'a')] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"] = Json::array({"*.went_on"});
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.x"] = "fact";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // kind / res / payload / desc shapes
    a = advert; a["event_patterns"]["*.on"]["kind"] = "query";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.on"]["kind"] = 3;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.on"]["res"] = "out";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.on"]["res"] = {"Out"};
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.on"]["res"] = {3};
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.went_on"]["desc"] = 1;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["event_patterns"]["*.pulse"]["payload"]["ms"]["type"] = "frobnicate";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // the pre-PF-02 fake: a dotted/wildcard key inside "events" stays
    // rejected - this is why the keys had to be top-level
    a = advert; a["events"]["*.went_on"] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a = advert; a["events"]["led.went_on"] = Json::object();
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_match_event_pattern ()
{
    std::cout << "Testing: capMatchEventPattern..." << std::endl;

    const Json_de advert = Json::parse(PF02_ADVERT);
    using de::comm::capMatchEventPattern;

    // facts
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "went_on", "fact") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "btn", "went_on", "fact") != nullptr);
    // "res" class whitelist: changed is pwm-only
    ASSERT_TRUE(capMatchEventPattern(advert, "servo", "changed", "fact") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "changed", "fact") == nullptr);
    // commands
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "on", "command") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "servo", "set", "command") != nullptr);
    // an input resource takes no commands
    ASSERT_TRUE(capMatchEventPattern(advert, "btn", "on", "command") == nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "btn", "pulse", "command") == nullptr);
    // wrong kind: "on" is a command, not a fact
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "on", "fact") == nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "went_on", "command") == nullptr);
    // unknown verb / unknown resource key
    ASSERT_TRUE(capMatchEventPattern(advert, "led", "went_onn", "fact") == nullptr);
    ASSERT_TRUE(capMatchEventPattern(advert, "nosuch", "went_on", "fact") == nullptr);
    // kind defaults to "fact" when the pattern omits it
    Json_de a = advert;
    a["event_patterns"]["*.flipped"] = Json::object();
    ASSERT_TRUE(capMatchEventPattern(a, "led", "flipped", "fact") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(a, "led", "flipped", "command") == nullptr);
    // the returned pattern is the advert's object (payload readable)
    const Json_de* pat = capMatchEventPattern(advert, "led", "pulse", "command");
    ASSERT_TRUE(pat != nullptr);
    ASSERT_TRUE((*pat)["payload"]["ms"]["max"] == 4000);
    // no event_patterns / no resources at all -> nothing matches,
    // and an unresourced advert matches any key
    ASSERT_TRUE(capMatchEventPattern(Json::parse(VALID_ADVERT), "led", "went_on", "fact") == nullptr);
    a = advert;
    a.erase("resources");
    ASSERT_TRUE(capMatchEventPattern(a, "anykey", "went_on", "fact") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(a, "anykey", "on", "command") != nullptr);
    ASSERT_TRUE(capMatchEventPattern(a, "anykey", "set", "command") != nullptr);  // res w/o resources: unchecked
    // defensive: malformed members -> no match, never a crash
    a = advert; a["event_patterns"] = 5;
    ASSERT_TRUE(capMatchEventPattern(a, "led", "went_on", "fact") == nullptr);
    a = advert; a["resources"] = 5;
    ASSERT_TRUE(capMatchEventPattern(a, "led", "went_on", "fact") == nullptr);

    std::cout << "passed" << std::endl;
    return true;
}


/* ------------------------------------------------------------------ *
 *  TASK-PF-01 — the per-action "claim" flag                            *
 * ------------------------------------------------------------------ */

bool test_action_claim ()
{
    std::cout << "Testing: action 'claim' validation (PF-01)..." << std::endl;

    std::string err;
    Json_de a = Json::parse(PF02_ADVERT);

    // both spellings accepted
    a["actions"]["write"]["claim"] = "ns";
    a["actions"]["read"]["claim"] = "action";
    ASSERT_TRUE(de::comm::capValidateAdvert(a, err));

    // a bad value rejects
    a["actions"]["read"]["claim"] = "pin";
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a["actions"]["read"]["claim"] = 1;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));
    a["actions"]["read"]["claim"] = true;
    ASSERT_FALSE(de::comm::capValidateAdvert(a, err));

    // an old advert (no claim anywhere) is unchanged
    ASSERT_TRUE(de::comm::capValidateAdvert(Json::parse(VALID_ADVERT), err));

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
    ok &= test_type_list_limits_and_alias();
    ok &= test_event_patterns_valid();
    ok &= test_match_event_pattern();
    ok &= test_action_claim();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
