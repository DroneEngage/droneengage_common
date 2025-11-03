#ifndef ANDRUAV_MESSAGE_PARSER_BASE_H_
#define ANDRUAV_MESSAGE_PARSER_BASE_H_

#include "de_message_parser_base.hpp"
#include "de_facade_base.hpp"

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

        private:
            void parseDefaultCommand(Json_de &andruav_message, const char *full_message, const int &full_message_length, int messageType, uint32_t permission);
            void handleConfigAction(Json_de &andruav_message, const Json_de &cmd);

        protected:
            bool m_is_binary;
            bool m_is_system;
            bool m_is_inter_module;

            de::comm::CFacade_Base &m_facade = de::comm::CFacade_Base::getInstance();
        };
    }
}

#endif