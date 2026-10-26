#ifndef DRONE_TRACKER_GPS_API_H
#define DRONE_TRACKER_GPS_API_H

#include "model.h"

/* Copia el estado publicado. out debe apuntar a memoria valida. */
void gps_snapshot_get(GpsSnapshot *out);

#endif