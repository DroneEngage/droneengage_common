#include <chrono>
#include <future>
#include <unistd.h>

#include "../helpers/colors.hpp"
#include "../helpers/helpers.hpp"
#include "de_module.hpp"
#include "de_facade_base.hpp"
#include "cap_schema.hpp"
#include "configFile.hpp"







void de::comm::CModule::defineModule (
                 std::string module_class,
                 std::string module_id,
                 std::string module_key,
                 std::string module_version,
                 Json_de message_filter
            ) 
{
    m_module_class = module_class;
    m_module_id = module_id;
    m_module_key = module_key;
    m_module_version = module_version;
    m_message_filter = message_filter;
    return ;
}


bool de::comm::CModule::init (const std::string targetIP, int broadcatsPort, const std::string host, int listenningPort,  int chunkSize, bool use_unix_socket)
{
    // Auto-detect chunk size when not explicitly configured (chunkSize <= 0).
    // Modules pass 0 when s2s_udp_packet_size is missing from their config.
    // Loopback (MTU 65536) can use the full default; non-loopback targets
    // are capped at SAFE_REMOTE_UDP_PAYLOAD to avoid IP fragmentation.
    // If the user explicitly sets s2s_udp_packet_size, that value is used as-is.
    if (chunkSize <= 0)
    {
        if (isLocalhost(targetIP)) {
            chunkSize = DEFAULT_UDP_DATABUS_PACKET_SIZE;
        } else {
            chunkSize = SAFE_REMOTE_UDP_PAYLOAD;
        }
        std::cout << _INFO_CONSOLE_TEXT << "Auto chunk size: " << chunkSize
                  << " (target " << targetIP << ")" << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }

    // Determine transport type based on config and target IP
    m_use_unix_socket = use_unix_socket && isLocalhost(targetIP);
    
    if (m_use_unix_socket) {
        // Unix domain socket for localhost communication.
        // Base dir is resolved at runtime: /run/de_comm (root, secure) or
        // /tmp/de_comm (non-root fallback). See helpers::getUnixSocketBaseDir().
        std::string baseDir = getUnixSocketBaseDir();
        std::string brokerSocketPath = baseDir + "/de_comm_broker.sock";
        std::string ownSocketPath = baseDir + "/de_comm_" + m_module_id + "_" + m_module_key + ".sock";
        
#ifdef DEBUG
        std::cout << _INFO_CONSOLE_TEXT << "CModule::init - Using Unix socket. broker:" << brokerSocketPath << " own:" << ownSocketPath << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
        cUnixDgramClient.init(brokerSocketPath.c_str(), ownSocketPath.c_str(), chunkSize);
        createJSONID(true);
        cUnixDgramClient.start();
    } else {
        // UDP for cross-board or when Unix socket is disabled
#ifdef DEBUG
        std::cout << _INFO_CONSOLE_TEXT << "CModule::init - Using UDP. targetIP:" << targetIP << " port:" << broadcatsPort << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
        cUDPClient.init(targetIP.c_str(), broadcatsPort, host.c_str() ,listenningPort, chunkSize);
        createJSONID(true);
        cUDPClient.start();
    }

    // TASK-31 (GL-0038): board CPU reporting thresholds come from the
    // optional "cpu_monitor" object in this module's own config (absent
    // -> defaults; enabled=false sends nothing).
    m_cpu_monitor_config = deParseCpuMonitorConfig(de::CConfigFile::getInstance().GetConfigJSON());
    m_cpu_monitor.configure(m_cpu_monitor_config);
    m_board_id = deBoardId();
    m_clock_ticks_per_s = sysconf(_SC_CLK_TCK);
    if (m_clock_ticks_per_s <= 0) m_clock_ticks_per_s = 100;
    m_have_prev_proc        = false;
    m_last_cpu_report_ms    = 0;

    m_health_thread_exit = false;
    m_health_thread = std::thread(&CModule::healthHeartbeatLoop, this);

    m_initialized = true;

    return true;
}


bool de::comm::CModule::uninit ()
{
    // Stop the transport first so any thread blocked in sendMSG (ENOENT
    // backoff, chunk sends) aborts immediately instead of stalling joins.
    if (m_use_unix_socket) {
        cUnixDgramClient.stop();
    } else {
        cUDPClient.stop();
    }

    m_health_thread_exit = true;
    if (m_health_thread.joinable()) m_health_thread.join();

    return true;
}


/**
 * @brief Schedules this module's self-reported memory health every
 * MODULE_HEALTH_INTERVAL_SEC. Runs for the lifetime of the module (started
 * by init(), stopped by uninit()). CModule only owns the timing; the actual
 * message content is CFacade_Base::sendMemoryStatus()'s responsibility, same
 * as every other message type (CModule stays a dumb transport).
 */
void de::comm::CModule::healthHeartbeatLoop ()
{
    static const int MODULE_HEALTH_INTERVAL_SEC = 30;

    int tick = 0;
    while (!m_health_thread_exit.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // TASK-31 (GL-0038): board CPU sample every second; the report
        // goes to de_comm on 6547 (transition edge + every report_s).
        if (m_cpu_monitor_config.enabled)
        {
            const uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            cpuReportTick(now_ms);
        }

        if (++tick >= MODULE_HEALTH_INTERVAL_SEC)
        {
            tick = 0;
            CFacade_Base::getInstance().sendMemoryStatus("");
        }
    }
}


/**
 * @brief one 1 Hz CPU sample on the health thread: feed the board
 * monitor, refresh the process CPU delta, then report to de_comm on a
 * transition edge (ev = BECAME_*) or every report_s (ev = PERIODIC).
 * A transition report resets the periodic clock so the two never emit
 * back to back.
 */
void de::comm::CModule::cpuReportTick (const uint64_t now_ms)
{
    CpuTimes cpu_times;
    if (!deReadProcStatCpu(cpu_times)) return;

    const CpuTransition transition = m_cpu_monitor.updateFromTimes(cpu_times, now_ms);
    const double usage = m_cpu_monitor.lastUsagePercent();
    if (usage < 0.0) return;   // first sample only seeds the baseline

    int process_usage = 0;
    ProcCpuTimes proc_times;
    if (deReadProcSelfStatCpu(proc_times))
    {
        if (m_have_prev_proc && (now_ms > m_prev_proc_ms))
        {
            const double proc_pct = deProcessCpuUsagePercent(m_prev_proc_times, proc_times,
                                                             now_ms - m_prev_proc_ms,
                                                             m_clock_ticks_per_s);
            if (proc_pct >= 0.0) process_usage = static_cast<int>(proc_pct + 0.5);
        }
        m_prev_proc_times = proc_times;
        m_prev_proc_ms    = now_ms;
        m_have_prev_proc  = true;
    }

    int report_event = -1;
    if (transition == CpuTransition::BECAME_SUPER_BUSY)
    {
        report_event = CPU_REPORT_EVENT_BECAME_SUPER_BUSY;
    }
    else if (transition == CpuTransition::BECAME_FREE)
    {
        report_event = CPU_REPORT_EVENT_BECAME_FREE;
    }
    else if ((m_last_cpu_report_ms == 0)
             || ((now_ms >= m_last_cpu_report_ms)
                 && ((now_ms - m_last_cpu_report_ms)
                     >= static_cast<uint64_t>(m_cpu_monitor_config.report_s) * 1000ULL)))
    {
        report_event = CPU_REPORT_EVENT_PERIODIC;
    }

    if (report_event < 0) return;

    m_last_cpu_report_ms = now_ms;
    CFacade_Base::getInstance().sendCpuStatus(
        m_board_id,
        static_cast<int>(usage + 0.5),
        process_usage,
        (m_cpu_monitor.state() == CpuBusyState::SUPER_BUSY) ? CPU_BOARD_STATE_SUPER_BUSY
                                                            : CPU_BOARD_STATE_FREE,
        report_event);
}


void de::comm::CModule::sendSYSMSG (const Json_de& jmsg, const int& andruav_message_id)
{
    Json_de fullMessage;

    fullMessage[ANDRUAV_PROTOCOL_TARGET_ID]         = ANDRUAV_PROTOCOL_SENDER_COMM_SERVER; 
    fullMessage[INTERMODULE_ROUTING_TYPE]           = CMD_COMM_SYSTEM;
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_TYPE]      = andruav_message_id;
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_CMD]       = jmsg;
    
    const std::string& msg = fullMessage.dump();
    #ifdef DEBUG
        //std::cout << "sendJMSG:" << msg.c_str() << std::endl;
    #endif
    sendMSG(msg.c_str(), msg.length());         
}


/**
 * @brief sends JSON packet
 * @details sends JSON packet.
 * 
 * 
 * @param targetPartyID 
 * @param jmsg 
 * @param andruav_message_id 
 * @param internal_message if true @link INTERMODULE_MODULE_KEY @endlink equaqls to Module key
 */
void de::comm::CModule::sendJMSG (const std::string targetPartyID, const Json_de jmsg, const int andruav_message_id, const bool internal_message)
{
    std::lock_guard<std::mutex> lock(m_lock);
                
    Json_de fullMessage;

    /**
    // Route messages:
    //  Internally: i.e. DroneEngage Communication module will handle it and will resend it to other modules
    //                  or modulated then forwarded to Cmmunication Server.
    //  Group: i.e. to all members of groups.
    //  Individual: i.e. to a given member or a certain type of members i.e. all vehicles or all GCS.
    */
    std::string msg_routing_type = CMD_COMM_GROUP;
    if (internal_message == true)
    {
        msg_routing_type = CMD_TYPE_INTERMODULE;
    }
    else
    {
        if (targetPartyID.length() != 0 )
        {
            msg_routing_type = CMD_COMM_INDIVIDUAL;
        }
    }
        
    fullMessage[INTERMODULE_MODULE_KEY]             = m_module_key;
    fullMessage[ANDRUAV_PROTOCOL_TARGET_ID]         = targetPartyID; // targetID can exist even if routing is intermodule
    fullMessage[INTERMODULE_ROUTING_TYPE]           = std::string(msg_routing_type);
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_TYPE]      = andruav_message_id;
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_CMD]       = jmsg;
    const std::string& msg = fullMessage.dump();
    #ifdef DDEBUG
        std::cout << "sendJMSG:" << msg.c_str() << std::endl;
    #endif
    sendMSG(msg.c_str(), msg.length());
}


/**
 * @brief sends binary packet
 * @details sends binary packet.
 * Binary packet always has JSON header then 0 then binary data.
 * 
 * @param targetPartyID 
 * @param bmsg 
 * @param andruav_message_id 
 * @param internal_message if true @link INTERMODULE_MODULE_KEY @endlink equaqls to Module key
 * @param message_cmd JSON message in ms section of JSON header. if null then pass Json_de()
 */
void de::comm::CModule::sendBMSG (const std::string& targetPartyID, const char * bmsg, const int bmsg_length, const int& andruav_message_id, const bool& internal_message, const Json_de& message_cmd)
{
    std::lock_guard<std::mutex> lock(m_lock);
                
    Json_de fullMessage;

    std::string msg_routing_type = CMD_COMM_GROUP;
    if (internal_message == true)
    {
        msg_routing_type = CMD_TYPE_INTERMODULE;
        
    }
    else
    {
        if (targetPartyID.length() != 0 )
        {
                    
            msg_routing_type = CMD_COMM_INDIVIDUAL;
        }
        
    }
        
    fullMessage[INTERMODULE_MODULE_KEY]             = m_module_key;    
    fullMessage[ANDRUAV_PROTOCOL_TARGET_ID]         = targetPartyID; // targetID can exist even if routing is intermodule
    fullMessage[INTERMODULE_ROUTING_TYPE]           = std::string(msg_routing_type);
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_TYPE]      = andruav_message_id;
    fullMessage[ANDRUAV_PROTOCOL_MESSAGE_CMD]       = message_cmd;

    std::string json_msg = fullMessage.dump();
    
    /**** Attach Binary part to String after inserting NULL ***/

    // Prepare a vector for the whole message
    std::vector<char> msg(json_msg.begin(), json_msg.end());
    msg.push_back('\0'); // Add null terminator

    // Append binary message
    if (bmsg_length != 0)
    {
        msg.insert(msg.end(), bmsg, bmsg + bmsg_length);
    }

    // Access the complete message as a char array
    char* msg_ptr = msg.data();

    /**** Attachment End ****/


    sendMSG(msg_ptr, json_msg.length()+1+bmsg_length);

    return ;
}


/**
* @brief similar to Remote execute command but between modules.
* 
* @param command_type 
* @return const Json_de 
*/
void de::comm::CModule::sendMREMSG(const int& command_type)
{
    std::lock_guard<std::mutex> lock(m_lock);
                
    Json_de json_msg;        
        
    json_msg[INTERMODULE_MODULE_KEY]        = m_module_key;
    json_msg[INTERMODULE_ROUTING_TYPE]      =  CMD_TYPE_INTERMODULE;
    json_msg[ANDRUAV_PROTOCOL_MESSAGE_TYPE] =  TYPE_AndruavModule_RemoteExecute;
    

    Json_de ms;
    ms["C"] = command_type;
    json_msg[ANDRUAV_PROTOCOL_MESSAGE_CMD]          = ms;
    
    
    const std::string msg = json_msg.dump();
    sendMSG(msg.c_str(), msg.length());         
}


/**
 * @brief forward a message received from another channel.
 * example: P@P module receives a messages from telemetry and wants to forward it on DE databus.
 * 
 * @param message 
 * @param datalength 
 */
void de::comm::CModule::forwardMSG (const char * message, const std::size_t datalength)
{
    sendMSG(message, datalength);
}


void de::comm::CModule::onReceive (const char * message, int len)
{
    static bool bFirstReceived = false;
        
    #ifdef DDEBUG        
        std::cout << _INFO_CONSOLE_TEXT << "RX MSG: :len " << std::to_string(len) << ":" << message <<   _NORMAL_CONSOLE_TEXT_ << std::endl;
    #endif
#ifndef DE_DISABLE_TRY    
    try
    {
#endif        
        /* code */
        Json_de jMsg = Json_de::parse(message);
        
        #ifdef DDEBUG
        std::cout << _INFO_CONSOLE_TEXT << "RX MSG: jMsg" << jMsg.dump() <<   _NORMAL_CONSOLE_TEXT_ << std::endl;
        #endif

        if (!jMsg.contains(ANDRUAV_PROTOCOL_MESSAGE_TYPE)) return ;
        
        if (!jMsg.contains(INTERMODULE_ROUTING_TYPE)) return ;
        
        
        if (std::strcmp(jMsg[INTERMODULE_ROUTING_TYPE].get<std::string>().c_str(),CMD_TYPE_INTERMODULE)==0)
        {
            if (!jMsg.contains(ANDRUAV_PROTOCOL_MESSAGE_CMD)) return ;
            
            const Json_de cmd = jMsg[ANDRUAV_PROTOCOL_MESSAGE_CMD];
            

            const int messageType = jMsg[ANDRUAV_PROTOCOL_MESSAGE_TYPE].get<int>();
            switch (messageType)
            {
            case TYPE_AndruavModule_ID:
                {
                    if (!cmd.contains(JSON_INTERMODULE_PARTY_RECORD)) return ;
                    
                    const Json_de unit_ids = cmd [JSON_INTERMODULE_PARTY_RECORD];
                    if (!unit_ids.contains(ANDRUAV_PROTOCOL_SENDER)) return ;
                    if (!unit_ids.contains(ANDRUAV_PROTOCOL_GROUP_ID)) return ;
            
                    m_party_id = std::string(unit_ids[ANDRUAV_PROTOCOL_SENDER].get<std::string>());
                    m_group_id = std::string(unit_ids[ANDRUAV_PROTOCOL_GROUP_ID].get<std::string>());
                    
                    if (!bFirstReceived)
                    {
                        // tell server you dont need to send ID again.
                        std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << " ** Communicator Server Found" << _SUCCESS_CONSOLE_TEXT_ << ": m_party_id(" << _INFO_CONSOLE_TEXT << m_party_id << _SUCCESS_CONSOLE_TEXT_ << ") m_group_id(" << _INFO_CONSOLE_TEXT << m_group_id << _SUCCESS_CONSOLE_TEXT_ << ")" <<  _NORMAL_CONSOLE_TEXT_ << std::endl;
                        createJSONID(false);
                        bFirstReceived = true;
                        // Phase-3: publish the advert once on registration
                        if (!m_capabilities_sent && !m_capability_adverts.empty())
                        {
                            m_capabilities_sent = true;
                            sendCapabilities();
                        }
                    }

                    if (m_OnReceive!= nullptr) m_OnReceive(message, len, jMsg);

                    return ;
                }
                break;

            case TYPE_AndruavMessage_MODULE_CAPABILITIES:
                {
                    // de_comm asking for the advert (P3-01). Helper-owned:
                    // not forwarded to the module's m_OnReceive.
                    if (cmd.value("r", false)) sendCapabilities();
                    return ;
                }

            case TYPE_AndruavMessage_CAPABILITY_INVOKE:
                {
                    handleCapabilityInvoke(cmd);
                    return ;
                }

            case TYPE_AndruavMessage_DUMMY:
                {
                    std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << " TYPE_AndruavMessage_DUMMY" << _SUCCESS_CONSOLE_TEXT_ << message <<  _NORMAL_CONSOLE_TEXT_ << std::endl;

                }
                break;

                default:
                    break;
            }
            
            

            
        }

        if (m_OnReceive!= nullptr) m_OnReceive(message, len, jMsg);
#ifndef DE_DISABLE_TRY
    }
    catch(const std::exception& e)
    {
        std::cout << "ERROR:" << e.what() << std::endl ;
    }
#endif
}


void de::comm::CModule::appendExtraField(const std::string name, const Json_de& ms)
{
    // Add the provided ms object as an entry to m_stdinValues
    m_stdinValues[name] = ms;
}


/**
 * @brief creates JSON message that identifies Module
 * @details generates JSON message that identifies module
 * 'a': module_id
 * 'b': module_class. fixed "fcb"
 * 'c': module_messages. can be updated from config file.
 * 'd': module_features. fixed per module. [T,R]
 * 'e': module_key. uniqueley identifies this instance and can be set in config file.
 * 's': hardware_serial. 
 * 't': hardware_type. 
 * 'z': resend request flag
 * @param reSend if true then server should reply with server json_msg
 * @return 
 */
void de::comm::CModule::createJSONID (bool reSend)
{
        Json_de json_msg;        
        
        json_msg[INTERMODULE_ROUTING_TYPE] =  CMD_TYPE_INTERMODULE;
        json_msg[ANDRUAV_PROTOCOL_MESSAGE_TYPE] =  TYPE_AndruavModule_ID;
        Json_de ms;
              
        ms[JSON_INTERMODULE_MODULE_ID]              = m_module_id;
        ms[JSON_INTERMODULE_MODULE_CLASS]           = m_module_class;
        ms[JSON_INTERMODULE_MODULE_MESSAGES_LIST]   = m_message_filter;
        ms[JSON_INTERMODULE_MODULE_FEATURES]        = m_module_features;
        ms[JSON_INTERMODULE_MODULE_KEY]             = m_module_key; 
        ms[JSON_INTERMODULE_HARDWARE_ID]            = m_hardware_serial; 
        ms[JSON_INTERMODULE_HARDWARE_TYPE]          = m_hardware_serial_type; 
        ms[JSON_INTERMODULE_VERSION]                = m_module_version;
        ms[JSON_INTERMODULE_RESEND]                 = reSend;
        ms[JSON_INTERMODULE_TIMESTAMP_INSTANCE]     = m_instance_time_stamp;

        // Add fields from m_stdinValues to ms
        for (const std::pair<std::string, Json_de>&  entry : m_stdinValues) {
            const std::string& key = entry.first;
            const Json_de& value = entry.second;
            ms[key] = value;
        }

        json_msg[ANDRUAV_PROTOCOL_MESSAGE_CMD] = ms;

        #ifdef DEBUG
            //std::cout << json_msg.dump(4) << std::endl;
        #endif

        if (m_use_unix_socket) {
            cUnixDgramClient.setJsonId(json_msg.dump());
        } else {
            cUDPClient.setJsonId(json_msg.dump());
        }

        return ;
}


/**
 * @brief push a message type into the module filter (idempotent).
 */
void de::comm::CModule::capAddToFilter (const int msg_id)
{
    if (!m_message_filter.is_array()) m_message_filter = Json_de::array();
    for (const Json_de& e : m_message_filter)
    {
        if (e.is_number() && e.get<int>() == msg_id) return;
    }
    m_message_filter.push_back(msg_id);
}


bool de::comm::CModule::setCapabilities (const std::vector<std::string>& adverts)
{
    Json_de parsed = Json_de::array();
    for (const std::string& advert_str : adverts)
    {
        if (advert_str.size() > CAP_ADVERT_MAX_BYTES)
        {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << " setCapabilities: advert exceeds "
                      << CAP_ADVERT_MAX_BYTES << " bytes" << _NORMAL_CONSOLE_TEXT_ << std::endl;
            return false;
        }

#ifndef DE_DISABLE_TRY
        Json_de advert;
        try
        {
            advert = Json_de::parse(advert_str);
        }
        catch (const std::exception& e)
        {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << " setCapabilities: advert is not valid JSON: "
                      << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
            return false;
        }
#else
        const Json_de advert = Json_de::parse(advert_str, nullptr, false);
        if (advert.is_discarded())
        {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << " setCapabilities: advert is not valid JSON"
                      << _NORMAL_CONSOLE_TEXT_ << std::endl;
            return false;
        }
#endif

        std::string err;
        if (!capValidateAdvert(advert, err))
        {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << " setCapabilities: invalid advert: "
                      << err << _NORMAL_CONSOLE_TEXT_ << std::endl;
            return false;
        }
        parsed.push_back(advert);
    }

    {
        std::lock_guard<std::mutex> lock(m_cap_lock);
        m_capability_adverts = adverts;
        m_capability_parsed  = parsed;
        m_capability_hash    = capAdvertHash(adverts);
    }

    // the ID message only carries the hash; the full adverts go out on 6542
    appendExtraField("ch", m_capability_hash);

    // the helper answers capability requests and invokes itself
    capAddToFilter(TYPE_AndruavMessage_MODULE_CAPABILITIES);
    capAddToFilter(TYPE_AndruavMessage_CAPABILITY_INVOKE);

    // refresh the cached ID message when init() already ran
    if (m_initialized) createJSONID(false);

    // registered already (party known)? send the advert now - the normal
    // path sends it once when the first ID reply arrives
    if (m_initialized && !m_party_id.empty() && !m_capabilities_sent)
    {
        m_capabilities_sent = true;
        sendCapabilities();
    }

    return true;
}


void de::comm::CModule::onInvoke (CapabilityInvokeHandler handler)
{
    std::lock_guard<std::mutex> lock(m_cap_lock);
    m_onInvoke = handler;
}


void de::comm::CModule::publishState (const std::string& ns, const Json_de& changed, const bool full)
{
    Json_de msg =
    {
        {"ns", ns},
        {"s",  changed}
    };
    if (full) msg["full"] = true;

    sendJMSG("", msg, TYPE_AndruavMessage_MODULE_STATE, true);
}


std::string de::comm::CModule::fireEvent (const std::string& ns, const std::string& ev, const Json_de& payload)
{
    const uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const std::string nonce = std::to_string(ms) + "-" + std::to_string(++m_event_counter);

    Json_de msg =
    {
        {"d",  ns + "." + ev},
        {"ed", payload},
        {"n",  nonce}
    };

    sendJMSG("", msg, TYPE_AndruavMessage_Sync_EventFire, true);
    return nonce;
}


void de::comm::CModule::sendCapabilities ()
{
    Json_de caps = Json_de::array();
    std::string ch;
    {
        std::lock_guard<std::mutex> lock(m_cap_lock);
        for (const std::string& a : m_capability_adverts) caps.push_back(a);
        ch = m_capability_hash;
    }

    Json_de msg =
    {
        {"caps", caps},
        {"ch",   ch}
    };

    sendJMSG("", msg, TYPE_AndruavMessage_MODULE_CAPABILITIES, true);
}


void de::comm::CModule::sendInvokeResult (const Json_de& result)
{
    sendJMSG("", result, TYPE_AndruavMessage_CAPABILITY_RESULT, true);
}


void de::comm::CModule::cacheInvokeResult (const std::string& id, const Json_de& result)
{
    std::lock_guard<std::mutex> lock(m_cap_lock);
    if (!m_invoke_results.count(id)) m_invoke_order.push_back(id);
    m_invoke_results[id] = result;
    while (m_invoke_order.size() > 64)
    {
        m_invoke_results.erase(m_invoke_order.front());
        m_invoke_order.pop_front();
    }
}


/**
 * @brief one manager thread per invoke: the handler runs inside a
 * std::async worker so the deadline is enforced without touching the
 * receive thread. A handler that outlives its deadline keeps running;
 * its late result is dropped and logged.
 */
void de::comm::CModule::runInvokeOnWorker (const std::string& id, const std::string& ns,
                                           const std::string& act, const Json_de& params,
                                           const double deadline_s)
{
    CapabilityInvokeHandler handler;
    {
        std::lock_guard<std::mutex> lock(m_cap_lock);
        handler = m_onInvoke;
    }

    std::thread([this, id, ns, act, params, deadline_s, handler]()
    {
        std::future<Json_de> fut = std::async(std::launch::async,
            [handler, id, ns, act, params]() -> Json_de
            {
                Json_de res = {{"ok", true}};
                std::string err;
#ifndef DE_DISABLE_TRY
                try
                {
#endif
                    const Json_de data = handler(id, ns, act, params, err);
                    if (!err.empty())
                    {
                        res["ok"]  = false;
                        res["err"] = err;
                    }
                    else
                    {
                        res["data"] = data;
                    }
#ifndef DE_DISABLE_TRY
                }
                catch (const std::exception& e)
                {
                    res["ok"]  = false;
                    res["err"] = std::string("handler exception: ") + e.what();
                }
#endif
                return res;
            });

        if (fut.wait_for(std::chrono::milliseconds((long)(deadline_s * 1000.0)))
                == std::future_status::ready)
        {
            Json_de result = fut.get();
            result["id"] = id;
            {
                std::lock_guard<std::mutex> lock(m_cap_lock);
                m_invoke_inflight.erase(id);
            }
            cacheInvokeResult(id, result);
            sendInvokeResult(result);
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(m_cap_lock);
                m_invoke_inflight.erase(id);
            }
            Json_de result = {{"id", id}, {"ok", false}, {"err", "timeout"}};
            cacheInvokeResult(id, result);
            sendInvokeResult(result);

            // wait for the runaway handler, then drop its result
            Json_de late = fut.get();
            std::cout << _LOG_CONSOLE_TEXT << "capability invoke " << id
                      << " (" << ns << "." << act << ") finished after its deadline - result dropped"
                      << _NORMAL_CONSOLE_TEXT_ << std::endl;
        }
    }).detach();
}


/**
 * @brief CAPABILITY_INVOKE {id, ns, act, p, dl}. Idempotent by "id": a
 * repeated id replays the cached result (or is ignored while inflight).
 * Params are validated against the advert before the handler sees them.
 */
void de::comm::CModule::handleCapabilityInvoke (const Json_de& cmd)
{
    const std::string id  = cmd.value("id", "");
    const std::string ns  = cmd.value("ns", "");
    std::string act = cmd.value("act", "");
    Json_de with = Json_de::object();
    if (cmd.contains("p") && cmd["p"].is_object()) with = cmd["p"];

    double deadline_s = 10.0;
    if (cmd.contains("dl") && cmd["dl"].is_number())
        deadline_s = cmd["dl"].get<double>();
    if (deadline_s <= 0) deadline_s = 10.0;

    if (id.empty()) return;

    {
        std::lock_guard<std::mutex> lock(m_cap_lock);

        auto it = m_invoke_results.find(id);
        if (it != m_invoke_results.end())
        {
            // idempotent replay - never run the handler twice
            sendInvokeResult(it->second);
            return;
        }
        if (m_invoke_inflight.count(id))
        {
            // same invoke while still running - the first result will
            // answer the resend too
            return;
        }
        m_invoke_inflight.insert(id);
    }

    auto fail = [this, &id](const std::string& err)
    {
        {
            std::lock_guard<std::mutex> lock(m_cap_lock);
            m_invoke_inflight.erase(id);
        }
        Json_de result = {{"id", id}, {"ok", false}, {"err", err}};
        cacheInvokeResult(id, result);
        sendInvokeResult(result);
    };

    // resolve "<ns>.<act>" in the adverts (copied - the adverts can be
    // replaced by a setCapabilities on another thread)
    Json_de action;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_cap_lock);
        for (const Json_de& advert : m_capability_parsed)
        {
            if (advert.value("ns", "") == ns)
            {
                const Json_de* a = capFindAction(advert, act);
                if (a != nullptr)
                {
                    action = *a;
                    found = true;
                    // an old (deprecated_alias) name reaches the handler
                    // as the action's current name
                    act = capCanonicalAction(advert, act);
                }
                break;
            }
        }
    }

    if (!found)
    {
        fail("unknown action " + ns + "." + act);
        return;
    }

    // params checked against the advert; defaults filled in
    Json_de filled;
    const Json_de params_schema = action.contains("params")
        ? action["params"] : Json_de::object();
    const std::vector<std::string> errors = capCheckParams(params_schema, with, filled);
    if (!errors.empty())
    {
        std::string err;
        for (const std::string& e : errors) { if (!err.empty()) err += "; "; err += e; }
        fail("invalid params: " + err);
        return;
    }

    if (m_onInvoke == nullptr)
    {
        fail("no invoke handler");
        return;
    }

    runInvokeOnWorker(id, ns, act, filled, deadline_s);
}