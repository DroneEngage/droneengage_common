#ifndef P2P_ESP32_FACADE_BASE_H_
#define P2P_ESP32_FACADE_BASE_H_


#include <iostream>

#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

#include "messages.hpp"
#include "de_module.hpp"

namespace de
{
namespace comm
{
    class CFacade_Base
    {
        public:

            void requestID(const std::string&target_party_id) const;
            void sendErrorMessage (const std::string&target_party_id, const int& error_number, const int& info_type, const int& notification_type, const std::string& description) const;

            void API_sendConfigTemplate(const std::string&  target_party_id, const std::string& module_key, const Json_de& json_file_content_json, const bool reply);
  
        
        protected:

            CModule &m_module = de::comm::CModule::getInstance();            
    };
}
}
#endif