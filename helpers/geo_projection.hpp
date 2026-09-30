#ifndef DE_GEO_PROJECTION_H_
#define DE_GEO_PROJECTION_H_

/**
 * @brief Pure pinhole-camera to ground-plane projection (P3-07, shared
 * since SU-04).
 *
 * Projects a normalized image point (u, v) in -0.5..0.5 through a camera
 * with known FOV, mounted with a fixed attitude relative to the body,
 * through the vehicle attitude into NED, and intersects the ray with a
 * flat ground plane at home altitude.
 *
 * Frames: camera (x right, y down, z forward) -> body FRD (x fwd, y right,
 * z down) -> NED. Mount pitch -90 deg = camera looking straight down; with
 * that mount, image u+ maps to body +y (right) and image v+ maps to body
 * -x (behind). Use mount yaw_deg to re-align the image axes to a different
 * physical mounting.
 *
 * Shared by de_mavlink (live pose from the FC) and de_comm (fixed pose of
 * a static unit, TASK-SU-04). No dependencies beyond <cmath>/<array> so it
 * can be unit-tested standalone.
 */

#include <array>
#include <cmath>

namespace de
{
namespace geo
{

    struct GeoCameraParams
    {
        double hfov_deg = 62.2;
        double vfov_deg = 48.8;
    };

    /// camera mount attitude relative to the body frame (deg).
    /// pitch -90 looks straight down.
    struct GeoMountParams
    {
        double pitch_deg = -90.0;
        double yaw_deg   = 0.0;
        double roll_deg  = 0.0;
    };

    /// vehicle attitude in radians (aerospace roll/pitch/yaw).
    struct GeoAttitude
    {
        double roll_rad  = 0.0;
        double pitch_rad = 0.0;
        double yaw_rad   = 0.0;
    };

    /// vehicle position; rel_alt_m is height above the home-altitude
    /// ground plane (GLOBAL_POSITION_INT.relative_alt). For a static unit
    /// it is the configured camera mount height above the ground.
    struct GeoPose
    {
        double lat_deg   = 0.0;
        double lng_deg   = 0.0;
        double rel_alt_m = 0.0;
    };

    struct GeoProjectionResult
    {
        bool   ok = false;
        double lat = 0.0;             ///< deg
        double lng = 0.0;             ///< deg
        double ground_range_m = 0.0;  ///< horizontal distance from the vehicle
        double unc_m = 0.0;           ///< ~95 % radius (2 sigma)
    };

    using GeoVec3 = std::array<double, 3>;

    inline double geoDeg2Rad (const double deg) { return deg * M_PI / 180.0; }

    /// convert a 0..1 normalized box coordinate into the -0.5..0.5
    /// centered convention this projector expects
    inline double geoBoxToCentered (const double normalized01)
    {
        return normalized01 - 0.5;
    }

    /// pinhole ray in camera frame (x right, y down, z forward)
    inline GeoVec3 geoRayCamera (const double u, const double v,
                                 const GeoCameraParams& cam)
    {
        return { std::tan(u * geoDeg2Rad(cam.hfov_deg)),
                 std::tan(v * geoDeg2Rad(cam.vfov_deg)),
                 1.0 };
    }

    inline void geoRot (const double r, const double p, const double y,
                        const GeoVec3& in, GeoVec3& out)
    {
        const double cr = std::cos(r), sr = std::sin(r);
        const double cp = std::cos(p), sp = std::sin(p);
        const double cy = std::cos(y), sy = std::sin(y);

        // ZYX euler: Rz(yaw) * Ry(pitch) * Rx(roll)
        out[0] = (cy*cp) * in[0] + (cy*sp*sr - sy*cr) * in[1] + (cy*sp*cr + sy*sr) * in[2];
        out[1] = (sy*cp) * in[0] + (sy*sp*sr + cy*cr) * in[1] + (sy*sp*cr - cy*sr) * in[2];
        out[2] = (-sp)   * in[0] + (cp*sr)            * in[1] + (cp*cr)            * in[2];
    }

    /// camera frame -> body FRD: forward camera axis maps to body-forward,
    /// then the mount euler is applied (z fwd camera = base pointing fwd)
    inline GeoVec3 geoRayBody (const GeoVec3& ray_cam, const GeoMountParams& mount)
    {
        // base alignment: body_x = cam_z, body_y = cam_x, body_z = cam_y
        const GeoVec3 base = { ray_cam[2], ray_cam[0], ray_cam[1] };
        GeoVec3 out;
        geoRot(geoDeg2Rad(mount.roll_deg), geoDeg2Rad(mount.pitch_deg),
               geoDeg2Rad(mount.yaw_deg), base, out);
        return out;
    }

    /// body FRD -> NED through the vehicle attitude
    inline GeoVec3 geoRayNED (const GeoVec3& ray_body, const GeoAttitude& att)
    {
        GeoVec3 out;
        geoRot(att.roll_rad, att.pitch_rad, att.yaw_rad, ray_body, out);
        return out;
    }

    /// local tangent-plane offset -> lat/lng
    inline void geoOffsetToLatLng (const double lat_deg, const double lng_deg0,
                                   const double north_m, const double east_m,
                                   double& out_lat, double& out_lng)
    {
        constexpr double M_PER_DEG_LAT = 111320.0;
        const double clat = std::cos(geoDeg2Rad(lat_deg));
        const double m_per_deg_lng = M_PER_DEG_LAT * (std::fabs(clat) < 1e-6 ? 1e-6 : clat);
        out_lat = lat_deg + north_m / M_PER_DEG_LAT;
        out_lng = lng_deg0 + east_m / m_per_deg_lng;
    }

    /// horizontal distance between two lat/lng points (m)
    inline double geoDistanceM (const double lat1, const double lng1,
                                const double lat2, const double lng2)
    {
        constexpr double M_PER_DEG_LAT = 111320.0;
        const double dlat = (lat2 - lat1) * M_PER_DEG_LAT;
        const double dlng = (lng2 - lng1) * M_PER_DEG_LAT
                          * std::cos(geoDeg2Rad((lat1 + lat2) * 0.5));
        return std::sqrt(dlat * dlat + dlng * dlng);
    }

    /**
     * @brief project a normalized image point to the ground.
     *
     * @param u, v           normalized -0.5..0.5 (use geoBoxToCentered for
     *                       0..1 top-left conventions)
     * @param box_w, box_h   normalized box size (0..1) - used only for the
     *                       uncertainty budget
     * @param rangefinder_m  downward rangefinder distance (m), < 0 when not
     *                       available; used when the ray is within 15 deg of
     *                       vertical
     * @return ok=false when the ray is above the horizon or less than 5 deg
     *         below it
     */
    inline GeoProjectionResult geoProjectToGround (
        const double u, const double v,
        const double box_w, const double box_h,
        const GeoCameraParams& cam, const GeoMountParams& mount,
        const GeoAttitude& att, const GeoPose& pose,
        const double sigma_att_deg, const double sigma_alt_m,
        const double rangefinder_m = -1.0)
    {
        GeoProjectionResult res;

        const GeoVec3 ray_cam  = geoRayCamera(u, v, cam);
        const GeoVec3 ray_body = geoRayBody(ray_cam, mount);
        const GeoVec3 ray_ned  = geoRayNED(ray_body, att);

        // normalize
        const double n = std::sqrt(ray_ned[0]*ray_ned[0] + ray_ned[1]*ray_ned[1]
                                   + ray_ned[2]*ray_ned[2]);
        if (n < 1e-9) return res;
        const double rn = ray_ned[0] / n, re = ray_ned[1] / n, rd = ray_ned[2] / n;

        // the ray must point at least 5 deg below the horizon
        constexpr double MIN_DOWN = 0.0871557427;  // sin(5 deg)
        if (rd < MIN_DOWN) return res;

        // ground depth below the vehicle: rel_alt above the home plane,
        // or the rangefinder when the ray is near vertical
        double depth_m = pose.rel_alt_m;
        constexpr double NEAR_VERTICAL = 0.9659258263;  // cos(15 deg)
        if ((rangefinder_m > 0.0) && (rd >= NEAR_VERTICAL))
        {
            depth_m = rangefinder_m;
        }
        if (depth_m <= 0.0) return res;

        const double slant_m = depth_m / rd;
        const double north_m = slant_m * rn;
        const double east_m  = slant_m * re;

        geoOffsetToLatLng(pose.lat_deg, pose.lng_deg, north_m, east_m,
                          res.lat, res.lng);
        res.ground_range_m = std::sqrt(north_m * north_m + east_m * east_m);

        // 1-sigma contributors (meters)
        const double s_alt = sigma_alt_m / rd;                    // alt error * slant factor
        const double s_att = geoDeg2Rad(sigma_att_deg) * slant_m; // attitude error at slant range
        const double s_box = 0.5 * slant_m * std::max(
            std::fabs(box_w) * geoDeg2Rad(cam.hfov_deg),
            std::fabs(box_h) * geoDeg2Rad(cam.vfov_deg));          // half box on the ground
        res.unc_m = 2.0 * std::sqrt(s_alt * s_alt + s_att * s_att + s_box * s_box);

        res.ok = true;
        return res;
    }

} // namespace geo
} // namespace de

#endif
