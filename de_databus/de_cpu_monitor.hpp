#ifndef DE_CPU_MONITOR_H_
#define DE_CPU_MONITOR_H_

/**
 * @brief board CPU busy monitor (TASK-25/GL-0032, moved to de_common by
 * TASK-30/GL-0037) - every module samples its board and reports to
 * de_comm on 6547.
 *
 * Pure, no singletons, no /proc access in the monitor itself: the caller
 * feeds the text of /proc/stat and a monotonic timestamp, the monitor
 * computes busy% between successive aggregate "cpu " lines and runs a
 * two-state FREE <-> SUPER_BUSY machine with hysteresis:
 *
 *   FREE       -> SUPER_BUSY when usage >= super_busy_percent
 *                 held for sustain_s
 *   SUPER_BUSY -> FREE       when usage <= free_percent
 *                 held for sustain_s
 *
 * Usage sitting between the two thresholds keeps the current state -
 * that dead band is what stops flapping. A transition is reported once,
 * on the edge; steady load reports NOTHING.
 *
 * Also here: the process-level /proc/self/stat parser used for the 6547
 * "pu" field, deBoardId() for the "bd" field, and the deRead*Proc*
 * sampler wrappers modules call once per tick.
 */

#include <cstdint>
#include <string>

#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

namespace de
{
namespace comm
{

/// thresholds + gates, from the optional "cpu_monitor" config object
struct CpuMonitorConfig
{
    bool     enabled            = true;  //< false: wiring never calls update()
    int      super_busy_percent = 90;    //< usage >= this, sustained, -> SUPER_BUSY
    int      free_percent       = 70;    //< usage <= this, sustained, -> FREE
    int      sustain_s          = 5;     //< seconds a side must hold before the switch
    int      report_s           = 10;    //< module periodic 6547 report interval
    int      summary_s          = 60;    //< unit CPU summary to GCS interval (0 = off)
};

/// the two machine states
enum class CpuBusyState { FREE = 0, SUPER_BUSY = 1 };

/// what update() returns on the transition edge
enum class CpuTransition { NONE = 0, BECAME_SUPER_BUSY = 1, BECAME_FREE = 2 };

/// one aggregate /proc/stat "cpu " line, summed: total ticks vs idle part
struct CpuTimes
{
    uint64_t total    = 0;   //< all columns summed (guest already folded by the kernel)
    uint64_t idle_all = 0;   //< idle + iowait (iowait counts as waiting, not busy)
};

/// one process's /proc/self/stat jiffies used for the 6547 "pu" field
struct ProcCpuTimes
{
    uint64_t busy_jiffies = 0;   //< utime + stime (kernel ticks)
};

/**
 * @brief parse the aggregate "cpu " line out of /proc/stat text.
 * total = sum of every numeric column; idle_all = idle + iowait
 * (a missing iowait column on very old kernels counts as 0).
 * Returns false when the text has no usable aggregate line or fewer
 * than the mandatory 4 columns (user nice system idle).
 */
bool deParseProcStatCpu (const std::string& proc_stat_text, CpuTimes& out);

/**
 * @brief busy% between two samples:
 * (total_delta - idle_all_delta) / total_delta * 100.
 * Returns -1 when the total did not advance (no new information).
 */
double deCpuUsagePercent (const CpuTimes& prev, const CpuTimes& cur);

/**
 * @brief parse /proc/self/stat text into utime+stime jiffies. The comm
 * field (second column, in parentheses) may contain spaces and ')' so
 * the parse anchors on the LAST ')' in the line; the state char is
 * consumed next and utime/stime are overall fields 14/15.
 * Returns false when the text has no ')' or fewer than 15 fields.
 */
bool deParseProcSelfStatCpu (const std::string& proc_self_stat_text, ProcCpuTimes& out);

/**
 * @brief this process's CPU % between two /proc/self/stat samples:
 * (busy_jiffies_delta / ticks_per_second) / (elapsed_ms / 1000) * 100.
 * Returns -1 on no elapsed time or when counters did not advance.
 * Caller passes ticks_per_second (sysconf(_SC_CLK_TCK)) so the function
 * stays pure/testable.
 */
double deProcessCpuUsagePercent (const ProcCpuTimes& prev, const ProcCpuTimes& cur,
                                 const uint64_t elapsed_ms, const long ticks_per_second);

/**
 * @brief read the optional "cpu_monitor" object out of the module config
 * root into CpuMonitorConfig. Absent -> defaults.
 * free_percent >= super_busy_percent (or an out-of-range field, or a
 * negative report_s/summary_s) falls back to ALL defaults with a
 * console warning - never a half-applied threshold pair.
 */
CpuMonitorConfig deParseCpuMonitorConfig (const Json_de& module_config);

/**
 * @brief stable board identifier for the 6547 "bd" field:
 * DE_BOARD_ID env wins when set and non-empty; else the first line of
 * /etc/machine-id; else gethostname(). The non-env value is cached
 * after the first read.
 */
std::string deBoardId ();

/**
 * @brief sampler wrapper: read + parse the aggregate cpu line in one
 * call. Path is env DE_PROC_STAT_PATH when set, else /proc/stat (the
 * env override lets a dev-box test fake a busy board). Returns false
 * when the file cannot be read or parsed.
 */
bool deReadProcStatCpu (CpuTimes& out);

/**
 * @brief sampler wrapper: read + parse /proc/self/stat utime/stime.
 * Path is env DE_PROC_SELF_STAT_PATH when set, else /proc/self/stat.
 * Returns false when the file cannot be read or parsed.
 */
bool deReadProcSelfStatCpu (ProcCpuTimes& out);

/**
 * @brief the 6547 MODULE_CPU_STATUS report body (the message "ms"
 * object): {bd board id, u board whole-system %, pu this process %,
 * s CPU_BOARD_STATE_*, ev CPU_REPORT_EVENT_*}. u/pu are clamped to
 * 0..100 per the TASK-29 contract. Pure - unit-tested without a
 * socket; CFacade_Base::sendCpuStatus() is the sender.
 */
Json_de deBuildCpuStatusJson (const std::string& board_id,
                              const int board_usage_percent,
                              const int process_usage_percent,
                              const int board_state,
                              const int report_event);

class CCpuMonitor
{
    public:
        CCpuMonitor();
        explicit CCpuMonitor (const CpuMonitorConfig& config);

        /// apply a new config; keeps the current state, drops a pending timer
        void configure (const CpuMonitorConfig& config);

        /**
         * @brief feed the /proc/stat text sampled at @p now_ms (monotonic).
         * The first successful sample only seeds the baseline (NONE).
         * Unparsable text keeps the previous baseline and reports NONE.
         */
        CpuTransition update (const std::string& proc_stat_text, const uint64_t now_ms);

        /**
         * @brief same as update() but takes an already-parsed CpuTimes
         * (from deReadProcStatCpu) - one read path when the caller also
         * wants the raw numbers for the 6547 "u" field.
         */
        CpuTransition updateFromTimes (const CpuTimes& cur, const uint64_t now_ms);

        CpuBusyState state () const { return m_state; }
        double lastUsagePercent () const { return m_last_usage; }
        const CpuMonitorConfig& config () const { return m_config; }

    private:
        CpuTransition onUsage (const double usage_percent, const uint64_t now_ms);

        CpuMonitorConfig m_config;
        CpuBusyState     m_state             = CpuBusyState::FREE;
        bool             m_have_sample       = false;
        CpuTimes         m_prev;
        double           m_last_usage        = -1.0;
        int              m_pending_dir       = 0;   //< +1 toward SUPER_BUSY, -1 toward FREE, 0 idle
        uint64_t         m_pending_since_ms  = 0;
};

}
}

#endif // DE_CPU_MONITOR_H_
