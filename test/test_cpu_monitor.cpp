/**
 * TASK-30 tests (GL-0037, port of TASK-25/GL-0032): /proc/stat aggregate
 * parsing (real format and old-kernel short lines), busy% between
 * samples, the FREE <-> SUPER_BUSY hysteresis (sustain window,
 * short-spike reset, dead band, exactly one edge report per change),
 * cpu_monitor config fallbacks, PLUS the new board-reporting pieces:
 * /proc/self/stat parse (comm with spaces and parentheses), process CPU
 * %, deBoardId() env override, and the DE_PROC_STAT_PATH sampler path.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../de_databus/de_cpu_monitor.hpp"

using namespace de::comm;

static int g_checks = 0;
static int g_fails = 0;

static void check (bool cond, const char* name)
{
    ++g_checks;
    if (cond) { std::printf("PASS %s\n", name); }
    else      { std::printf("FAIL %s\n", name); ++g_fails; }
}

/// synthetic /proc/stat with cumulative busy/idle ticks on the aggregate
/// line (per-core + junk lines included, as on a real system)
static std::string procStat (const uint64_t busy_ticks, const uint64_t idle_ticks)
{
    std::ostringstream s;
    s << "cpu  " << busy_ticks << " 0 0 " << idle_ticks << " 0 0 0 0 0 0\n"
      << "cpu0 " << busy_ticks << " 0 0 " << idle_ticks << " 0 0 0 0 0 0\n"
      << "intr 2005157 29 9 0 0\n"
      << "ctxt 4000000\n";
    return s.str();
}

/// feeds cumulative samples at ~usage% into a monitor
struct Feeder
{
    CCpuMonitor mon;
    uint64_t busy = 0;
    uint64_t idle = 0;

    Feeder (const CpuMonitorConfig& cfg) : mon(cfg) {}

    CpuTransition feed (const uint64_t busy_delta, const uint64_t idle_delta,
                        const uint64_t now_ms)
    {
        busy += busy_delta;
        idle += idle_delta;
        return mon.update(procStat(busy, idle), now_ms);
    }
};


// ------------------------------------------------------------- parsing

static void test_parse_real_format ()
{
    const std::string real =
        "cpu  4705 356 584 1629 23 0 195 0 0 0\n"
        "cpu0 2505 100 200 800 10 0 100 0 0 0\n"
        "cpu1 2200 256 384 829 13 0 95 0 0 0\n"
        "intr 2005157 29 9 0 0 0 0 0\n"
        "ctxt 4000000\n"
        "btime 1700000000\n"
        "processes 1500\n"
        "procs_running 2\n"
        "procs_blocked 0\n";
    CpuTimes t;
    check(deParseProcStatCpu(real, t), "parse real /proc/stat");
    check(t.total == 4705+356+584+1629+23+0+195+0+0+0, "total = sum of all columns");
    check(t.idle_all == 1629 + 23, "idle_all = idle + iowait");

    // old kernel: only user/nice/system/idle - no iowait, no steal/guest
    CpuTimes t2;
    check(deParseProcStatCpu("cpu  100 0 50 900\n", t2), "parse 4-column cpu line");
    check(t2.total == 1050 && t2.idle_all == 900, "4-column totals, iowait=0");

    check(!deParseProcStatCpu("intr 0\nctxt 1\n", t2), "no cpu line -> false");
    check(!deParseProcStatCpu("cpu  1 2\n", t2), "too few columns -> false");
    check(!deParseProcStatCpu("cpu0 1 1 1 1 0\n", t2), "per-core only -> false");
    check(!deParseProcStatCpu("", t2), "empty -> false");
}


static void test_usage_percent ()
{
    CpuTimes a, b;
    check(deParseProcStatCpu(procStat(0, 0), a), "baseline parse");
    check(deParseProcStatCpu(procStat(90, 10), b), "second parse");
    const double u = deCpuUsagePercent(a, b);
    check(u > 89.9 && u < 90.1, "usage ~90%");
    check(deCpuUsagePercent(b, b) < 0.0, "no progress -> -1");
    check(deCpuUsagePercent(b, a) < 0.0, "counter went back -> -1");
}


// ---------------------------------------------------------- hysteresis

static void test_super_busy_after_sustain ()
{
    Feeder f {CpuMonitorConfig()};
    check(f.feed(0, 100, 1000) == CpuTransition::NONE, "baseline -> NONE");
    check(f.feed(95, 5, 2000) == CpuTransition::NONE, "95% t+1s: pending, NONE");
    check(f.feed(95, 5, 4000) == CpuTransition::NONE, "95% t+3s: held 3s < 5s");
    check(f.feed(95, 5, 6000) == CpuTransition::NONE, "95% t+5s: held 5s? no, 4s");
    check(f.feed(95, 5, 7000) == CpuTransition::BECAME_SUPER_BUSY, "95% sustained 5s -> SUPER_BUSY");
    check(f.mon.state() == CpuBusyState::SUPER_BUSY, "state SUPER_BUSY");
    check(f.mon.lastUsagePercent() > 94.9, "last usage recorded");
    // exactly one transition per change - steady busy reports nothing
    check(f.feed(95, 5, 8000) == CpuTransition::NONE, "steady busy -> NONE");
    check(f.feed(99, 1, 9000) == CpuTransition::NONE, "still busy -> NONE");
}


static void test_short_spike_no_transition ()
{
    Feeder f {CpuMonitorConfig()};
    f.feed(0, 100, 1000);                            // baseline
    check(f.feed(95, 5, 2000) == CpuTransition::NONE, "spike starts pending");
    check(f.feed(10, 90, 3000) == CpuTransition::NONE, "drop under -> pending reset");
    // fresh window must run the full sustain again
    check(f.feed(95, 5, 4000) == CpuTransition::NONE, "new window starts");
    check(f.feed(95, 5, 8000) == CpuTransition::NONE, "4s held < 5s");
    check(f.feed(95, 5, 9000) == CpuTransition::BECAME_SUPER_BUSY, "5s from new window -> SUPER_BUSY");
    check(f.mon.state() == CpuBusyState::SUPER_BUSY, "state SUPER_BUSY");
}


static void test_dead_band_holds_state ()
{
    Feeder f {CpuMonitorConfig()};
    f.feed(0, 100, 1000);
    // 80% sits between free(70) and super(90): FREE stays FREE forever
    for (int i = 0; i < 20; ++i)
        check(f.feed(80, 20, 2000 + 1000*i) == CpuTransition::NONE, "80% in FREE -> NONE");
    check(f.mon.state() == CpuBusyState::FREE, "still FREE");
}


static void test_free_after_sustain ()
{
    Feeder f {CpuMonitorConfig()};
    f.feed(0, 100, 1000);
    for (int i = 0; i < 6; ++i) f.feed(95, 5, 2000 + 1000*i);   // SUPER_BUSY at t=7000
    check(f.mon.state() == CpuBusyState::SUPER_BUSY, "setup: SUPER_BUSY");

    check(f.feed(50, 50, 8000) == CpuTransition::NONE, "50% t+1s: pending free");
    check(f.feed(80, 20, 9000) == CpuTransition::NONE, "80% dead band -> reset, still SUPER_BUSY");
    check(f.mon.state() == CpuBusyState::SUPER_BUSY, "dead band keeps SUPER_BUSY");

    check(f.feed(50, 50, 10000) == CpuTransition::NONE, "50% again: new window");
    check(f.feed(50, 50, 14000) == CpuTransition::NONE, "4s held < 5s");
    check(f.feed(50, 50, 15000) == CpuTransition::BECAME_FREE, "50% sustained 5s -> FREE");
    check(f.mon.state() == CpuBusyState::FREE, "state FREE");
    check(f.feed(50, 50, 16000) == CpuTransition::NONE, "steady free -> NONE");
}


static void test_threshold_edges ()
{
    CpuMonitorConfig cfg;
    cfg.super_busy_percent = 90;
    cfg.free_percent       = 70;
    cfg.sustain_s          = 2;
    Feeder f (cfg);
    f.feed(0, 100, 1000);
    // exactly 90% qualifies (>=)
    check(f.feed(90, 10, 2000) == CpuTransition::NONE, "90% edge: pending");
    check(f.feed(90, 10, 4000) == CpuTransition::BECAME_SUPER_BUSY, "90% held 2s -> SUPER_BUSY");
    // exactly 70% qualifies (<=)
    check(f.feed(70, 30, 5000) == CpuTransition::NONE, "70% edge: pending");
    check(f.feed(70, 30, 7000) == CpuTransition::BECAME_FREE, "70% held 2s -> FREE");
    // 89% just under super_busy: nothing
    check(f.feed(89, 11, 8000) == CpuTransition::NONE, "89% -> NONE");
    check(f.feed(89, 11, 12000) == CpuTransition::NONE, "89% long -> still NONE");
}


static void test_first_sample_and_bad_input ()
{
    Feeder f {CpuMonitorConfig()};
    check(f.mon.update("garbage", 1000) == CpuTransition::NONE, "unparsable -> NONE");
    check(f.mon.update(procStat(50, 50), 2000) == CpuTransition::NONE, "first real sample = baseline");
    check(f.mon.update("garbage", 3000) == CpuTransition::NONE, "unparsable keeps baseline");
    check(f.feed(95, 5, 4000) == CpuTransition::NONE, "usage vs kept baseline, pending");
}


static void test_update_from_times ()
{
    // the sampler path: caller already parsed, monitor must behave the
    // same as feeding text
    CCpuMonitor mon;
    CpuTimes t1, t2;
    check(deParseProcStatCpu(procStat(0, 100), t1), "times baseline parse");
    check(deParseProcStatCpu(procStat(95, 105), t2), "times second parse");
    check(mon.updateFromTimes(t1, 1000) == CpuTransition::NONE, "first times = baseline");
    check(mon.updateFromTimes(t2, 2000) == CpuTransition::NONE, "usage 95%: pending");
    check(mon.lastUsagePercent() > 94.9, "last usage via updateFromTimes");
}


// -------------------------------------------------------------- config

static void test_config_defaults_and_fallback ()
{
    check(deParseCpuMonitorConfig(Json_de::object()).super_busy_percent == 90,
          "absent object -> default 90");
    check(deParseCpuMonitorConfig(Json_de::parse("{\"cpu_monitor\":\"x\"}")).free_percent == 70,
          "non-object cpu_monitor -> defaults");

    CpuMonitorConfig ok = deParseCpuMonitorConfig(Json_de::parse(
        "{\"cpu_monitor\":{\"enabled\":false,\"super_busy_percent\":80,"
        "\"free_percent\":50,\"sustain_s\":2,\"report_s\":3,\"summary_s\":0}}"));
    check(!ok.enabled && ok.super_busy_percent == 80 && ok.free_percent == 50 && ok.sustain_s == 2
          && ok.report_s == 3 && ok.summary_s == 0,
          "valid object parsed incl. report_s/summary_s");

    CpuMonitorConfig def = deParseCpuMonitorConfig(Json_de::object());
    check(def.report_s == 10 && def.summary_s == 60, "report_s/summary_s defaults 10/60");

    // free >= super -> ALL defaults, never a half-applied pair
    CpuMonitorConfig bad = deParseCpuMonitorConfig(Json_de::parse(
        "{\"cpu_monitor\":{\"super_busy_percent\":80,\"free_percent\":95,\"sustain_s\":2}}"));
    check(bad.super_busy_percent == 90 && bad.free_percent == 70 && bad.sustain_s == 5
          && bad.report_s == 10 && bad.summary_s == 60,
          "free >= super -> defaults");

    CpuMonitorConfig oob = deParseCpuMonitorConfig(Json_de::parse(
        "{\"cpu_monitor\":{\"super_busy_percent\":250}}"));
    check(oob.super_busy_percent == 90 && oob.free_percent == 70, "out-of-range -> defaults");

    CpuMonitorConfig bad_report = deParseCpuMonitorConfig(Json_de::parse(
        "{\"cpu_monitor\":{\"report_s\":0}}"));
    check(bad_report.report_s == 10, "report_s 0 -> defaults");

    CpuMonitorConfig bad_summary = deParseCpuMonitorConfig(Json_de::parse(
        "{\"cpu_monitor\":{\"summary_s\":-5}}"));
    check(bad_summary.summary_s == 60, "summary_s <0 -> defaults");
}


// ------------------------------------------------------- board/process

static void test_parse_proc_self_stat ()
{
    // real format: pid (comm) state ppid ... utime(14) stime(15) ...
    // comm may contain spaces and parens - parse must anchor on last ')'
    ProcCpuTimes t;
    check(deParseProcSelfStatCpu(
        "1234 (de tracker) S 1 234 567 0 -1 4194304 100 0 0 0 10 5 0 0 20 0 3 0 0 17 0 0 0 0 0 0\n", t),
        "parse /proc/self/stat with '(de tracker)' comm");
    check(t.busy_jiffies == 15, "busy = utime + stime");

    ProcCpuTimes t2;
    check(deParseProcSelfStatCpu(
        "99 (weird)name)x) R 5 6 7 0 0 0 0 0 0 0 40 60 0 0 0 0 0 0\n", t2),
        "comm containing ')' still parses");
    check(t2.busy_jiffies == 100, "busy jiffies after ')' comm");

    check(!deParseProcSelfStatCpu("no parens at all\n", t2), "no comm -> false");
    check(!deParseProcSelfStatCpu("1 (x) S 1 2\n", t2), "too few fields -> false");
}


static void test_process_cpu_percent ()
{
    ProcCpuTimes a {100}, b {150};   // 50 jiffies in 1000ms at 100 ticks/s
    const double u = deProcessCpuUsagePercent(a, b, 1000, 100);
    check(u > 49.9 && u < 50.1, "proc usage ~50%");
    check(deProcessCpuUsagePercent(a, a, 1000, 100) < 0.0, "no jiffies -> -1");
    check(deProcessCpuUsagePercent(a, b, 0, 100) < 0.0, "zero elapsed -> -1");
    check(deProcessCpuUsagePercent(a, b, 1000, 0) < 0.0, "zero ticks -> -1");
    // 50 jiffies over 1000ms at 200 ticks/s = 25%
    const double u2 = deProcessCpuUsagePercent(a, b, 1000, 200);
    check(u2 > 24.9 && u2 < 25.1, "usage scales with ticks_per_second");
}


static void test_board_id_env_wins ()
{
    setenv("DE_BOARD_ID", "board-test-1", 1);
    check(deBoardId() == "board-test-1", "DE_BOARD_ID override wins");
    setenv("DE_BOARD_ID", "board-test-2", 1);
    check(deBoardId() == "board-test-2", "env re-read per call");

    unsetenv("DE_BOARD_ID");
    const std::string real_id = deBoardId();
    check(!real_id.empty(), "fallback (machine-id/hostname) non-empty");
    check(deBoardId() == real_id, "fallback is cached/stable");
}


static void test_sampler_path_override ()
{
    // write a fake busy /proc/stat, point DE_PROC_STAT_PATH at it
    const char* fake_path = "/tmp/de_test_proc_stat_cpu.txt";
    {
        std::ofstream f (fake_path);
        f << procStat(1234, 567);
    }
    setenv("DE_PROC_STAT_PATH", fake_path, 1);
    CpuTimes t;
    check(deReadProcStatCpu(t), "deReadProcStatCpu reads env path");
    check(t.total == 1234 + 567 && t.idle_all == 567, "env-path totals correct");
    unsetenv("DE_PROC_STAT_PATH");

    ProcCpuTimes pt;
    check(deReadProcSelfStatCpu(pt), "deReadProcSelfStatCpu reads real self stat");
}


int main ()
{
    test_parse_real_format();
    test_usage_percent();
    test_super_busy_after_sustain();
    test_short_spike_no_transition();
    test_dead_band_holds_state();
    test_free_after_sustain();
    test_threshold_edges();
    test_first_sample_and_bad_input();
    test_update_from_times();
    test_config_defaults_and_fallback();
    test_parse_proc_self_stat();
    test_process_cpu_percent();
    test_board_id_env_wins();
    test_sampler_path_override();

    std::printf("=== %d checks, %d failed ===\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
