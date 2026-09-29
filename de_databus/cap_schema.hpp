#ifndef DE_CAP_SCHEMA_HPP_
#define DE_CAP_SCHEMA_HPP_

/**
 * @file cap_schema.hpp
 * @brief Phase 3 capability advert dialect ("de.cap/1") validator +
 *        parameter checker. Shared by modules (de_common helper),
 *        de_comm's registry and the standalone tests.
 *
 * Dialect summary (normative doc: Tasks/mission_planner/Phase-3-Tasks/
 * CAPABILITY_SCHEMA.md):
 *
 *   { "schema": "de.cap/1", "ns": "camera", "module": "...", "ver": "...",
 *     "actions": { "capture": {"desc", "params": {name: param}, "timeout_s"} },
 *     "events":  { "captured": {"payload": {name: param}} },
 *     "state":   { "recording": param } }
 *
 *   param := { "type": "bool"|"int"|"number"|"string"|"enum"|"geo"|
 *                     "array"|"object" | [scalar-type, ...],
 *              "min", "max", "default", "unit", "desc", "optional",
 *              "values" (enum), "items" (array), "fields" (object) }
 *
 * Names match [a-z][a-z0-9_]* .  Max advert size 16 KB per advert string.
 */

#include <string>
#include <vector>

#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

namespace de
{
namespace comm
{

#define CAP_SCHEMA_ID        "de.cap/1"
#define CAP_ADVERT_MAX_BYTES (16 * 1024)
#define CAP_NAME_MAX_LEN     32

/**
 * @brief advert "ch" hash: first 16 hex chars of SHA-256 over the advert
 * strings exactly as sent, joined with '\n' in the order sent
 * (never re-serialize to hash).
 */
std::string capAdvertHash (const std::vector<std::string>& adverts);

/**
 * @brief true when name matches [a-z][a-z0-9_]* and is within
 * CAP_NAME_MAX_LEN.
 */
bool capIsValidName (const std::string& name);

/**
 * @brief Validate one parsed advert document.
 * @param advert parsed JSON object (parse + size check is the caller's job)
 * @param err    first error found, human readable
 * @return true when the advert conforms to de.cap/1
 */
bool capValidateAdvert (const Json_de& advert, std::string& err);

/**
 * @brief Check invoke `with` params against an action's `params` schema.
 *
 * Types, min/max, enum values, required/optional are enforced; entries with
 * a `default` that are missing from `with` are written into `filled`.
 * `filled` = validated `with` (unknown keys are errors) + injected defaults.
 *
 * @param params_schema  the action's "params" object (may be empty)
 * @param with           params as received ({} treated as empty object)
 * @param filled         out: params to hand to the action handler
 * @return list of errors; empty means clean
 */
std::vector<std::string> capCheckParams (const Json_de& params_schema,
                                        const Json_de& with,
                                        Json_de& filled);

/**
 * @brief Convenience: find action `act` inside advert `advert`, by name or
 * by an action's "deprecated_alias" (the name it had before a rename).
 * @return pointer into advert, or nullptr.
 */
const Json_de* capFindAction (const Json_de& advert, const std::string& act);

/**
 * @brief the current name of action `act` (itself, or the action whose
 * "deprecated_alias" it is), or "" when the advert has no such action.
 * Handlers are always called with this name.
 */
std::string capCanonicalAction (const Json_de& advert, const std::string& act);

} // namespace comm
} // namespace de

#endif
