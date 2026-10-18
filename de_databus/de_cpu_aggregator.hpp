#ifndef DE_CPU_AGGREGATOR_H_
#define DE_CPU_AGGREGATOR_H_

/**
 * @brief unit CPU aggregator (TASK-32/GL-0039) - pure logic, hosted by
 * de_comm. Every module reports its board CPU on intermodule message
 * 6547; this class merges those reports into the virtual unit state
 * that goes out to the GCS as 1008 CP/MI.
 *
 * Model: the latest report is kept per (board id, module id). A board's
 * usage = max u over its FRESH reports (fresh = seen within
 * 3 x report_s); a fully stale board keeps its last numbers for the
 * summary but is flagged st=true. Board state = SUPER_BUSY when any
 * fresh report on it says s=CPU_BOARD_STATE_SUPER_BUSY.
 *
 * Unit usage = max over all known boards (never an average). Unit state
 * = SUPER_BUSY iff at least one FRESH board is SUPER_BUSY (TASK-29 Q1
 * answer); a stale board can never make the unit busy, and the unit
 * goes FREE the moment no fresh board is busy - including when the busy
 * board simply went silent. A board with no report for 60 s is removed
 * and the unit state is recomputed.
 *
 * tick(now_ms) returns a bitmask (UNIT_CPU_EVENT_*): at most one unit
 * transition edge (never repeated while steady) plus SUMMARY_DUE every
 * summary_s (0 = never). deBuildUnitCpuWarning()/deBuildUnitCpuSummary()
 * build the 1008 payload object.
 *
 * Pure: no sockets, no clock reads - the caller passes now_ms.
 */

#include <map>
#include <string>

#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

#include "./de_cpu_monitor.hpp"

namespace de
{
namespace comm
{

/// bitmask returned by CCpuAggregator::tick()
enum UnitCpuEvent
{
    UNIT_CPU_EVENT_NONE              = 0,
    UNIT_CPU_EVENT_BECAME_SUPER_BUSY = 1,   //< unit edge into SUPER_BUSY
    UNIT_CPU_EVENT_BECAME_FREE       = 2,   //< unit edge back to FREE
    UNIT_CPU_EVENT_SUMMARY_DUE       = 4    //< summary_s elapsed
};


/**
 * @brief merge 6547 MODULE_CPU_STATUS reports into the virtual unit
 * CPU state.
 */
class CCpuAggregator
{
    public:
        /// report_s drives the stale window (3x) and the module bookkeeping;
        /// summary_s drives SUMMARY_DUE (0 = never)
        explicit CCpuAggregator (const CpuMonitorConfig& config = CpuMonitorConfig());

        /// apply a new config; keeps reports and unit state, drops timers
        void configure (const CpuMonitorConfig& config);

        /**
         * @brief merge one 6547 report received from @p module_id.
         * Fields per the 6547 contract: bd string REQUIRED, u int
         * REQUIRED, s int REQUIRED, pu int OPTIONAL, ev ignored (the
         * aggregator derives transitions itself). Returns false on a
         * malformed report (missing/wrong-typed required fields) - the
         * report is dropped.
         */
        bool onReport (const std::string& module_id, const Json_de& report,
                       const uint64_t now_ms);

        /**
         * @brief recompute the unit state at @p now_ms: expire stale
         * boards (>60 s silent -> removed), edge-detect the unit state,
         * and fire SUMMARY_DUE every summary_s. Returns a bitmask of
         * UnitCpuEvent; a unit transition is reported once, on the edge.
         */
        int tick (const uint64_t now_ms);

        int          unitUsagePercent () const { return m_unit_usage; }
        CpuBusyState unitState        () const { return m_unit_state; }
        int          boardCount       () const { return static_cast<int>(m_boards.size()); }

        /**
         * @brief the 1008 CP object at @p now_ms: {u, s, b:[{id,u,s,st?,
         * m:[[module_id,pu],...]}]} per the TASK-29 contract.
         */
        Json_de buildCpuDetailJson (const uint64_t now_ms) const;

        /**
         * @brief 1008 payload for a unit transition: EN=ERROR_SYSTEM_LOAD,
         * IT=NOTIFICATION_TYPE_TELEMETRY, NT=WARNING/NOTICE, MI=1/2, DS
         * naming the busiest busy board and its highest-pu module, CP.
         */
        Json_de deBuildUnitCpuWarning (const bool became_super_busy,
                                       const uint64_t now_ms) const;

        /**
         * @brief 1008 payload for the periodic unit summary:
         * EN=ERROR_SYSTEM_LOAD, IT=NOTIFICATION_TYPE_TELEMETRY, NT=INFO,
         * MI=3, DS naming the busiest board, CP.
         */
        Json_de deBuildUnitCpuSummary (const uint64_t now_ms) const;

    private:
        struct ModuleReport
        {
            int      u               = 0;      //< board whole-system %
            int      pu              = -1;     //< module process % (-1 = not reported)
            int      s               = 0;      //< CPU_BOARD_STATE_*
            uint64_t seen_at_ms      = 0;
        };
        // board id -> module id -> latest report
        typedef std::map<std::string, std::map<std::string, ModuleReport>> BoardsByModule;

        bool     boardIsFresh      (const std::map<std::string, ModuleReport>& modules,
                                    const uint64_t now_ms) const;
        void     computeBoardStats (const std::map<std::string, ModuleReport>& modules,
                                    const uint64_t now_ms,
                                    int& out_u, int& out_s, uint64_t& out_newest_ms) const;
        uint64_t staleWindowMs () const
        { return 3ULL * static_cast<uint64_t>(m_config.report_s) * 1000ULL; }

        CpuMonitorConfig m_config;
        BoardsByModule   m_boards;
        CpuBusyState     m_unit_state          = CpuBusyState::FREE;
        int              m_unit_usage          = 0;
        bool             m_clock_started       = false;
        uint64_t         m_last_summary_ms     = 0;
};

}
}

#endif // DE_CPU_AGGREGATOR_H_
