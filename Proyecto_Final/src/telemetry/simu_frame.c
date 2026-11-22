#include "simu_frame.h"

#include <errno.h>
#include <math.h>
#include <string.h>

static const char hex[] = "0123456789ABCDEF";

static char *digits(char *out, uint64_t value, size_t width)
{
    for (size_t i = width; i > 0U; --i) {
        out[i - 1U] = (char)('0' + value % 10U);
        value /= 10U;
    }
    return out + width;
}

static int32_t power_of_ten(size_t width)
{
    int32_t value = 1;
    for (size_t i = 0U; i < width; ++i) {
        value *= 10;
    }
    return value;
}

static char *fixed(char *out, int32_t value, size_t whole,
                   size_t decimals, bool signed_field)
{
    const int32_t scale = power_of_ten(decimals);
    const int32_t limit = signed_field ? power_of_ten(whole + decimals) - 1
                                       : 100 * scale;
    const int32_t low = signed_field ? -limit : 0;

    if (value < low) {
        value = low;
    } else if (value > limit) {
        value = limit;
    }

    if (signed_field) {
        *out++ = value < 0 ? '-' : '+';
    }

    const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    out = digits(out, magnitude / (uint32_t)scale, whole);
    *out++ = '.';
    out = digits(out, magnitude % (uint32_t)scale, decimals);
    *out++ = ',';
    return out;
}

static char *real_field(char *out, float value, size_t whole, size_t decimals,
                        bool signed_field, uint16_t *replacements)
{
    if (!isfinite(value)) {
        ++*replacements;
        value = 0.0f;
    }

    /* Acotar antes de convertir a entero. fixed() ajusta el ultimo digito. */
    const float edge = signed_field ? (float)power_of_ten(whole) : 100.0f;
    value = fminf(edge, fmaxf(signed_field ? -edge : 0.0f, value));
    const int32_t scaled = (int32_t)roundf(value * (float)power_of_ten(decimals));

    return fixed(out, scaled, whole, decimals, signed_field);
}

static char *vector_fields(char *out, Vec3f vector, size_t whole,
                           size_t decimals, uint16_t *replacements)
{
    out = real_field(out, vector.x, whole, decimals, true, replacements);
    out = real_field(out, vector.y, whole, decimals, true, replacements);
    return real_field(out, vector.z, whole, decimals, true, replacements);
}

static Vec3f euler_degrees(Quaternionf q, uint16_t *replacements)
{
    if (!isfinite(q.w) || !isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z)) {
        *replacements += 3U;
        return (Vec3f){0};
    }

    const float s = fminf(1.0f, fmaxf(-1.0f, 2.0f * (q.w*q.y - q.x*q.z)));
    const float degrees_per_rad = 57.2957795131f;

    if (1.0f - fabsf(s) <= 1.0e-6f) {
        return (Vec3f){
            atan2f(2.0f * (q.w*q.x - q.y*q.z),
                   1.0f - 2.0f * (q.x*q.x + q.z*q.z)) * degrees_per_rad,
            copysignf(90.0f, s),
            0.0f
        };
    }

    return (Vec3f){
        atan2f(2.0f * (q.w*q.x + q.y*q.z),
               1.0f - 2.0f * (q.x*q.x + q.y*q.y)) * degrees_per_rad,
        asinf(s) * degrees_per_rad,
        atan2f(2.0f * (q.w*q.z + q.x*q.y),
               1.0f - 2.0f * (q.y*q.y + q.z*q.z)) * degrees_per_rad
    };
}

int simu_frame_encode(const SimuFrameInput *in, char *buf, size_t cap,
                      size_t *len, uint16_t *replacements)
{
    static const char *const state_text[] = {"IDLE____", "FLYING__", "PROBLEM_"};

    *len = 0U;
    *replacements = 0U;

    if (cap < SIMU_FRAME_BUFFER_SIZE) {
        return -EMSGSIZE;
    }
    if (in->uptime_ms > UINT64_C(9999999999)) {
        return -ERANGE;
    }

    const unsigned int state = (unsigned int)in->status->state;
    const uint16_t flags = in->status->flags;
    if (state >= sizeof(state_text) / sizeof(state_text[0]) || (flags & 0xFF00U)) {
        return -EINVAL;
    }

    const AcquisitionSnapshot *acq = in->acquisition;
    const GpsSnapshot *gps = in->gps;
    const bool fix = (flags & TRACKER_FLAG_GPS_INVALID) == 0U;
    const bool have_position = gps->last_fix_ms >= 0;
    const int64_t altitude_mm = have_position ? gps->alt_mm : 0;
    const int32_t altitude_dm =
        (int32_t)((altitude_mm + (altitude_mm < 0 ? -50 : 50)) / 100);
    const unsigned int satellites = gps->satellites > 99U ? 99U : gps->satellites;

    memcpy(buf, "$SIMU1,", 7U);
    char *out = digits(buf + 7U, in->seq, 5U);
    *out++ = ',';
    out = digits(out, in->uptime_ms, 10U);
    *out++ = ',';
    *out++ = fix ? '1' : '0';
    *out++ = ',';
    out = digits(out, fix ? satellites : 0U, 2U);
    *out++ = ',';

    out = fixed(out, have_position ? gps->lat_udeg : 0, 2U, 6U, true);
    out = fixed(out, have_position ? gps->lon_udeg : 0, 3U, 6U, true);
    out = fixed(out, altitude_dm, 5U, 1U, true);

    out = vector_fields(out, acq->nav.position_m, 4U, 2U, replacements);
    out = vector_fields(out, acq->nav.velocity_mps, 3U, 2U, replacements);
    out = vector_fields(out, euler_degrees(acq->nav.relative_q, replacements),
                        3U, 2U, replacements);
    out = vector_fields(out, acq->imu.accel_mps2, 3U, 2U, replacements);
    out = vector_fields(out, acq->imu.gyro_dps, 4U, 1U, replacements);
    out = real_field(out, acq->light.light_pct, 3U, 1U, false, replacements);
    out = real_field(out, acq->environment.temperature_c, 3U, 1U, true, replacements);
    out = real_field(out, acq->environment.humidity_pct, 3U, 1U, false, replacements);

    memcpy(out, state_text[state], 8U);
    out += 8U;
    *out++ = ',';
    for (unsigned int shift = 16U; shift > 0U; shift -= 4U) {
        *out++ = hex[(flags >> (shift - 4U)) & 0x0FU];
    }

    uint8_t checksum = 0U;
    for (const char *p = buf + 1U; p < out; ++p) {
        checksum ^= (uint8_t)*p;
    }

    *out++ = '*';
    *out++ = hex[checksum >> 4];
    *out++ = hex[checksum & 0x0FU];
    *out++ = '\r';
    *out++ = '\n';
    *out = '\0';

    *len = (size_t)(out - buf);
    return 0;
}