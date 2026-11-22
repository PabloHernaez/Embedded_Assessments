
#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <stdbool.h>

// Estructura de GPS
typedef struct {
    double latitude;
    double longitude;
    double altitude;

    uint8_t satellites;
    uint8_t fix_quality;

    bool valid;
    // Cuenta de GGAs (debugging)
    uint32_t gga_count;

} GPS_Struct;


// Inicializar el modulo GPS
int gps_init(void);

// comprobar posicion valida
bool gps_has_fix(void);

// Obtener posicion gps
int gps_get_data(GPS_Struct *data);

#endif /* GPS_H */
