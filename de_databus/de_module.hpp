#ifndef UAVOS_MODULE_H
#define UAVOS_MODULE_H


#include <ctime>
#include <iostream>
#include <thread>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "../helpers/json_nlohmann.hpp"
#include "udpClient.hpp"
#include "unixDgramClient.hpp"
#include "messages.hpp"
#include "../helpers/helpers.hpp"
using Json_de = nlohmann::json;

typedef enum {
    HARDWARE_TYPE_UNDEFINED     = 0,
    HARDWARE_TYPE_CPU           = 1
} ENUM_HARDWARE_TYPE;


#define MODULE_FEATURE_RECEIVING_TELEMETRY      "R"
#define MODULE_FEATURE_SENDING_TELEMETRY        "T"
#define MODULE_FEATURE_CAPTURE_IMAGE            "C"
#define MODULE_FEATURE_CAPTURE_VIDEO            "V"
#define MODULE_FEATURE_GPIO                     "G"
#define MODULE_FEATURE_AI_RECOGNITION           "A"
#define MODULE_FEATURE_TRACKING                 "K"
#define MODULE_FEATURE_P2P                      "P" 
#define MODULE_FEATURE_PRECISION_LANDING        "L"


#define MODULE_CLASS_COMM                       "comm"
#define MODULE_CLASS_FCB                        "fcb"
#define MODULE_CLASS_VIDEO                      "camera"
#define MODULE_CLASS_P2P                        "p2p"
#define MODULE_CLASS_GENERIC                    "gen"
#define MODULE_CLASS_GPIO                       "gpio" 
#define MODULE_CLASS_A_RECOGNITION              "ai_rec" 
#define MODULE_CLASS_TRACKING                   "trk"
#define MODULE_CLASS_VIEWLINK                   "vlk"
#define MODULE_CLASS_IR_MI48_CAMERA             "ir"
#define MODULE_CLASS_TELNET                     "tel"
#define MODULE_CLASS_PRECISION_LANDING          "pl"


namespace de
{
namespace comm
{

/**
 * @brief Phase-3 invoke handler signature. Receives the invoke id (the
 * CAPABILITY_INVOKE "id", for correlating events fired by the action),
 * the capability namespace, the action name and the ALREADY-VALIDATED
 * params (checked against the advert, defaults filled in). Return the
 * action's result object (sent as CAPABILITY_RESULT "data"), or set `err`
 * to fail the invoke. Runs on a worker thread - never on the receive
 * thread.
 */
typedef std::function<Json_de (const std::string& id,
                               const std::string& ns,
                               const std::string& act,
                               const Json_de& params,
                               std::string& err)> CapabilityInvokeHandler;



    class CModule : public CCallBack_UDPClient
    {
        public:

            static CModule& getInstance()
            {
                static CModule instance;

                return instance;
            }

            CModule(CModule const&)               = delete;
            void operator=(CModule const&)        = delete;

        private:

            CModule(): cUDPClient(this), cUnixDgramClient(this)
            {
                m_instance_time_stamp = std::time(nullptr);
                m_hardware_serial = "";
                m_hardware_serial_type = HARDWARE_TYPE_UNDEFINED;

            }

            ~CModule() { uninit(); }
        
        public:

            void defineModule (
                 std::string module_class,
                 std::string module_id,
                 std::string module_key,
                 std::string module_version,
                 Json_de message_filter
            );
            
            bool init (const std::string targetIP, int broadcatsPort, const std::string host, int listenningPort, int chunkSize, bool use_unix_socket = false) ;
            bool uninit ();
            



        public:

            void sendBMSG (const std::string& targetPartyID, const char * bmsg, const int bmsg_length, const int& andruav_message_id, const bool& internal_message, const Json_de& message_cmd);
            void sendJMSG (const std::string targetPartyID, const Json_de jmsg, const int andruav_message_id, const bool internal_message);
            void sendSYSMSG (const Json_de& jmsg, const int& andruav_message_id);
            void sendMREMSG (const int& command_type);
            void forwardMSG (const char * message, const std::size_t datalength);

        public:

            void setMessageOnReceive (void (*onReceive)(const char *, int len, Json_de jMsg))
                {
                    m_OnReceive = onReceive;
                }
        
            void sendMSG (const char * msg, const int length)
                {
                    if (m_use_unix_socket) {
                        if (!cUnixDgramClient.isStarted()) return;
                        cUnixDgramClient.sendMSG(msg, length);
                    } else {
                        if (!cUDPClient.isStarted()) return;
                        cUDPClient.sendMSG(msg, length);
                    }
                }


            void onReceive (const char *, int len) override;

        public:

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
             */
            void createJSONID (bool reSend) ;


            

            
            // called from main
            void OnConnectionStatusChangedWithAndruavServer (const int status){};

            /**
             * @brief Get the Module Features object
             *  module features i.e. can transmit, can recieve, needs stream ...etc.
             * in this case we use T & R only. 
             * @return const Json 
             */
             inline const Json_de getModuleFeatures() const
            {
                return m_module_features;
            }

            inline void addModuleFeatures(const std::string feature)
            {
                m_module_features.push_back(feature);
            }

            
            void setModuleClass(const std::string module_class) 
            {
                m_module_class = module_class;
            }


            void setModuleId(const std::string module_id) 
            {
                m_module_id = module_id;
            }



            const std::string getModuleKey() const
            {
                return m_module_key;
            }

            void setModuleKey(const char* module_key) 
            {
                m_module_key = module_key;
            }


            void setHardware(const std::string hardware_serial, const ENUM_HARDWARE_TYPE hardware_serial_type) 
            {
                m_hardware_serial = hardware_serial;
                m_hardware_serial_type = hardware_serial_type;
            }


            inline const std::string getGroupId() const 
            {
                return m_group_id;
            }


            inline const std::string getPartyId() const 
            {
                return m_party_id;
            }

        public:

            void appendExtraField(const std::string name, const Json_de& ms);

        public:

            /**
             * @brief Phase-3 capability adverts (de.cap/1 documents).
             *  One advert string per namespace this module owns; each is
             *  validated and its "ch" hash is appended to the module ID
             *  message (extra field "ch"). Also subscribes this module to
             *  MODULE_CAPABILITIES / CAPABILITY_INVOKE in the message
             *  filter. Call before init(), or call again after a change -
             *  the ID message is regenerated either way.
             * @return false (+ log) when any advert fails validation
             */
            bool setCapabilities (const std::vector<std::string>& adverts);

            /**
             * @brief register the action handler invoked by
             *  CAPABILITY_INVOKE. Params arrive already validated against
             *  the advert (defaults filled). Results are idempotent:
             *  a repeated invoke "id" replays the cached result without
             *  calling the handler again (last 64 ids kept).
             */
            void onInvoke (CapabilityInvokeHandler handler);

            /**
             * @brief publish changed state keys for namespace `ns`
             *  (MODULE_STATE 6545, intermodule). full=true sends a complete
             *  snapshot that replaces the stored state.
             */
            void publishState (const std::string& ns, const Json_de& changed, const bool full = false);

            /**
             * @brief fire a capability event as Sync_EventFire
             *  d="<ns>.<ev>", ed=payload, n=<fresh nonce>, no "sc" -
             *  intermodule only, so de_comm's acceptExternalEvent
             *  de-duplicates by sender|d|n and never drops it on scope.
             * @return the nonce sent in "n" (callers that consume their own
             *  namespace's events can use it to filter self-echoes)
             */
            std::string fireEvent (const std::string& ns, const std::string& ev, const Json_de& payload);

        private:

            void capAddToFilter (const int msg_id);
            void sendCapabilities ();
            void handleCapabilityInvoke (const Json_de& cmd);
            void sendInvokeResult (const Json_de& result);
            void cacheInvokeResult (const std::string& id, const Json_de& result);
            void runInvokeOnWorker (const std::string& id, const std::string& ns,
                                    const std::string& act, const Json_de& params,
                                    const double deadline_s);

        protected:
            std::map <std::string,Json_de> m_stdinValues;
            

            

            /**
            * @brief module features i.e. can transmit, can recieve, needs stream ...etc.
            * in this case we use T & R only.
            */
            Json_de m_module_features = Json_de::array();

            /**
             * @brief Identifies the type of the module.
             * Modules of the same identical functions should have the same class
             * For example multiple camera modules should have same MODULE_CLASS_VIDEO.
             * 
             */
            std::string m_module_class;

            /**
             * @brief this id identifies this particular module. This can be 
             * FCB_Main, FCB_Secondary ....etc.
             * or CAM_MOD1, CAM_MOD2 ...etc.
             * 
             */
            std::string m_module_id;

            /**
             * @brief this is a GUID key for this module. the difference between
             * m_module_id & m_module_key is that m_module_key should be unique over
             * the whole system.
             * 
             */
            std::string m_module_key;



            std::string m_hardware_serial;


            ENUM_HARDWARE_TYPE m_hardware_serial_type;


            std::string m_module_version;

            std::time_t m_instance_time_stamp;
    
            CUDPClient cUDPClient;
            CUnixDgramClient cUnixDgramClient;
            bool m_use_unix_socket;

            /**
             * @brief DroneEngage Current m_party_id read from communicator
             * This is important communication part to identify myself and other senders.
             */
            std::string  m_party_id;
            /**
             * @brief DroneEngage Current m_group_id read from communicator
             * This is important communication part to identify myself and other senders.
             */
            std::string  m_group_id;
            
            Json_de m_message_filter;

            // Phase-3 capability helper state
            std::vector<std::string> m_capability_adverts;   // raw strings, as sent
            Json_de m_capability_parsed = Json_de::array();  // parsed, same order
            std::string m_capability_hash;                   // ID "ch" field
            CapabilityInvokeHandler m_onInvoke = nullptr;
            bool m_capabilities_sent = false;
            // invoke idempotency: id -> result, last 64 kept (FIFO)
            std::map<std::string, Json_de> m_invoke_results;
            std::deque<std::string> m_invoke_order;
            std::set<std::string> m_invoke_inflight;         // running right now
            std::atomic<uint64_t> m_event_counter {0};
            bool m_initialized = false;

            void (*m_OnReceive)(const char *, int len, Json_de jMsg) = nullptr;

            std::mutex m_lock;
            std::mutex m_cap_lock;   // guards m_invoke_* tables

        private:

            /**
             * @brief Background thread started by init() / stopped by uninit() that
             * calls sendMemoryStatus() every MODULE_HEALTH_INTERVAL_SEC. Centralized
             * here so every module gets self-reported memory health for free, instead
             * of each module's main.cpp having to remember to schedule it.
             */
            void healthHeartbeatLoop ();

            std::thread       m_health_thread;
            std::atomic<bool> m_health_thread_exit {false};
    };
};
};
#endif
