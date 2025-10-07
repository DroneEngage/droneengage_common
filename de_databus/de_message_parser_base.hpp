#ifndef ANDRUAV_MESSAGE_PARSER_BASE_H_
#define ANDRUAV_MESSAGE_PARSER_BASE_H_

#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

namespace de
{
namespace comm
{
    class CAndruavMessageParserBase
    {
    public:
        virtual ~CAndruavMessageParserBase() {}

        void parseMessage(Json_de &andruav_message, const char *full_message, const int &full_message_length);
        
    protected:
    
        virtual void parseRemoteExecute(Json_de &andruav_message) = 0;
        virtual void parseCommand(Json_de &andruav_message, const char *full_message, const int &full_message_length, int messageType, uint32_t permission) = 0;

    
    protected:

        bool m_is_binary;
        bool m_is_system;
        bool m_is_inter_module;

    };
}
}

#endif