#include "./de_cpu_aggregator.hpp"

#include "./messages.hpp"


static const uint64_t BOARD_REMOVE_AFTER_MS = 60000ULL;


de::comm::CCpuAggregator::CCpuAggregator (const CpuMonitorConfig& config)
    : m_config (config)
{
}


void de::comm::CCpuAggregator::configure (const CpuMonitorConfig& config)
{
    m_config         = config;
    m_clock_started  = false;   // next tick re-baselines the summary timer
    m_last_summary_ms = 0;
}


bool de::comm::CCpuAggregator::onReport (const std::string& module_id,
                                         const Json_de& report,
                                         const uint64_t now_ms)
{
    if ((!report.is_object())
        || (!report.contains("bd")) || (!report["bd"].is_string())
        || (!report.contains("u"))  || (!report["u"].is_number())
        || (!report.contains("s"))  || (!report["s"].is_number()))
    {
        return false;
    }

    const std::string board_id = report["bd"].get<std::string>();
    if (board_id.empty()) return false;

    ModuleReport entry;
    entry.u          = report["u"].get<int>();
    entry.s          = report["s"].get<int>();
    entry.seen_at_ms = now_ms;
    if (report.contains("pu") && report["pu"].is_number())
    {
        entry.pu = report["pu"].get<int>();
    }

    m_boards[board_id][module_id] = entry;
    return true;
}


bool de::comm::CCpuAggregator::boardIsFresh (const std::map<std::string, ModuleReport>& modules,
                                             const uint64_t now_ms) const
{
    for (const auto& module_and_report : modules)
    {
        const ModuleReport& report = module_and_report.second;
        if ((now_ms >= report.seen_at_ms)
            && ((now_ms - report.seen_at_ms) <= staleWindowMs()))
        {
            return true;
        }
    }
    return false;
}


void de::comm::CCpuAggregator::computeBoardStats (const std::map<std::string, ModuleReport>& modules,
                                                  const uint64_t now_ms,
                                                  int& out_u, int& out_s,
                                                  uint64_t& out_newest_ms) const
{
    // usage = max u over FRESH reports; when the whole board is stale
    // its last numbers still show (flagged st=true) so fall back to all
    // reports. state = SUPER_BUSY iff any FRESH report says busy.
    out_u         = 0;
    out_s         = CPU_BOARD_STATE_FREE;
    out_newest_ms = 0;

    bool saw_fresh = false;
    int  stale_max_u = 0;
    for (const auto& module_and_report : modules)
    {
        const ModuleReport& report = module_and_report.second;
        if (report.seen_at_ms > out_newest_ms) out_newest_ms = report.seen_at_ms;
        if (report.u > stale_max_u) stale_max_u = report.u;

        const bool fresh = (now_ms >= report.seen_at_ms)
                        && ((now_ms - report.seen_at_ms) <= staleWindowMs());
        if (!fresh) continue;

        saw_fresh = true;
        if (report.u > out_u) out_u = report.u;
        if (report.s == CPU_BOARD_STATE_SUPER_BUSY) out_s = CPU_BOARD_STATE_SUPER_BUSY;
    }
    if (!saw_fresh) out_u = stale_max_u;
}


int de::comm::CCpuAggregator::tick (const uint64_t now_ms)
{
    if (!m_clock_started)
    {
        m_clock_started   = true;
        m_last_summary_ms = now_ms;
    }

    // drop boards silent for over a minute, then fold the rest into the
    // unit figure - stale boards are listed but never make the unit busy
    int unit_usage = 0;
    bool any_fresh_board_busy = false;
    for (auto board_it = m_boards.begin(); board_it != m_boards.end();)
    {
        int      board_u, board_s;
        uint64_t newest_ms;
        computeBoardStats(board_it->second, now_ms, board_u, board_s, newest_ms);

        if ((now_ms >= newest_ms) && ((now_ms - newest_ms) > BOARD_REMOVE_AFTER_MS))
        {
            board_it = m_boards.erase(board_it);
            continue;
        }

        if (board_u > unit_usage) unit_usage = board_u;
        if (board_s == CPU_BOARD_STATE_SUPER_BUSY) any_fresh_board_busy = true;
        ++board_it;
    }

    int events = UNIT_CPU_EVENT_NONE;

    m_unit_usage = unit_usage;
    const CpuBusyState want = any_fresh_board_busy ? CpuBusyState::SUPER_BUSY
                                                 : CpuBusyState::FREE;
    if (want != m_unit_state)
    {
        m_unit_state = want;
        events |= (want == CpuBusyState::SUPER_BUSY)
                ? UNIT_CPU_EVENT_BECAME_SUPER_BUSY
                : UNIT_CPU_EVENT_BECAME_FREE;
    }

    if ((m_config.summary_s > 0)
        && (now_ms >= m_last_summary_ms)
        && ((now_ms - m_last_summary_ms) >= static_cast<uint64_t>(m_config.summary_s) * 1000ULL))
    {
        m_last_summary_ms = now_ms;
        events |= UNIT_CPU_EVENT_SUMMARY_DUE;
    }

    return events;
}


Json_de de::comm::CCpuAggregator::buildCpuDetailJson (const uint64_t now_ms) const
{
    Json_de boards = Json_de::array();
    for (const auto& board_and_modules : m_boards)
    {
        int      board_u, board_s;
        uint64_t newest_ms;
        computeBoardStats(board_and_modules.second, now_ms, board_u, board_s, newest_ms);

        Json_de board;
        board["id"] = board_and_modules.first;
        board["u"]  = board_u;
        board["s"]  = board_s;
        if (!boardIsFresh(board_and_modules.second, now_ms))
        {
            board["st"] = true;
        }

        Json_de modules = Json_de::array();
        for (const auto& module_and_report : board_and_modules.second)
        {
            if (module_and_report.second.pu < 0) continue;   // module did not report pu
            modules.push_back(Json_de::array({module_and_report.first,
                                              module_and_report.second.pu}));
        }
        if (!modules.empty()) board["m"] = modules;

        boards.push_back(board);
    }

    Json_de detail;
    detail["u"] = m_unit_usage;
    detail["s"] = (m_unit_state == CpuBusyState::SUPER_BUSY) ? CPU_BOARD_STATE_SUPER_BUSY
                                                           : CPU_BOARD_STATE_FREE;
    detail["b"] = boards;
    return detail;
}


Json_de de::comm::CCpuAggregator::deBuildUnitCpuWarning (const bool became_super_busy,
                                                         const uint64_t now_ms) const
{
    // the DS names the busiest FRESH busy board and its highest-pu module
    // so the operator sees "who" is loading the unit, not just "where"
    std::string busiest_board;
    int         busiest_board_u = -1;
    std::string busiest_module;
    int         busiest_module_pu = -1;
    for (const auto& board_and_modules : m_boards)
    {
        int      board_u, board_s;
        uint64_t newest_ms;
        computeBoardStats(board_and_modules.second, now_ms, board_u, board_s, newest_ms);
        if ((board_s != CPU_BOARD_STATE_SUPER_BUSY) || (board_u < busiest_board_u)) continue;

        busiest_board   = board_and_modules.first;
        busiest_board_u = board_u;
        for (const auto& module_and_report : board_and_modules.second)
        {
            if (module_and_report.second.pu > busiest_module_pu)
            {
                busiest_module_pu = module_and_report.second.pu;
                busiest_module    = module_and_report.first;
            }
        }
    }

    std::string description;
    if (became_super_busy)
    {
        description = "Unit CPU super busy " + std::to_string(m_unit_usage) + "%";
        if (!busiest_board.empty())
        {
            description += " - busiest board '" + busiest_board + "' "
                         + std::to_string(busiest_board_u) + "%";
            if (!busiest_module.empty())
            {
                description += " (module '" + busiest_module + "' "
                             + std::to_string(busiest_module_pu) + "%)";
            }
        }
    }
    else
    {
        description = "Unit CPU free " + std::to_string(m_unit_usage) + "%";
    }

    Json_de payload;
    payload["EN"] = ERROR_SYSTEM_LOAD;
    payload["IT"] = NOTIFICATION_TYPE_TELEMETRY;
    payload["NT"] = became_super_busy ? NOTIFICATION_TYPE_WARNING
                                    : NOTIFICATION_TYPE_NOTICE;
    payload["DS"] = description;
    payload["MI"] = became_super_busy ? MESSAGE_ID_CPU_SUPER_BUSY
                                    : MESSAGE_ID_CPU_FREE;
    payload["CP"] = buildCpuDetailJson(now_ms);
    return payload;
}


Json_de de::comm::CCpuAggregator::deBuildUnitCpuSummary (const uint64_t now_ms) const
{
    std::string busiest_board;
    int         busiest_board_u = -1;
    for (const auto& board_and_modules : m_boards)
    {
        int      board_u, board_s;
        uint64_t newest_ms;
        computeBoardStats(board_and_modules.second, now_ms, board_u, board_s, newest_ms);
        if (board_u > busiest_board_u)
        {
            busiest_board_u = board_u;
            busiest_board   = board_and_modules.first;
        }
    }

    std::string description = "Unit CPU " + std::to_string(m_unit_usage) + "% across "
                            + std::to_string(m_boards.size()) + " board(s)";
    if (!busiest_board.empty())
    {
        description += " - busiest '" + busiest_board + "' "
                     + std::to_string(busiest_board_u) + "%";
    }

    Json_de payload;
    payload["EN"] = ERROR_SYSTEM_LOAD;
    payload["IT"] = NOTIFICATION_TYPE_TELEMETRY;
    payload["NT"] = NOTIFICATION_TYPE_INFO;
    payload["DS"] = description;
    payload["MI"] = MESSAGE_ID_CPU_SUMMARY;
    payload["CP"] = buildCpuDetailJson(now_ms);
    return payload;
}
