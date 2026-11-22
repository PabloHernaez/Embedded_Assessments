# Servicio de adquisición (D4)

Un hilo estático (prioridad 3, pila de 2048 B) que despierta con un `k_timer`
de 20 ms. Cada vuelta:

1. **Luz** cada 2 s: ADC y porcentaje (`acquisition_light.c`).
2. **IMU** en cada vuelta (`acquisition_imu.c`): lectura, calibración inicial,
   movimiento y navegación. NO_NEW no hace nada.
3. **Ambiente** cada 2 s (`acquisition_environment.c`): lanza F5 y recoge el par
   40 ms después, en otra vuelta, sin bloquear.
4. **Publicación**: copia lo que cambió y acumula las estadísticas de NORMAL
   bajo `acquisition_lock`, en una sección breve. Hardware y cálculo, fuera.

La aplicación solo usa `include/acquisition_api.h`: `acquisition_snapshot_get`,
`acquisition_stats_begin` y `acquisition_stats_end`.

## Fallos

| Caso | Qué hace |
| --- | --- |
| Error I2C de la IMU | Dato inválido, se conservan valor, fecha, q/v/p, sesgo y q0. El Si7021 no usa el bus en esa vuelta y se revalida antes de su siguiente medida. |
| 3 errores seguidos o 500 ms sin muestra nueva | `i2c_recover_bus` y `imu_init`, como mucho una vez por segundo. La primera muestra posterior usa dt = 0. |
| Saturación | Dato inválido y continuidad rota, sin reiniciar nada. |
| Error del Si7021 o del ADC | Calidad inválida, se conserva el último valor y se reconfigura en el siguiente intento (2 s). |

## Estadísticas

`../algorithms/statistics.c`: ventanas consecutivas de 30 s `[inicio, fin)`
desde `acquisition_stats_begin`, con media/mín/máx/n de luz, T, HR, |a| y |ω|.
La ventana se cierra a su hora aunque no haya muestras; n = 0 se muestra N/D.
