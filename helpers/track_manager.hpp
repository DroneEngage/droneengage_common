#ifndef DE_TRACK_MANAGER_H_
#define DE_TRACK_MANAGER_H_

/**
 * @brief Box-only multi-object track manager (ByteTrack-lite).
 *
 * Turns the per-frame observations of one camera (helpers/obs.hpp) into
 * observations that carry a stable "trk" id and only exist for CONFIRMED
 * tracks. It is the ONLY temporal filter of the perception stack: a
 * backend reports everything it sees every frame, this class decides what
 * is real.
 *
 *  - association: per class, optimal assignment (Hungarian) on 1-IoU
 *    between each track's predicted box (constant-velocity centre) and the
 *    frame's detections, gated by iou_min;
 *  - a new track is TENTATIVE: it is confirmed after confirm_hits matched
 *    frames in a row, and dropped on its first miss, so a one-frame false
 *    positive is never published;
 *  - a confirmed track survives max_misses consecutive missed frames
 *    (keeping its id) and is dropped on the next one;
 *  - ids are positive, increase monotonically and are never reused, not
 *    even after reset();
 *  - only confirmed tracks seen in THIS frame are published, with the raw
 *    detection box (boxes are not smoothed).
 *
 * No video, no clock: velocity is per processed frame. One instance per
 * camera (cam_id is copied through, not interpreted). Not thread safe.
 * Pure header; standalone test in de_common/test/test_track_manager.cpp.
 */

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "obs.hpp"

namespace de
{
namespace ai
{

    struct TrackManagerConfig
    {
        int    confirm_hits = 3;     // matched frames in a row before a track is published
        int    max_misses   = 5;     // consecutive missed frames a confirmed track survives
        double iou_min      = 0.3;   // minimum IoU for a detection to join a track
        double min_conf     = 0.25;  // detections below this never start or extend a track
    };

    namespace track_detail
    {
        struct Box { double x, y, w, h; };

        inline double iou (const Box& a, const Box& b)
        {
            const double x1 = std::max(a.x, b.x);
            const double y1 = std::max(a.y, b.y);
            const double x2 = std::min(a.x + a.w, b.x + b.w);
            const double y2 = std::min(a.y + a.h, b.y + b.h);
            const double iw = x2 - x1;
            const double ih = y2 - y1;
            if (iw <= 0.0 || ih <= 0.0) return 0.0;

            const double inter = iw * ih;
            const double uni = a.w * a.h + b.w * b.h - inter;
            return (uni > 0.0) ? inter / uni : 0.0;
        }

        /**
         * Minimum-cost assignment of n rows to m columns (n <= m), O(n^2 m).
         * cost is row-major n*m. Returns, for every row, its column.
         */
        inline std::vector<int> hungarian (const std::vector<double>& cost, int n, int m)
        {
            const double INF = std::numeric_limits<double>::infinity();
            std::vector<double> u(n + 1, 0.0), v(m + 1, 0.0);
            std::vector<int> p(m + 1, 0), way(m + 1, 0);

            for (int i = 1; i <= n; ++i)
            {
                p[0] = i;
                int j0 = 0;
                std::vector<double> minv(m + 1, INF);
                std::vector<char> used(m + 1, 0);

                do
                {
                    used[j0] = 1;
                    const int i0 = p[j0];
                    double delta = INF;
                    int j1 = 0;

                    for (int j = 1; j <= m; ++j)
                    {
                        if (used[j]) continue;
                        const double cur = cost[(i0 - 1) * m + (j - 1)] - u[i0] - v[j];
                        if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                        if (minv[j] < delta) { delta = minv[j]; j1 = j; }
                    }

                    for (int j = 0; j <= m; ++j)
                    {
                        if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                        else minv[j] -= delta;
                    }

                    j0 = j1;
                } while (p[j0] != 0);

                do
                {
                    const int j1 = way[j0];
                    p[j0] = p[j1];
                    j0 = j1;
                } while (j0 != 0);
            }

            std::vector<int> row_to_col(n, -1);
            for (int j = 1; j <= m; ++j)
            {
                if (p[j] != 0) row_to_col[p[j] - 1] = j - 1;
            }
            return row_to_col;
        }
    }


    class TrackManager
    {
    public:
        TrackManager () {}
        explicit TrackManager (const TrackManagerConfig& config) : m_config(config) { sanitize(); }

        void setConfig (const TrackManagerConfig& config) { m_config = config; sanitize(); }
        const TrackManagerConfig& config () const { return m_config; }

        // forget every track (detection paused, camera changed); ids keep counting up
        void reset () { m_tracks.clear(); }

        size_t trackCount () const { return m_tracks.size(); }

        /**
         * Feed one processed frame. Returns the same frame (cam_id,
         * tm_capture) holding only confirmed, currently matched
         * observations, each with its "trk" set. The result is empty when
         * nothing is confirmed yet.
         */
        ObservationFrame update (const ObservationFrame& frame)
        {
            using track_detail::Box;

            ObservationFrame out;
            out.cam_id = frame.cam_id;
            out.tm_capture = frame.tm_capture;

            std::vector<const Observation*> dets;
            for (const Observation& o : frame.obs)
            {
                if (o.conf >= m_config.min_conf) dets.push_back(&o);
            }

            std::vector<int> det_track(dets.size(), -1);   // index into m_tracks
            std::vector<char> track_matched(m_tracks.size(), 0);

            // association, one class at a time
            std::vector<std::string> classes;
            for (const Observation* d : dets)
            {
                if (std::find(classes.begin(), classes.end(), d->cls) == classes.end()) classes.push_back(d->cls);
            }

            for (const std::string& cls : classes)
            {
                std::vector<int> di, ti;
                for (size_t i = 0; i < dets.size(); ++i) if (dets[i]->cls == cls) di.push_back((int) i);
                for (size_t t = 0; t < m_tracks.size(); ++t) if (m_tracks[t].cls == cls) ti.push_back((int) t);
                if (di.empty() || ti.empty()) continue;

                // rows = the smaller side so n <= m
                const bool rows_are_tracks = ti.size() <= di.size();
                const int n = rows_are_tracks ? (int) ti.size() : (int) di.size();
                const int m = rows_are_tracks ? (int) di.size() : (int) ti.size();

                const double BIG = 1e6;
                std::vector<double> cost((size_t) n * m, BIG);
                for (int a = 0; a < n; ++a)
                {
                    for (int b = 0; b < m; ++b)
                    {
                        const int t = rows_are_tracks ? ti[a] : ti[b];
                        const int d = rows_are_tracks ? di[b] : di[a];
                        const Box det = { dets[d]->x, dets[d]->y, dets[d]->w, dets[d]->h };
                        const double v = track_detail::iou(m_tracks[t].predicted(), det);
                        if (v >= m_config.iou_min) cost[(size_t) a * m + b] = 1.0 - v;
                    }
                }

                const std::vector<int> assign = track_detail::hungarian(cost, n, m);
                for (int a = 0; a < n; ++a)
                {
                    const int b = assign[a];
                    if (b < 0 || cost[(size_t) a * m + b] >= BIG) continue;

                    const int t = rows_are_tracks ? ti[a] : ti[b];
                    const int d = rows_are_tracks ? di[b] : di[a];
                    det_track[d] = t;
                    track_matched[t] = 1;
                }
            }

            // matched tracks take the detection
            for (size_t d = 0; d < dets.size(); ++d)
            {
                if (det_track[d] < 0) continue;
                Track& tr = m_tracks[det_track[d]];
                tr.update(*dets[d]);
                if (!tr.confirmed && tr.hits >= m_config.confirm_hits) tr.confirmed = true;
            }

            // unmatched tracks: tentative die at once, confirmed after max_misses
            for (size_t t = 0; t < m_tracks.size(); ++t)
            {
                if (track_matched[t]) continue;
                m_tracks[t].miss();
            }

            // new tracks from unmatched detections
            std::vector<Track> born;
            for (size_t d = 0; d < dets.size(); ++d)
            {
                if (det_track[d] >= 0) continue;
                Track tr;
                tr.id = m_next_id++;
                tr.cls = dets[d]->cls;
                tr.update(*dets[d]);
                if (tr.hits >= m_config.confirm_hits) tr.confirmed = true;
                born.push_back(tr);
            }

            // publish, in detection order: every detection that belongs to a confirmed track
            for (size_t d = 0; d < dets.size(); ++d)
            {
                const Track* tr = nullptr;
                if (det_track[d] >= 0) tr = &m_tracks[det_track[d]];
                if (tr == nullptr || !tr->confirmed) continue;

                Observation o = *dets[d];
                o.has_trk = true;
                o.trk = tr->id;
                out.obs.push_back(o);
            }

            // new tracks that confirm on their first frame (confirm_hits <= 1)
            {
                size_t bi = 0;
                for (size_t d = 0; d < dets.size(); ++d)
                {
                    if (det_track[d] >= 0) continue;
                    const Track& tr = born[bi++];
                    if (!tr.confirmed) continue;

                    Observation o = *dets[d];
                    o.has_trk = true;
                    o.trk = tr.id;
                    out.obs.push_back(o);
                }
            }

            // drop dead tracks, then add the new ones
            std::vector<Track> alive;
            for (const Track& tr : m_tracks)
            {
                if (tr.dead(m_config)) continue;
                alive.push_back(tr);
            }
            for (const Track& tr : born) alive.push_back(tr);
            m_tracks.swap(alive);

            return out;
        }

    private:
        struct Track
        {
            int64_t     id = 0;
            std::string cls;
            double      x = 0, y = 0, w = 0, h = 0;   // last matched box
            double      vx = 0, vy = 0;                // centre velocity per frame
            int         hits = 0;                      // consecutive matched frames
            int         misses = 0;                    // consecutive missed frames
            bool        confirmed = false;

            // where the box should be now (constant-velocity centre, size kept)
            track_detail::Box predicted () const
            {
                const double steps = (double) (misses + 1);
                return { x + vx * steps, y + vy * steps, w, h };
            }

            void update (const Observation& o)
            {
                if (hits > 0 || misses > 0)
                {
                    const double steps = (double) (misses + 1);
                    const double ncx = o.x + o.w * 0.5, ncy = o.y + o.h * 0.5;
                    const double pcx = x + w * 0.5,     pcy = y + h * 0.5;
                    const double mvx = (ncx - pcx) / steps, mvy = (ncy - pcy) / steps;
                    // smooth the velocity a little so one jittery frame does not throw the prediction
                    vx = (hits > 0) ? 0.5 * vx + 0.5 * mvx : mvx;
                    vy = (hits > 0) ? 0.5 * vy + 0.5 * mvy : mvy;
                }
                x = o.x; y = o.y; w = o.w; h = o.h;
                hits += 1;
                misses = 0;
            }

            void miss ()
            {
                misses += 1;
                hits = 0;
            }

            bool dead (const TrackManagerConfig& c) const
            {
                if (misses == 0) return false;
                if (!confirmed) return true;
                return misses > c.max_misses;
            }
        };

        void sanitize ()
        {
            if (m_config.confirm_hits < 1) m_config.confirm_hits = 1;
            if (m_config.max_misses < 0) m_config.max_misses = 0;
            if (m_config.iou_min < 0.0) m_config.iou_min = 0.0;
            if (m_config.iou_min > 1.0) m_config.iou_min = 1.0;
            if (m_config.min_conf < 0.0) m_config.min_conf = 0.0;
            if (m_config.min_conf > 1.0) m_config.min_conf = 1.0;
        }

        TrackManagerConfig m_config;
        std::vector<Track> m_tracks;
        int64_t m_next_id = 1;
    };

}
}

#endif
