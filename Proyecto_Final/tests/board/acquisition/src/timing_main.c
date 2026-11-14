#include <zephyr/kernel.h>

#include "acquisition_api.h"

/**
 * Activa estadisticas reales para medir adquisicion sin salida de diagnostico.
 * Entradas: ninguna explicita; el hilo D4 se crea estaticamente como en produccion.
 * Retorno: no retorna durante el ensayo (firma int de main).
 * Modifica: abre una sesion estadistica; despues duerme y deja trabajar al servicio.
 *           No lee sensores, imprime, barre pilas ni altera sus prioridades.
 */
int main(void)
{
    acquisition_stats_begin();
    while (true) {
        k_msleep(1000);
    }
}
