/**
 * TASK-32 tests (GL-0039): CCpuAggregator unit-CPU merge - bd dedupe
 * across modules, any-fresh-board unit state with exactly one edge per
 * change, SUMMARY_DUE cadence (and summary_s=0 off), stale boards flagged
 * st=true without flipping the unit, 60 s removal + recompute, and the
 * 1008 payloads (MI 1/2/3, NT WARNING/NOTICE/INFO, CP.u = max board u,
 * CP.b lists every board with per-module pu pairs).
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include "../de_databus/de_cpu_aggregator.hpp"
#include "../de_databus/messages.hpp"

using namespace de::comm;

static int g_checks = 0;
static int g_fails = 0;

static void check (bool cond, const char* name)
{
    ++g_checks;
    if (cond) { std::printf("PASS %s\n", name); }
    else      { std::printf("FAIL %s\n", name); ++g_fails; }
}

/// builds a 6547 report body
static Json_de report6547 (const std::string& board_id, const int u,
                           const int s, const int pu = -1)
{
    Json_de report;
    report["bd"] = board_id;
    report["u"]  = u;
    report["s"]  = s;
    if (pu >= 0) report["pu"] = pu;
    return report;
}

static bool hasBoard (const Json_de& detail, const std::string& board_id)
{
    for (const auto& board : detail["b"])
    {
        if (board["id"].get<std::string>() == board_id) return true;
    }
    return false;
}

static const Json_de* findBoard (const Json_de& detail, const std::string& board_id)
{
    for (const auto& board : detail["b"])
    {
        if (board["id"].get<std::string>() == board_id) return &board;
    }
    return nullptr;
}

int main ()
{
    CpuMonitorConfig cfg;
    cfg.report_s  = 10;   // stale window = 30 s
    cfg.summary_s = 60;

    // ---- malformed reports are dropped ----
    {
        CCpuAggregator agg(cfg);
        check(!agg.onReport("de_tracker", Json_de::object(), 0), "missing bd dropped");
        Json_de bad = report6547("", 50, CPU_BOARD_STATE_FREE);
        check(!agg.onReport("de_tracker", bad, 0), "empty bd dropped");
        bad = report6547("b1", 50, CPU_BOARD_STATE_FREE);
        bad["u"] = "lots";
        check(!agg.onReport("de_tracker", bad, 0), "non-numeric u dropped");
        check(agg.boardCount() == 0, "no boards from bad reports");
    }

    // ---- bd dedupe: two modules on one board = one board ----
    {
        CCpuAggregator agg(cfg);
        check(agg.onReport("de_tracker",  report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 1000), "report m1 accepted");
        check(agg.onReport("de_camera",   report6547("b1", 60, CPU_BOARD_STATE_FREE, 30), 1000), "report m2 accepted");
        check(agg.boardCount() == 1, "two modules one board = one board");

        agg.tick(2000);
        const Json_de detail = agg.buildCpuDetailJson(2000);
        check(detail["b"].size() == 1, "CP.b has one entry");
        check(detail["b"][0]["u"].get<int>() == 60, "board u = max over module reports");
        check(detail["b"][0]["m"].size() == 2, "both modules listed in m");
        check(!detail["b"][0].contains("st"), "fresh board not stale");
    }

    // ---- transitions: one edge per change, any-board rule ----
    {
        CCpuAggregator agg(cfg);
        agg.onReport("de_tracker", report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 1000);
        agg.onReport("de_ai",      report6547("b2", 30, CPU_BOARD_STATE_FREE, 10), 1000);
        agg.tick(2000);

        // board b2 goes busy
        agg.onReport("de_ai", report6547("b2", 95, CPU_BOARD_STATE_SUPER_BUSY, 70), 3000);
        int ev = agg.tick(3000);
        check((ev & UNIT_CPU_EVENT_BECAME_SUPER_BUSY) != 0, "one busy board -> UNIT_BECAME_SUPER_BUSY");
        check(agg.unitState() == CpuBusyState::SUPER_BUSY, "unit state SUPER_BUSY");
        check(agg.unitUsagePercent() == 95, "unit u = max over boards");

        ev = agg.tick(4000);
        check((ev & UNIT_CPU_EVENT_BECAME_SUPER_BUSY) == 0, "no repeat while steady busy");

        // second board also busy -> still one unit state, no new edge
        agg.onReport("de_tracker", report6547("b1", 92, CPU_BOARD_STATE_SUPER_BUSY, 55), 5000);
        ev = agg.tick(5000);
        check((ev & (UNIT_CPU_EVENT_BECAME_SUPER_BUSY | UNIT_CPU_EVENT_BECAME_FREE)) == 0,
              "second busy board -> no extra transition");

        // b2 frees; b1 still busy -> still no edge
        agg.onReport("de_ai", report6547("b2", 20, CPU_BOARD_STATE_FREE, 5), 6000);
        ev = agg.tick(6000);
        check((ev & (UNIT_CPU_EVENT_BECAME_SUPER_BUSY | UNIT_CPU_EVENT_BECAME_FREE)) == 0,
              "one board still busy -> unit stays SUPER_BUSY");

        // both free -> exactly one BECAME_FREE
        agg.onReport("de_tracker", report6547("b1", 30, CPU_BOARD_STATE_FREE, 8), 7000);
        ev = agg.tick(7000);
        check((ev & UNIT_CPU_EVENT_BECAME_FREE) != 0, "all boards free -> UNIT_BECAME_FREE");
        ev = agg.tick(8000);
        check((ev & UNIT_CPU_EVENT_BECAME_FREE) == 0, "no repeat while steady free");
    }

    // ---- SUMMARY_DUE cadence ----
    {
        CCpuAggregator agg(cfg);
        agg.onReport("de_tracker", report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 1000);
        int ev = agg.tick(1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) == 0, "first tick baselines - no summary");
        ev = agg.tick(30 * 1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) == 0, "no summary before summary_s");
        ev = agg.tick(61 * 1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) != 0, "SUMMARY_DUE after summary_s");
        ev = agg.tick(100 * 1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) == 0, "summary timer resets");
        ev = agg.tick(121 * 1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) != 0, "SUMMARY_DUE every summary_s");
    }
    {
        CpuMonitorConfig off = cfg;
        off.summary_s = 0;
        CCpuAggregator agg(off);
        agg.onReport("de_tracker", report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 1000);
        agg.tick(1000);
        int ev = agg.tick(600 * 1000);
        check((ev & UNIT_CPU_EVENT_SUMMARY_DUE) == 0, "summary_s=0 never fires");
    }

    // ---- stale board: flagged, does not flip unit; busy board going
    //      silent frees the unit; 60 s removal recomputes ----
    {
        CCpuAggregator agg(cfg);
        agg.onReport("de_tracker", report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 1000);
        agg.onReport("de_ai",      report6547("b2", 20, CPU_BOARD_STATE_FREE, 5), 1000);
        agg.tick(2000);

        // b2 goes stale (>3*report_s silent) while FREE -> st=true, no transition
        const int ev = agg.tick(40 * 1000);
        const Json_de detail = agg.buildCpuDetailJson(40 * 1000);
        check(findBoard(detail, "b2") != nullptr, "stale board still listed");
        check(findBoard(detail, "b2")->contains("st"), "stale board flagged st=true");
        check((ev & (UNIT_CPU_EVENT_BECAME_SUPER_BUSY | UNIT_CPU_EVENT_BECAME_FREE)) == 0,
              "stale free board -> no transition");

        // b1 keeps reporting; b2 silent >60 s -> removed, recomputed
        agg.onReport("de_tracker", report6547("b1", 40, CPU_BOARD_STATE_FREE, 12), 55 * 1000);
        agg.tick(62 * 1000);
        const Json_de detail2 = agg.buildCpuDetailJson(62 * 1000);
        check(findBoard(detail2, "b2") == nullptr, "board removed after 60 s silence");
        check(agg.boardCount() == 1, "one board left");
        check(agg.unitUsagePercent() == 40, "unit u recomputed without b2");
    }
    {
        // busy board going stale frees the unit (stale boards never busy)
        CCpuAggregator agg(cfg);
        agg.onReport("de_ai", report6547("b2", 95, CPU_BOARD_STATE_SUPER_BUSY, 70), 1000);
        int ev = agg.tick(1000);
        check((ev & UNIT_CPU_EVENT_BECAME_SUPER_BUSY) != 0, "busy board -> unit busy");

        ev = agg.tick(40 * 1000);   // b2 stale (>30 s)
        check((ev & UNIT_CPU_EVENT_BECAME_FREE) != 0, "busy board stale -> unit back to FREE");
        check(agg.unitState() == CpuBusyState::FREE, "unit FREE while only stale board busy");
        const Json_de detail = agg.buildCpuDetailJson(40 * 1000);
        check(detail["b"][0]["s"].get<int>() == CPU_BOARD_STATE_FREE, "stale board state not busy");
    }

    // ---- 1008 payloads ----
    {
        CCpuAggregator agg(cfg);
        agg.onReport("de_tracker", report6547("b1", 96, CPU_BOARD_STATE_SUPER_BUSY, 80), 1000);
        agg.onReport("de_mavlink", report6547("b1", 96, CPU_BOARD_STATE_SUPER_BUSY, 20), 1000);
        agg.onReport("de_ai",      report6547("b2", 30, CPU_BOARD_STATE_FREE, 10), 1000);
        agg.tick(2000);

        const Json_de warn = agg.deBuildUnitCpuWarning(true, 2000);
        check(warn["EN"].get<int>() == ERROR_SYSTEM_LOAD, "warning EN=ERROR_SYSTEM_LOAD");
        check(warn["IT"].get<int>() == NOTIFICATION_TYPE_TELEMETRY, "warning IT=telemetry");
        check(warn["NT"].get<int>() == NOTIFICATION_TYPE_WARNING, "warning NT=WARNING");
        check(warn["MI"].get<int>() == MESSAGE_ID_CPU_SUPER_BUSY, "warning MI=1");
        check(warn["DS"].get<std::string>().find("b1") != std::string::npos, "warning DS names busy board");
        check(warn["DS"].get<std::string>().find("de_tracker") != std::string::npos,
              "warning DS names highest-pu module");
        check(warn["CP"]["u"].get<int>() == 96, "CP.u = max board u");
        check(warn["CP"]["s"].get<int>() == CPU_BOARD_STATE_SUPER_BUSY, "CP.s = busy");
        check(warn["CP"]["b"].size() == 2, "CP.b lists every board");
        check(hasBoard(warn["CP"], "b1") && hasBoard(warn["CP"], "b2"), "CP.b has both ids");
        check(findBoard(warn["CP"], "b1")->at("m").size() == 2, "busy board m lists both modules");

        const Json_de free = agg.deBuildUnitCpuWarning(false, 2000);
        check(free["NT"].get<int>() == NOTIFICATION_TYPE_NOTICE, "free NT=NOTICE");
        check(free["MI"].get<int>() == MESSAGE_ID_CPU_FREE, "free MI=2");

        const Json_de summary = agg.deBuildUnitCpuSummary(2000);
        check(summary["NT"].get<int>() == NOTIFICATION_TYPE_INFO, "summary NT=INFO");
        check(summary["MI"].get<int>() == MESSAGE_ID_CPU_SUMMARY, "summary MI=3");
        check(summary["CP"]["u"].get<int>() == 96, "summary CP.u = max board u");
        check(summary["CP"]["b"].size() == 2, "summary CP.b lists every board");
    }

    std::printf("=== %d checks, %d failed ===\n", g_checks, g_fails);
    return (g_fails == 0) ? 0 : 1;
}
