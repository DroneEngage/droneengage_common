#include "./de_cpu_monitor.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "../helpers/colors.hpp"


bool de::comm::deParseProcStatCpu (const std::string& proc_stat_text, CpuTimes& out)
{
    std::istringstream in (proc_stat_text);
    std::string line;
    while (std::getline(in, line))
    {
        // the aggregate line is exactly "cpu" + space; "cpu0"/"cpu1"/...
        // per-core lines are skipped
        if (line.compare(0, 4, "cpu ") != 0) continue;

        std::istringstream fields (line.substr(4));
        uint64_t values[16];
        int count = 0;
        uint64_t v;
        while ((count < 16) && (fields >> v))
        {
            values[count++] = v;
        }
        // user nice system idle are mandatory; iowait (index 4) is
        // absent on very old kernels and counts as 0
        if (count < 4) return false;

        out.total    = 0;
        for (int i = 0; i < count; ++i) out.total += values[i];
        out.idle_all = values[3] + ((count > 4) ? values[4] : 0);
        return true;
    }
    return false;
}


double de::comm::deCpuUsagePercent (const CpuTimes& prev, const CpuTimes& cur)
{
    if (cur.total <= prev.total) return -1.0;

    const uint64_t total_delta = cur.total    - prev.total;
    const uint64_t idle_delta  = cur.idle_all - prev.idle_all;
    const uint64_t busy_delta  = (idle_delta <= total_delta) ? (total_delta - idle_delta) : 0;

    return (100.0 * static_cast<double>(busy_delta)) / static_cast<double>(total_delta);
}


bool de::comm::deParseProcSelfStatCpu (const std::string& proc_self_stat_text, ProcCpuTimes& out)
{
    // comm (field 2) is wrapped in parentheses and may itself contain
    // spaces and ')' - e.g. "1234 (de tracker) S 1 ..." - so anchor on
    // the LAST ')'. The token after comm is state (a char), then the
    // numeric fields: ppid(0) ... utime(10) stime(11) overall fields 14/15.
    const std::string::size_type comm_end = proc_self_stat_text.rfind(')');
    if (comm_end == std::string::npos) return false;

    std::istringstream fields (proc_self_stat_text.substr(comm_end + 1));
    std::string state;
    if (!(fields >> state)) return false;

    uint64_t values[16];
    int count = 0;
    uint64_t v;
    while ((count < 16) && (fields >> v))
    {
        values[count++] = v;
    }
    // need at least through stime: 12 numeric fields (ppid .. stime)
    if (count < 12) return false;

    out.busy_jiffies = values[10] + values[11];
    return true;
}


double de::comm::deProcessCpuUsagePercent (const ProcCpuTimes& prev, const ProcCpuTimes& cur,
                                         const uint64_t elapsed_ms, const long ticks_per_second)
{
    if ((elapsed_ms == 0) || (ticks_per_second <= 0)) return -1.0;
    if (cur.busy_jiffies <= prev.busy_jiffies) return -1.0;

    const uint64_t busy_delta = cur.busy_jiffies - prev.busy_jiffies;
    const double busy_s = static_cast<double>(busy_delta) / static_cast<double>(ticks_per_second);
    return (100.0 * busy_s) / (static_cast<double>(elapsed_ms) / 1000.0);
}


de::comm::CpuMonitorConfig de::comm::deParseCpuMonitorConfig (const Json_de& module_config)
{
    CpuMonitorConfig cfg;
    if ((!module_config.contains("cpu_monitor")) || (!module_config["cpu_monitor"].is_object()))
    {
        return cfg;
    }

    const Json_de& src = module_config["cpu_monitor"];
    if (src.contains("enabled")            && src["enabled"].is_boolean())             cfg.enabled            = src["enabled"].get<bool>();
    if (src.contains("super_busy_percent") && src["super_busy_percent"].is_number())   cfg.super_busy_percent = src["super_busy_percent"].get<int>();
    if (src.contains("free_percent")       && src["free_percent"].is_number())         cfg.free_percent       = src["free_percent"].get<int>();
    if (src.contains("sustain_s")          && src["sustain_s"].is_number())            cfg.sustain_s          = src["sustain_s"].get<int>();
    if (src.contains("report_s")           && src["report_s"].is_number())             cfg.report_s           = src["report_s"].get<int>();
    if (src.contains("summary_s")          && src["summary_s"].is_number())            cfg.summary_s          = src["summary_s"].get<int>();

    const bool bad_range = (cfg.super_busy_percent < 1) || (cfg.super_busy_percent > 100)
                        || (cfg.free_percent < 0)       || (cfg.free_percent > 100)
                        || (cfg.sustain_s < 1)
                        || (cfg.report_s < 1)           || (cfg.summary_s < 0);
    if (bad_range || (cfg.free_percent >= cfg.super_busy_percent))
    {
        std::cout << _ERROR_CONSOLE_TEXT_ << "cpu_monitor: invalid thresholds (free_percent "
                  << cfg.free_percent << " >= super_busy_percent " << cfg.super_busy_percent
                  << " or out of range) - falling back to defaults" << _NORMAL_CONSOLE_TEXT_ << std::endl;
        cfg = CpuMonitorConfig();
    }
    return cfg;
}


static bool deReadTextFile (const std::string& path, std::string& out)
{
    std::ifstream in (path);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}


bool de::comm::deReadProcStatCpu (CpuTimes& out)
{
    const char* override_path = std::getenv("DE_PROC_STAT_PATH");
    const std::string path = (override_path && override_path[0]) ? override_path : "/proc/stat";

    std::string text;
    if (!deReadTextFile(path, text)) return false;
    return deParseProcStatCpu(text, out);
}


bool de::comm::deReadProcSelfStatCpu (ProcCpuTimes& out)
{
    const char* override_path = std::getenv("DE_PROC_SELF_STAT_PATH");
    const std::string path = (override_path && override_path[0]) ? override_path : "/proc/self/stat";

    std::string text;
    if (!deReadTextFile(path, text)) return false;
    return deParseProcSelfStatCpu(text, out);
}


std::string de::comm::deBoardId ()
{
    // env override always wins and is re-read per call (tests + dev-box
    // fakes); the machine-id/hostname fallback is read once and cached.
    const char* env_board_id = std::getenv("DE_BOARD_ID");
    if (env_board_id && env_board_id[0]) return env_board_id;

    static std::string cached_board_id;
    if (!cached_board_id.empty()) return cached_board_id;

    std::ifstream machine_id_file ("/etc/machine-id");
    if (machine_id_file && std::getline(machine_id_file, cached_board_id) && !cached_board_id.empty())
    {
        return cached_board_id;
    }

    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0)
    {
        cached_board_id = hostname;
    }
    return cached_board_id;
}


Json_de de::comm::deBuildCpuStatusJson (const std::string& board_id,
                                        const int board_usage_percent,
                                        const int process_usage_percent,
                                        const int board_state,
                                        const int report_event)
{
    const auto clamp_pct = [] (const int v) -> int
    {
        if (v < 0)   return 0;
        if (v > 100) return 100;
        return v;
    };

    Json_de report;
    report["bd"] = board_id;
    report["u"]  = clamp_pct(board_usage_percent);
    report["pu"] = clamp_pct(process_usage_percent);
    report["s"]  = board_state;
    report["ev"] = report_event;
    return report;
}


de::comm::CCpuMonitor::CCpuMonitor ()
{
}


de::comm::CCpuMonitor::CCpuMonitor (const CpuMonitorConfig& config)
    : m_config (config)
{
}


void de::comm::CCpuMonitor::configure (const CpuMonitorConfig& config)
{
    m_config           = config;
    m_pending_dir      = 0;
    m_pending_since_ms = 0;
}


de::comm::CpuTransition de::comm::CCpuMonitor::update (const std::string& proc_stat_text,
                                                     const uint64_t now_ms)
{
    CpuTimes cur;
    if (!deParseProcStatCpu(proc_stat_text, cur)) return CpuTransition::NONE;

    return updateFromTimes(cur, now_ms);
}


de::comm::CpuTransition de::comm::CCpuMonitor::updateFromTimes (const CpuTimes& cur,
                                                              const uint64_t now_ms)
{
    if (!m_have_sample)
    {
        // first sample is the baseline - usage needs two samples
        m_prev        = cur;
        m_have_sample = true;
        return CpuTransition::NONE;
    }

    const double usage = deCpuUsagePercent(m_prev, cur);
    if (usage < 0.0) return CpuTransition::NONE;   // counter did not advance

    m_prev       = cur;
    m_last_usage = usage;
    return onUsage(usage, now_ms);
}


de::comm::CpuTransition de::comm::CCpuMonitor::onUsage (const double usage_percent,
                                                      const uint64_t now_ms)
{
    // which side is pressuring the machine state, if any
    int want = 0;
    if ((m_state == CpuBusyState::FREE)       && (usage_percent >= m_config.super_busy_percent)) want = +1;
    if ((m_state == CpuBusyState::SUPER_BUSY) && (usage_percent <= m_config.free_percent))       want = -1;

    if (want == 0)
    {
        // between thresholds (or back under before sustain) - dead band,
        // no flapping, any pending window is dropped
        m_pending_dir      = 0;
        m_pending_since_ms = 0;
        return CpuTransition::NONE;
    }

    if (m_pending_dir != want)
    {
        m_pending_dir      = want;
        m_pending_since_ms = now_ms;
        return CpuTransition::NONE;
    }

    const uint64_t held_ms = (now_ms >= m_pending_since_ms) ? (now_ms - m_pending_since_ms) : 0;
    if (held_ms < static_cast<uint64_t>(m_config.sustain_s) * 1000ULL) return CpuTransition::NONE;

    m_pending_dir      = 0;
    m_pending_since_ms = 0;
    if (want > 0)
    {
        m_state = CpuBusyState::SUPER_BUSY;
        return CpuTransition::BECAME_SUPER_BUSY;
    }
    m_state = CpuBusyState::FREE;
    return CpuTransition::BECAME_FREE;
}
