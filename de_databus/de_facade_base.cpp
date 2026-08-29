#include <iostream>
#include <string>

#include "../helpers/colors.hpp"
#include "de_facade_base.hpp"
#include "de_module_health.hpp"




using namespace de::comm;



/**
 * @brief ORIGINAL
 * 
 * @param target_party_id 
 */
void CFacade_Base::requestID(const std::string&target_party_id) const
{
    
    Json_de message = 
        {
            {"C", TYPE_AndruavMessage_ID}
        };
        

    m_module.sendJMSG (target_party_id, message, TYPE_AndruavMessage_RemoteExecute, true);
    
    return ;
}


void CFacade_Base::sendErrorMessage (const std::string&target_party_id, const int& error_number, const int& info_type, const int& notification_type, const std::string& description)  const
{
    /*
        EN : error number  "not currently processed".
        IT : info type indicate what component is reporting the error.
        NT : sevirity and com,pliant with ardupilot.
        DS : description message.
    */
    Json_de message =
        {
            {"EN", error_number},
            {"IT", info_type},
            {"NT", notification_type},
            {"DS", description}
        };

    m_module.sendJMSG (target_party_id, message, TYPE_AndruavMessage_Error, false);
    
    std::cout << std::endl << _SUCCESS_CONSOLE_BOLD_TEXT_ << " -- sendErrorMessage " << _NORMAL_CONSOLE_TEXT_ << description << std::endl;
    
    return ;
}


void CFacade_Base::configureMemoryStatus(const double max_rss_mb, const double max_growth_mb_per_hour) const
{
    CModuleHealthMonitor::getInstance().configure(max_rss_mb, max_growth_mb_per_hour);
}


void CFacade_Base::sendMemoryStatus(const std::string& target_party_id) const
{
    const MODULE_HEALTH_SAMPLE& health = CModuleHealthMonitor::getInstance().sample();
    if (!health.valid) return; // /proc/self/status unavailable

    Json_de message =
        {
            {"a",  MODULE_HEALTH_ACTION_STATUS},
            {"k",  m_module.getModuleKey()},
            {"rs", health.rss_mb},
            {"pk", health.vmpeak_mb},
            {"sw", health.vmswap_mb},
            {"th", health.threads},
            {"sl", health.slope_mb_h},
            {"tr", health.trend},
            {"hs", health.status},
            {"up", health.uptime_sec}
        };

    m_module.sendJMSG (target_party_id, message, TYPE_AndruavMessage_MODULE_HEALTH_STATUS, false);

    #ifdef DDEBUG
        std::cout << "sendMemoryStatus:" << message.dump() << std::endl;
    #endif

    return ;
}


void CFacade_Base::API_sendConfigTemplate(const std::string& target_party_id, const std::string& module_key, const Json_de& json_file_content_json, const bool reply)
{
   // Create JSON message
    Json_de message = {
        {"a", CONFIG_STATUS_FETCH_CONFIG_TEMPLATE},
        {"b", json_file_content_json},
        {"k", module_key},
        {"R", reply}
    };

    // Send command
    m_module.sendJMSG (target_party_id, message, TYPE_AndruavMessage_CONFIG_STATUS, false);

#ifdef DEBUG
    std::cout << std::endl << _INFO_CONSOLE_TEXT << "API_sendConfigTemplate: module_key:" << module_key << _NORMAL_CONSOLE_TEXT_ << std::endl;
    std::cout << std::endl << _INFO_CONSOLE_TEXT << "API_sendConfigTemplate: json_file_content_json:" << json_file_content_json.dump() << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif    

    return ;
}