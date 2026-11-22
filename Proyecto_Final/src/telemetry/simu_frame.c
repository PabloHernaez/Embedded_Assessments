#include "simu_frame.h"

#include "attitude.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

/* NaN/Inf -> 0 y acotado a [lo, hi]: asi printf (que ya redondea) nunca ensancha el campo. */
static double fit(double value, double lo, double hi)
{
    return isfinite(value) ? fmin(hi, fmax(lo, value)) : 0.0;
}

int simu_frame_encode(const SimuFrameInput *in, char buf[SIMU_FRAME_BUFFER_SIZE])
{
    static const char *const state_text[] = { "IDLE____", "FLYING__", "PROBLEM_" };

    const AcquisitionSnapshot *a = in->acquisition;
    const GpsSnapshot *g = in->gps;
    const bool fix = (in->status->flags & TRACKER_FLAG_GPS_INVALID) == 0U;
    const bool have_position = g->last_fix_ms >= 0; /* si nunca hubo fix: ceros */
    const Vec3f rpy = euler_degrees(a->nav.relative_q);
    const Vec3f p = a->nav.position_m;
    const Vec3f v = a->nav.velocity_mps;
    const Vec3f acc = a->imu.accel_mps2;
    const Vec3f w = a->imu.gyro_dps;

    /* Tabla 3 del enunciado: un especificador por campo, en el mismo orden. */
    int len = snprintf(buf, SIMU_FRAME_BUFFER_SIZE,
        "$SIMU1,%05u,%010" PRIu64 ",%c,%02u,"   /* SEQ, TMS, GF, SAT */
        "%+010.6f,%+011.6f,%+08.1f,"             /* LAT, LON, ALT */
        "%+08.2f,%+08.2f,%+08.2f,"               /* X, Y, Z */
        "%+07.2f,%+07.2f,%+07.2f,"               /* VX, VY, VZ */
        "%+07.2f,%+07.2f,%+07.2f,"               /* ROLL, PITCH, YAW */
        "%+07.2f,%+07.2f,%+07.2f,"               /* AX, AY, AZ */
        "%+07.1f,%+07.1f,%+07.1f,"               /* GX, GY, GZ */
        "%05.1f,%+06.1f,%05.1f,%s,%04X",         /* LIGHT, TEMP, HUM, STATE, FLAGS */
        (unsigned int)in->seq, in->uptime_ms, fix ? '1' : '0',
        fix ? (g->satellites > 99U ? 99U : (unsigned int)g->satellites) : 0U,
        fit(have_position ? g->lat_udeg / 1e6 : 0.0, -90.0, 90.0),
        fit(have_position ? g->lon_udeg / 1e6 : 0.0, -180.0, 180.0),
        fit(have_position ? g->alt_mm / 1e3 : 0.0, -99999.9, 99999.9),
        fit(p.x, -9999.99, 9999.99), fit(p.y, -9999.99, 9999.99),
        fit(p.z, -9999.99, 9999.99),
        fit(v.x, -999.99, 999.99), fit(v.y, -999.99, 999.99),
        fit(v.z, -999.99, 999.99),
        fit(rpy.x, -999.99, 999.99), fit(rpy.y, -999.99, 999.99),
        fit(rpy.z, -999.99, 999.99),
        fit(acc.x, -999.99, 999.99), fit(acc.y, -999.99, 999.99),
        fit(acc.z, -999.99, 999.99),
        fit(w.x, -9999.9, 9999.9), fit(w.y, -9999.9, 9999.9),
        fit(w.z, -9999.9, 9999.9),
        fit(a->light.light_pct, 0.0, 100.0),
        fit(a->environment.temperature_c, -999.9, 999.9),
        fit(a->environment.humidity_pct, 0.0, 100.0),
        state_text[in->status->state], (unsigned int)in->status->flags);

    if (len != (int)SIMU_FRAME_WIRE_SIZE - 5) {
        return -EIO; /* algun campo no cabe: TMS de mas de 10 cifras */
    }

    /* Checksum: XOR de los caracteres entre '$' y '*', excluidos ambos. */
    uint8_t checksum = 0;
    for (int i = 1; i < len; i++) {
        checksum ^= (uint8_t)buf[i];
    }
    snprintf(buf + len, SIMU_FRAME_BUFFER_SIZE - (size_t)len, "*%02X\r\n", checksum);
    return 0;
}
