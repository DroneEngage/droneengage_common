#include "messages.hpp"
#include "../helpers/helpers.hpp"
#include "../helpers/colors.hpp"

#include "./configFile.hpp"
#include "./localConfigFile.hpp"

#include "de_message_parser_base.hpp"

using namespace de::comm;

void CAndruavMessageParserBase::parseMessage(Json_de &andruav_message, const char *full_message, const int &full_message_length)
{
    const int messageType = andruav_message[ANDRUAV_PROTOCOL_MESSAGE_TYPE].get<int>();
    m_is_binary = !(full_message[full_message_length - 1] == 125 || (full_message[full_message_length - 2] == 125)); // "}".charCodeAt(0)  IS TEXT / BINARY Msg

    uint32_t permission = 0;
    if (validateField(andruav_message, ANDRUAV_PROTOCOL_MESSAGE_PERMISSION, Json_de::value_t::number_unsigned))
    {
        permission = andruav_message[ANDRUAV_PROTOCOL_MESSAGE_PERMISSION].get<int>();
    }

    m_is_system = false;
    if ((validateField(andruav_message, ANDRUAV_PROTOCOL_SENDER, Json_de::value_t::string)) && (andruav_message[ANDRUAV_PROTOCOL_SENDER].get<std::string>().compare(ANDRUAV_PROTOCOL_SENDER_COMM_SERVER) == 0))
    {
        m_is_system = true;
    }

    m_is_inter_module = false;
    if ((validateField(andruav_message, INTERMODULE_ROUTING_TYPE, Json_de::value_t::string)) && (andruav_message[INTERMODULE_ROUTING_TYPE].get<std::string>().compare(CMD_TYPE_INTERMODULE) == 0))
    {
        m_is_inter_module = true;
    }

    if (messageType == TYPE_AndruavMessage_RemoteExecute)
    {
        parseRemoteExecute(andruav_message);
        return;
    }

    parseDefaultCommand(andruav_message, full_message, full_message_length, messageType, permission);
    parseCommand(andruav_message, full_message, full_message_length, messageType, permission);
}

void CAndruavMessageParserBase::parseDefaultCommand(Json_de &andruav_message, const char *full_message, const int &full_message_length, int messageType, uint32_t permission)
{
    const Json_de cmd = andruav_message[ANDRUAV_PROTOCOL_MESSAGE_CMD];

    switch (messageType)
    {
    case TYPE_AndruavMessage_CONFIG_ACTION:
    {
        handleConfigAction(andruav_message, cmd);
    }
    break;
    }
}

void CAndruavMessageParserBase::handleConfigAction(Json_de &andruav_message, const Json_de &cmd)
{
    std::string module_key = "";
    if (!validateField(cmd, "a", Json_de::value_t::number_unsigned))
        return;
    if (validateField(cmd, "b", Json_de::value_t::string))
    {
        module_key = de::comm::CModule::getInstance().getModuleKey();
        if (module_key != cmd["b"].get<std::string>())
        {
            return;
        }
    }
    int action = cmd["a"].get<int>();
    switch (action)
    {
    case CONFIG_ACTION_Restart:
        exit(0);
        break;
    case CONFIG_ACTION_APPLY_CONFIG:
    {
        Json_de config = cmd["c"];
        std::cout << config << std::endl;
        de::CConfigFile &cConfigFile = de::CConfigFile::getInstance();
        cConfigFile.updateJSON(config.dump());
    }
    break;
    case CONFIG_REQUEST_FETCH_CONFIG_TEMPLATE:
    {
        if (!andruav_message.contains(ANDRUAV_PROTOCOL_SENDER))
            return;
        std::string sender = andruav_message[ANDRUAV_PROTOCOL_SENDER].get<std::string>();
#ifdef DEBUG
        std::cout << std::endl
                  << _INFO_CONSOLE_TEXT << "CONFIG_REQUEST_FETCH_CONFIG_TEMPLATE" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
        std::ifstream file("template.json");
        if (!file.is_open())
        {
            std::cout << std::endl
                      << _ERROR_CONSOLE_BOLD_TEXT_ << "cannot read template.json" << _NORMAL_CONSOLE_TEXT_ << std::endl;
            de::comm::CFacade_Base::getInstance().sendErrorMessage(std::string(), 0, ERROR_3DR, NOTIFICATION_TYPE_ERROR, "cannot read template.json");
            Json_de empty_file_content_json = {};
            de::comm::CFacade_Base::getInstance().API_sendConfigTemplate(sender, module_key, empty_file_content_json, true);
            return;
        }
        std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        Json_de file_content_json = Json_de::parse(file_content);
        de::comm::CFacade_Base::getInstance().API_sendConfigTemplate(sender, module_key, file_content_json, true);
    }
    break;
    case CONFIG_REQUEST_FETCH_CONFIG:
        break;
    default:
        break;
    }
}