/**
 * @file test_track_manager.cpp
 * @brief standalone tests for helpers/track_manager.hpp.
 *
 * Build & run:  make -C test   &&   ./test/test_track_manager
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../helpers/obs.hpp"
#include "../helpers/track_manager.hpp"

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "ASSERTION FAILED: " << #condition << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))
#define ASSERT_EQ(val1, val2) ASSERT_TRUE((val1) == (val2))

using de::ai::Observation;
using de::ai::ObservationFrame;
using de::ai::TrackManager;
using de::ai::TrackManagerConfig;

static Observation det (const std::string& cls, double x, double y, double w = 0.1, double h = 0.2, double conf = 0.9)
{
    Observation o;
    o.cls = cls; o.conf = conf;
    o.x = x; o.y = y; o.w = w; o.h = h;
    return o;
}

static ObservationFrame frame (const std::vector<Observation>& obs, int64_t tm = 1)
{
    ObservationFrame f;
    f.cam_id = "CAM";
    f.tm_capture = tm;
    f.obs = obs;
    return f;
}

static TrackManagerConfig cfg (int confirm = 3, int misses = 5, double iou = 0.3, double conf = 0.25)
{
    TrackManagerConfig c;
    c.confirm_hits = confirm; c.max_misses = misses; c.iou_min = iou; c.min_conf = conf;
    return c;
}

// the trk of the published observation nearest to (x,y); -1 when none within 0.01
static int64_t trkNear (const ObservationFrame& f, double x, double y)
{
    double best = 1e9;
    int64_t trk = -1;
    for (const auto& o : f.obs)
    {
        const double d = std::fabs(o.x - x) + std::fabs(o.y - y);
        if (d < best) { best = d; trk = o.has_trk ? o.trk : -2; }
    }
    return (best < 0.01) ? trk : -1;
}


bool test_confirmation ()
{
    std::cout << "Testing: confirm after confirm_hits, only then published..." << std::endl;

    TrackManager tm(cfg(3));

    ObservationFrame f1 = tm.update(frame({ det("person", 0.30, 0.30) }, 10));
    ObservationFrame f2 = tm.update(frame({ det("person", 0.31, 0.30) }, 20));
    ASSERT_EQ(f1.obs.size(), (size_t) 0);
    ASSERT_EQ(f2.obs.size(), (size_t) 0);
    ASSERT_EQ(f1.cam_id, std::string("CAM"));
    ASSERT_EQ(f2.tm_capture, (int64_t) 20);

    ObservationFrame f3 = tm.update(frame({ det("person", 0.32, 0.30) }, 30));
    ASSERT_EQ(f3.obs.size(), (size_t) 1);
    ASSERT_TRUE(f3.obs[0].has_trk);
    ASSERT_TRUE(f3.obs[0].trk > 0);
    ASSERT_EQ(f3.obs[0].cls, std::string("person"));
    ASSERT_TRUE(std::fabs(f3.obs[0].x - 0.32) < 1e-12);   // raw detection box, not smoothed

    // confirm_hits = 1 publishes on the first frame
    TrackManager one(cfg(1));
    ASSERT_EQ(one.update(frame({ det("car", 0.5, 0.5) })).obs.size(), (size_t) 1);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_false_positive_never_published ()
{
    std::cout << "Testing: a 1-frame false positive is never published..." << std::endl;

    TrackManager tm(cfg(3));
    int published = 0;

    published += (int) tm.update(frame({ det("person", 0.60, 0.60) })).obs.size();   // ghost
    for (int i = 0; i < 20; ++i)
    {
        published += (int) tm.update(frame({})).obs.size();
    }
    ASSERT_EQ(published, 0);
    ASSERT_EQ(tm.trackCount(), (size_t) 0);

    // a ghost that blinks every other frame never accumulates hits
    for (int i = 0; i < 20; ++i)
    {
        std::vector<Observation> obs;
        if (i % 2 == 0) obs.push_back(det("person", 0.2, 0.2));
        published += (int) tm.update(frame(obs)).obs.size();
    }
    ASSERT_EQ(published, 0);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_miss_tolerance ()
{
    std::cout << "Testing: id survives <= max_misses missed frames, then is dropped..." << std::endl;

    TrackManager tm(cfg(3, 4));

    int64_t id = -1;
    for (int i = 0; i < 5; ++i)
    {
        ObservationFrame f = tm.update(frame({ det("person", 0.30 + 0.01 * i, 0.30) }));
        if (!f.obs.empty()) id = f.obs[0].trk;
    }
    ASSERT_TRUE(id > 0);

    // 4 misses (== max_misses), the object comes back where it should be
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_EQ(tm.update(frame({})).obs.size(), (size_t) 0);
    }
    ObservationFrame back = tm.update(frame({ det("person", 0.30 + 0.01 * 9, 0.30) }));
    ASSERT_EQ(back.obs.size(), (size_t) 1);
    ASSERT_EQ(back.obs[0].trk, id);

    // 5 misses (> max_misses): the track is gone, a new object gets a new, larger id
    for (int i = 0; i < 5; ++i) tm.update(frame({}));
    ASSERT_EQ(tm.trackCount(), (size_t) 0);

    int64_t newid = -1;
    for (int i = 0; i < 3; ++i)
    {
        ObservationFrame f = tm.update(frame({ det("person", 0.39, 0.30) }));
        if (!f.obs.empty()) newid = f.obs[0].trk;
    }
    ASSERT_TRUE(newid > id);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_crossing_objects_keep_ids ()
{
    std::cout << "Testing: two crossing objects keep their ids..." << std::endl;

    TrackManager tm(cfg(3, 5));

    int64_t a = -1, b = -1;
    int checked = 0;

    for (int i = 0; i < 40; ++i)
    {
        const double ax = 0.05 + 0.02 * i, ay = 0.40;
        const double bx = 0.75 - 0.02 * i, by = 0.42;

        std::vector<Observation> obs;
        // alternate the order so ids cannot follow list position
        if (i % 2 == 0) { obs.push_back(det("person", ax, ay)); obs.push_back(det("person", bx, by)); }
        else            { obs.push_back(det("person", bx, by)); obs.push_back(det("person", ax, ay)); }

        ObservationFrame f = tm.update(frame(obs));
        if (f.obs.size() != 2) { ASSERT_TRUE(i < 2); continue; }   // only the first frames are unconfirmed

        int64_t ta = trkNear(f, ax, ay), tb = trkNear(f, bx, by);
        ASSERT_TRUE(ta > 0 && tb > 0);
        ASSERT_TRUE(ta != tb);

        if (a < 0) { a = ta; b = tb; }
        ASSERT_EQ(ta, a);
        ASSERT_EQ(tb, b);
        ++checked;
    }
    ASSERT_TRUE(checked >= 35);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_classes_and_gates ()
{
    std::cout << "Testing: classes never share a track; iou gate; min_conf..." << std::endl;

    TrackManager tm(cfg(2, 3, 0.3, 0.25));

    // same place, different classes -> two tracks
    tm.update(frame({ det("person", 0.3, 0.3), det("car", 0.3, 0.3) }));
    ObservationFrame f = tm.update(frame({ det("person", 0.3, 0.3), det("car", 0.3, 0.3) }));
    ASSERT_EQ(f.obs.size(), (size_t) 2);
    ASSERT_TRUE(f.obs[0].trk != f.obs[1].trk);

    // a jump that does not overlap the old box is a new object, not the same track
    TrackManager g(cfg(1, 3, 0.3));
    int64_t first = g.update(frame({ det("person", 0.1, 0.1) })).obs[0].trk;
    int64_t second = g.update(frame({ det("person", 0.7, 0.7) })).obs[0].trk;
    ASSERT_TRUE(second != first);

    // low confidence detections never start or extend a track
    TrackManager c(cfg(1, 3, 0.3, 0.5));
    ASSERT_EQ(c.update(frame({ det("person", 0.3, 0.3, 0.1, 0.2, 0.4) })).obs.size(), (size_t) 0);
    ASSERT_EQ(c.trackCount(), (size_t) 0);

    // attrs / cls_id ride through
    TrackManager k(cfg(1));
    Observation o = det("hotspot", 0.2, 0.2);
    o.has_cls_id = true; o.cls_id = 7;
    o.attrs = Json_de::parse(R"({"temp_c":41.5})");
    ObservationFrame kf = k.update(frame({ o }));
    ASSERT_EQ(kf.obs.size(), (size_t) 1);
    ASSERT_TRUE(kf.obs[0].has_cls_id && kf.obs[0].cls_id == 7);
    ASSERT_TRUE(std::fabs(kf.obs[0].attrs["temp_c"].get<double>() - 41.5) < 1e-9);

    // the tracked output is a valid wire frame
    Json_de wire; std::string err;
    ASSERT_TRUE(de::ai::obsBuild(kf, wire, err));
    ASSERT_TRUE(wire["obs"][0].contains("trk"));

    std::cout << "passed" << std::endl;
    return true;
}


bool test_ids_monotonic_across_reset ()
{
    std::cout << "Testing: ids are never reused, not even after reset()..." << std::endl;

    TrackManager tm(cfg(1));
    int64_t id1 = tm.update(frame({ det("person", 0.3, 0.3) })).obs[0].trk;
    tm.reset();
    ASSERT_EQ(tm.trackCount(), (size_t) 0);
    int64_t id2 = tm.update(frame({ det("person", 0.3, 0.3) })).obs[0].trk;
    ASSERT_TRUE(id2 > id1);

    // empty frames are fine
    ASSERT_EQ(tm.update(frame({})).obs.size(), (size_t) 0);

    std::cout << "passed" << std::endl;
    return true;
}


bool test_benchmark_50_boxes ()
{
    std::cout << "Testing: per-frame cost for 50 boxes..." << std::endl;

    TrackManager tm(cfg(3, 5));

    const int N = 50;
    const int FRAMES = 2000;
    srand(7);

    std::vector<double> bx(N), by(N), vx(N), vy(N);
    for (int i = 0; i < N; ++i)
    {
        bx[i] = 0.05 + 0.9 * (rand() / (double) RAND_MAX) * 0.85;
        by[i] = 0.05 + 0.9 * (rand() / (double) RAND_MAX) * 0.85;
        vx[i] = ((rand() / (double) RAND_MAX) - 0.5) * 0.004;
        vy[i] = ((rand() / (double) RAND_MAX) - 0.5) * 0.004;
    }

    double total_us = 0.0, worst_us = 0.0;
    size_t published = 0;

    for (int t = 0; t < FRAMES; ++t)
    {
        std::vector<Observation> obs;
        for (int i = 0; i < N; ++i)
        {
            bx[i] += vx[i]; by[i] += vy[i];
            if (bx[i] < 0.0 || bx[i] > 0.88) vx[i] = -vx[i];
            if (by[i] < 0.0 || by[i] > 0.78) vy[i] = -vy[i];
            obs.push_back(det((i % 2) ? "person" : "car", std::fmin(0.88, std::fmax(0.0, bx[i])), std::fmin(0.78, std::fmax(0.0, by[i]))));
        }
        ObservationFrame f = frame(obs, t);

        const auto t0 = std::chrono::steady_clock::now();
        ObservationFrame r = tm.update(f);
        const auto t1 = std::chrono::steady_clock::now();

        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        total_us += us;
        if (us > worst_us) worst_us = us;
        published += r.obs.size();
    }

    const double avg_us = total_us / FRAMES;
    std::cout << "  50 boxes/frame, " << FRAMES << " frames: avg " << avg_us << " us, worst " << worst_us
              << " us, published " << published << std::endl;

    ASSERT_TRUE(published > 0);
    ASSERT_TRUE(avg_us <= 1000.0);

    std::cout << "passed" << std::endl;
    return true;
}


int main ()
{
    bool ok = true;

    ok &= test_confirmation();
    ok &= test_false_positive_never_published();
    ok &= test_miss_tolerance();
    ok &= test_crossing_objects_keep_ids();
    ok &= test_classes_and_gates();
    ok &= test_ids_monotonic_across_reset();
    ok &= test_benchmark_50_boxes();

    if (ok)
    {
        std::cout << std::endl << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    std::cout << std::endl << "TESTS FAILED" << std::endl;
    return 1;
}
