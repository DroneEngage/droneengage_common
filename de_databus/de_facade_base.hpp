#ifndef P2P_ESP32_FACADE_BASE_H_
#define P2P_ESP32_FACADE_BASE_H_

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
            static CFacade_Base &getInstance()
            {
                static CFacade_Base instance;

                return instance;
            }

        public:
            void requestID(const std::string &target_party_id) const;
            void sendErrorMessage(const std::string &target_party_id, const int &error_number, const int &info_type, const int &notification_type, const std::string &description) const;

            void API_sendConfigTemplate(const std::string &target_party_id, const std::string &module_key, const Json_de &json_file_content_json, const bool reply);

            /**
             * @brief Configure this module's memory ceiling/growth thresholds used by
             * sendMemoryStatus(). Defaults are 500MB / 20MB/h if not called. See
             * CModuleHealthMonitor::configure().
             */
            void configureMemoryStatus(const double max_rss_mb, const double max_growth_mb_per_hour) const;

            /**
             * @brief Samples this process's own memory (RSS/VmPeak/VmSwap/threads),
             * derives a growth trend + health status, and sends it as
             * TYPE_AndruavMessage_MODULE_HEALTH_STATUS. CModule::init() already calls
             * this automatically every 30s via CFacade_Base::getInstance(), so modules
             * normally never need to call this directly - it remains available for an
             * on-demand report. No-op if /proc/self/status is unavailable (e.g.
             * non-Linux host).
             */
            void sendMemoryStatus(const std::string &target_party_id) const;

        protected:
            CModule &m_module = de::comm::CModule::getInstance();
        };
    }
}
#endif