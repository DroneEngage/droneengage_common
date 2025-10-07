#include "messages.hpp"
#include "../helpers/helpers.hpp"
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

    parseCommand(andruav_message, full_message, full_message_length, messageType, permission);
}