#ifndef DRONE_TRACKER_ACQUISITION_API_H
#define DRONE_TRACKER_ACQUISITION_API_H

#include "model.h"

/* Copia el estado publicado. out debe apuntar a memoria valida. */
void acquisition_snapshot_get(AcquisitionSnapshot *out);

/* Inicia una nueva sesion de estadisticas de NORMAL. */
void acquisition_stats_begin(void);

/* Finaliza la sesion y descarta la ventana parcial. */
void acquisition_stats_end(void);

#endif