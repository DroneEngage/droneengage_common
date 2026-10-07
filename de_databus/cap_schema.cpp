#include <set>

#include "cap_schema.hpp"
#include "../helpers/sha256.hpp"


static const std::set<std::string>& capScalarTypes ()
{
    static const std::set<std::string> types = {
        "bool", "int", "number", "string", "enum", "geo", "array", "object"
    };
    return types;
}


/**
 * @brief "type" may be a single type name or a list of type names
 * (["string","int"] - the value may take any of them; exists for wire
 * params like gpio.write.pin that already accept several forms).
 */
static bool capTypeList (const Json_de& param, std::vector<std::string>& out)
{
    out.clear();
    if (!param.contains("type")) return false;

    const Json_de& t = param["type"];
    if (t.is_string())
    {
        out.push_back(t.get<std::string>());
        return true;
    }
    if (t.is_array() && !t.empty())
    {
        for (const Json_de& e : t)
        {
            if (!e.is_string()) { out.clear(); return false; }
            out.push_back(e.get<std::string>());
        }
        return true;
    }
    return false;
}


static bool capValidateParam (const Json_de& param, const std::string& path,
                              std::string& err, int depth)
{
    if (depth > 4)
    {
        err = path + ": param nesting too deep";
        return false;
    }
    if (!param.is_object())
    {
        err = path + ": param must be an object";
        return false;
    }

    std::vector<std::string> types;
    if (!capTypeList(param, types))
    {
        err = path + ": missing or invalid 'type'";
        return false;
    }
    for (const std::string& t : types)
    {
        if (!capScalarTypes().count(t))
        {
            err = path + ": unknown type '" + t + "'";
            return false;
        }
    }

    // a list only carries scalar types - no enum/geo/array/object inside
    if (types.size() > 1)
    {
        for (const std::string& t : types)
        {
            if (t == "enum" || t == "geo" || t == "array" || t == "object")
            {
                err = path + ": type list allows scalar types only";
                return false;
            }
        }
    }
    const std::string& t = types.front();

    if (t == "enum")
    {
        if (!param.contains("values") || !param["values"].is_array()
            || param["values"].empty())
        {
            err = path + ": enum requires a non-empty 'values' array";
            return false;
        }
    }
    if (t == "array" && param.contains("items"))
    {
        if (!capValidateParam(param["items"], path + "[]", err, depth + 1))
            return false;
    }
    if (t == "object" && param.contains("fields"))
    {
        if (!param["fields"].is_object())
        {
            err = path + ": 'fields' must be an object";
            return false;
        }
        for (auto it = param["fields"].begin(); it != param["fields"].end(); ++it)
        {
            if (!de::comm::capIsValidName(it.key()))
            {
                err = path + ": bad field name '" + it.key() + "'";
                return false;
            }
            if (!capValidateParam(it.value(), path + "." + it.key(), err, depth + 1))
                return false;
        }
    }
    if (param.contains("min") && !param["min"].is_number())
    {
        err = path + ": 'min' must be a number";
        return false;
    }
    if (param.contains("max") && !param["max"].is_number())
    {
        err = path + ": 'max' must be a number";
        return false;
    }
    if (param.contains("min") && param.contains("max")
        && param["min"].is_number() && param["max"].is_number()
        && param["min"].get<double>() > param["max"].get<double>())
    {
        err = path + ": 'min' > 'max'";
        return false;
    }
    if (param.contains("default"))
    {
        // default must itself be a valid value for the declared type
        Json_de fake_schema = Json_de::object({{path, param}});
        Json_de filled;
        Json_de with = Json_de::object({{path, param["default"]}});
        std::vector<std::string> errs =
            de::comm::capCheckParams(fake_schema, with, filled);
        if (!errs.empty())
        {
            err = path + ": bad default (" + errs.front() + ")";
            return false;
        }
    }

    return true;
}


/**
 * @brief validate a {name: param} map (action params, event payload, state).
 */
static bool capValidateParamMap (const Json_de& map, const std::string& path,
                                 std::string& err)
{
    if (!map.is_object())
    {
        err = path + " must be an object";
        return false;
    }
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        if (!de::comm::capIsValidName(it.key()))
        {
            err = path + ": bad name '" + it.key() + "'";
            return false;
        }
        if (!capValidateParam(it.value(), path + "." + it.key(), err, 0))
            return false;
    }
    return true;
}


std::string de::comm::capAdvertHash (const std::vector<std::string>& adverts)
{
    std::string joined;
    for (const std::string& a : adverts)
    {
        if (!joined.empty()) joined += '\n';
        joined += a;
    }
    return de::helpers::sha256_hex(joined).substr(0, 16);
}


bool de::comm::capIsValidName (const std::string& name)
{
    if (name.empty() || name.size() > CAP_NAME_MAX_LEN) return false;
    if (!(name[0] >= 'a' && name[0] <= 'z')) return false;
    for (const char c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}


bool de::comm::capValidateAdvert (const Json_de& advert, std::string& err)
{
    if (!advert.is_object())
    {
        err = "advert must be an object";
        return false;
    }

    if (!advert.contains("schema") || !advert["schema"].is_string()
        || advert["schema"].get<std::string>() != CAP_SCHEMA_ID)
    {
        err = "missing or wrong 'schema' (expected " CAP_SCHEMA_ID ")";
        return false;
    }

    if (!advert.contains("ns") || !advert["ns"].is_string()
        || !capIsValidName(advert["ns"].get<std::string>()))
    {
        err = "missing or bad 'ns'";
        return false;
    }

    if (!advert.contains("module") || !advert["module"].is_string())
    {
        err = "missing 'module'";
        return false;
    }
    if (!advert.contains("ver") || !advert["ver"].is_string())
    {
        err = "missing 'ver'";
        return false;
    }

    if (advert.contains("actions"))
    {
        const Json_de& actions = advert["actions"];
        if (!actions.is_object())
        {
            err = "'actions' must be an object";
            return false;
        }
        for (auto it = actions.begin(); it != actions.end(); ++it)
        {
            const std::string path = "actions." + it.key();
            if (!capIsValidName(it.key()))
            {
                err = path + ": bad action name";
                return false;
            }
            const Json_de& act = it.value();
            if (!act.is_object())
            {
                err = path + " must be an object";
                return false;
            }
            if (act.contains("desc") && !act["desc"].is_string())
            {
                err = path + ": 'desc' must be a string";
                return false;
            }
            if (act.contains("timeout_s")
                && !(act["timeout_s"].is_number() && act["timeout_s"].get<double>() > 0))
            {
                err = path + ": 'timeout_s' must be a positive number";
                return false;
            }
            if (act.contains("deprecated_alias")
                && !act["deprecated_alias"].is_string())
            {
                err = path + ": 'deprecated_alias' must be a string";
                return false;
            }
            // TASK-PF-01: "claim" narrows a sensor-lane claim from the
            // whole namespace to this action only ("action"); absent or
            // "ns" keeps the namespace-wide behaviour
            if (act.contains("claim")
                && !(act["claim"].is_string()
                     && (act["claim"].get<std::string>() == "ns"
                         || act["claim"].get<std::string>() == "action")))
            {
                err = path + ": 'claim' must be \"ns\" or \"action\"";
                return false;
            }
            if (act.contains("params")
                && !capValidateParamMap(act["params"], path + ".params", err))
                return false;
        }
    }

    if (advert.contains("events"))
    {
        const Json_de& events = advert["events"];
        if (!events.is_object())
        {
            err = "'events' must be an object";
            return false;
        }
        for (auto it = events.begin(); it != events.end(); ++it)
        {
            const std::string path = "events." + it.key();
            if (!capIsValidName(it.key()))
            {
                err = path + ": bad event name";
                return false;
            }
            const Json_de& ev = it.value();
            if (!ev.is_object())
            {
                err = path + " must be an object";
                return false;
            }
            if (ev.contains("payload")
                && !capValidateParamMap(ev["payload"], path + ".payload", err))
                return false;
        }
    }

    if (advert.contains("state")
        && !capValidateParamMap(advert["state"], "state", err))
        return false;

    // TASK-PF-02: the event surfaces of dynamic per-resource names
    // "<ns>.<key>.<verb>". Both keys are additive: pre-PF-02 validators
    // ignore unknown top-level keys, so a PF-02 advert still validates
    // (and is forwarded verbatim) by an old de_comm.

    if (advert.contains("resources"))
    {
        const Json_de& resources = advert["resources"];
        if (!resources.is_object())
        {
            err = "'resources' must be an object";
            return false;
        }
        for (auto it = resources.begin(); it != resources.end(); ++it)
        {
            if (!capIsValidName(it.key()))
            {
                err = "resources: bad resource name '" + it.key() + "'";
                return false;
            }
            if (!it.value().is_string()
                || !capIsValidName(it.value().get<std::string>()))
            {
                err = "resources." + it.key() + ": bad class name";
                return false;
            }
        }
    }

    if (advert.contains("event_patterns"))
    {
        const Json_de& patterns = advert["event_patterns"];
        if (!patterns.is_object())
        {
            err = "'event_patterns' must be an object";
            return false;
        }
        for (auto it = patterns.begin(); it != patterns.end(); ++it)
        {
            const std::string path = "event_patterns." + it.key();
            // keys are exactly "*.<verb>" with <verb> a cap name
            const std::string& key = it.key();
            if (key.size() < 3 || key[0] != '*' || key[1] != '.'
                || !capIsValidName(key.substr(2)))
            {
                err = path + ": pattern keys must be '*.<verb>'";
                return false;
            }
            const Json_de& pat = it.value();
            if (!pat.is_object())
            {
                err = path + " must be an object";
                return false;
            }
            if (pat.contains("kind"))
            {
                const Json_de& kind = pat["kind"];
                if (!kind.is_string()
                    || (kind.get<std::string>() != "fact"
                        && kind.get<std::string>() != "command"))
                {
                    err = path + ": 'kind' must be 'fact' or 'command'";
                    return false;
                }
            }
            if (pat.contains("res"))
            {
                const Json_de& res = pat["res"];
                if (!res.is_array())
                {
                    err = path + ": 'res' must be an array";
                    return false;
                }
                for (const Json_de& cls : res)
                {
                    if (!cls.is_string()
                        || !capIsValidName(cls.get<std::string>()))
                    {
                        err = path + ": 'res' entries must be class names";
                        return false;
                    }
                }
            }
            if (pat.contains("desc") && !pat["desc"].is_string())
            {
                err = path + ": 'desc' must be a string";
                return false;
            }
            if (pat.contains("payload")
                && !capValidateParamMap(pat["payload"], path + ".payload", err))
                return false;
        }
    }

    return true;
}


const Json_de* de::comm::capMatchEventPattern (const Json_de& advert,
                                             const std::string& key,
                                             const std::string& verb,
                                             const std::string& kind)
{
    if (!advert.is_object()) return nullptr;
    const Json_de* patterns = nullptr;
    if (advert.contains("event_patterns")
        && advert["event_patterns"].is_object())
        patterns = &advert["event_patterns"];
    if (patterns == nullptr || !patterns->contains("*." + verb))
        return nullptr;
    const Json_de& pat = (*patterns)["*." + verb];
    if (!pat.is_object()) return nullptr;

    if (pat.value("kind", "fact") != kind) return nullptr;

    // the resource check applies only when the advert lists resources:
    // without it the key cannot be class-checked and the pattern wins
    if (advert.contains("resources"))
    {
        const Json_de& resources = advert["resources"];
        if (!resources.is_object()) return nullptr;
        if (!resources.contains(key) || !resources[key].is_string())
            return nullptr;
        const std::string cls = resources[key].get<std::string>();
        if (pat.contains("res") && pat["res"].is_array())
        {
            bool allowed = false;
            for (const Json_de& c : pat["res"])
            {
                if (c.is_string() && c.get<std::string>() == cls)
                {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) return nullptr;
        }
    }

    return &pat;
}


/**
 * @brief does `value` satisfy a single declared scalar type?
 */
static bool capValueMatches (const Json_de& value, const std::string& type)
{
    if (type == "bool")   return value.is_boolean();
    if (type == "int")    return value.is_number_integer() || value.is_number_unsigned();
    if (type == "number") return value.is_number();
    if (type == "string") return value.is_string();
    if (type == "geo")
    {
        // [lat, lng, alt_m] - three numbers
        return value.is_array() && value.size() == 3
               && value[0].is_number() && value[1].is_number()
               && value[2].is_number();
    }
    if (type == "enum")
    {
        // checked against 'values' by the caller; any scalar allowed here
        return value.is_string() || value.is_number() || value.is_boolean();
    }
    if (type == "array")  return value.is_array();
    if (type == "object") return value.is_object();
    return false;
}


/**
 * @brief check one value against one param schema; appends to `errors`.
 */
static void capCheckValue (const Json_de& param, const Json_de& value,
                           const std::string& path,
                           std::vector<std::string>& errors, int depth)
{
    std::vector<std::string> types;
    if (!capTypeList(param, types) || types.empty())
    {
        errors.push_back(path + ": schema has no usable 'type'");
        return;
    }

    // the limits below belong to the type the value actually matched - for
    // ["string","int"] an int value must still honour min/max
    std::string t;
    for (const std::string& cand : types)
    {
        if (capValueMatches(value, cand)) { t = cand; break; }
    }
    if (t.empty())
    {
        std::string want;
        for (const std::string& c : types) { if (!want.empty()) want += "|"; want += c; }
        errors.push_back(path + ": expected " + want);
        return;
    }

    if ((t == "int" || t == "number"))
    {
        if (param.contains("min") && param["min"].is_number()
            && value.get<double>() < param["min"].get<double>())
            errors.push_back(path + ": must be >= " + param["min"].dump());
        if (param.contains("max") && param["max"].is_number()
            && value.get<double>() > param["max"].get<double>())
            errors.push_back(path + ": must be <= " + param["max"].dump());
    }

    if (t == "enum")
    {
        bool in = false;
        for (const Json_de& v : param.value("values", Json_de::array()))
            in = in || (v == value);
        if (!in) errors.push_back(path + ": not one of the allowed values");
    }

    if (t == "array" && value.is_array() && param.contains("items") && depth < 4)
    {
        for (size_t i = 0; i < value.size(); ++i)
            capCheckValue(param["items"], value[i],
                          path + "[" + std::to_string(i) + "]", errors, depth + 1);
    }

    if (t == "object" && value.is_object() && param.contains("fields") && depth < 4)
    {
        const Json_de& fields = param["fields"];
        for (auto it = fields.begin(); it != fields.end(); ++it)
        {
            if (value.contains(it.key()))
            {
                capCheckValue(it.value(), value[it.key()],
                              path + "." + it.key(), errors, depth + 1);
            }
            else if (!it.value().value("optional", false)
                     && !it.value().contains("default"))
            {
                errors.push_back(path + "." + it.key() + ": missing required field");
            }
        }
    }
}


std::vector<std::string> de::comm::capCheckParams (const Json_de& params_schema,
                                                   const Json_de& with,
                                                   Json_de& filled)
{
    std::vector<std::string> errors;

    filled = with.is_object() ? with : Json_de::object();

    if (params_schema.is_null()) return errors;
    if (!params_schema.is_object())
    {
        errors.push_back("params schema is not an object");
        return errors;
    }

    if (!with.is_null() && !with.is_object())
        errors.push_back("with: expected an object");

    // unknown keys are rejected - a typo'd param silently doing nothing is
    // worse than a hard error at invoke time
    if (with.is_object())
    {
        for (auto it = with.begin(); it != with.end(); ++it)
        {
            if (!params_schema.contains(it.key()))
                errors.push_back(it.key() + ": unknown param");
        }
    }

    for (auto it = params_schema.begin(); it != params_schema.end(); ++it)
    {
        const std::string& name = it.key();
        const Json_de& param = it.value();
        const bool present = with.is_object() && with.contains(name);

        if (!present)
        {
            if (param.contains("default"))
                filled[name] = param["default"];
            else if (!param.value("optional", false))
                errors.push_back(name + ": missing required param");
            continue;
        }

        capCheckValue(param, with[name], name, errors, 0);
    }

    return errors;
}


const Json_de* de::comm::capFindAction (const Json_de& advert, const std::string& act)
{
    if (!advert.is_object() || !advert.contains("actions")
        || !advert["actions"].is_object())
        return nullptr;
    const Json_de& actions = advert["actions"];
    if (actions.contains(act)) return &actions[act];
    // a renamed action keeps answering to its old name (README rule 2)
    for (auto it = actions.begin(); it != actions.end(); ++it)
    {
        if (it.value().is_object()
            && it.value().value("deprecated_alias", "") == act)
            return &it.value();
    }
    return nullptr;
}


std::string de::comm::capCanonicalAction (const Json_de& advert, const std::string& act)
{
    if (!advert.is_object() || !advert.contains("actions")
        || !advert["actions"].is_object())
        return std::string();
    const Json_de& actions = advert["actions"];
    if (actions.contains(act)) return act;
    for (auto it = actions.begin(); it != actions.end(); ++it)
    {
        if (it.value().is_object()
            && it.value().value("deprecated_alias", "") == act)
            return it.key();
    }
    return std::string();
}
