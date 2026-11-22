# Ensayos

## Host (sin placa)

Código C puro y módulos de adquisición con el driver y el reloj simulados.
Desde `Proyecto_Final`:

    cmake -S tests/host -B build-host
    cmake --build build-host --config Debug
    ctest --test-dir build-host -C Debug --output-on-failure

| Suite | Qué comprueba |
| --- | --- |
| `test_motion.c` | Confirmación de movimiento (300 ms) y reposo (2 s), histéresis y cancelación. |
| `test_estimator.c` | Reposo con la placa inclinada sin deriva (resta de g0), giros de 90° en cada eje y su composición, Euler explícito (p = 0,49 m tras 1 s a 1 m/s²) y dt = 0. |
| `test_acquisition_imu.c` | Calibración (100 muestras: sesgo y g0 como medias), NO_NEW, errores, saturación, huecos > 50 ms, navegación con dt reales desde una postura inclinada y recuperación (3 errores o 500 ms sin muestra → reinicio, máx. 1/s, sin perder sesgo, g0 ni trayectoria). |
| `test_acquisition_environment.c` | Cadencia de 2 s, 40 ms desde el final de F5, retención del par, revalidación tras fallos y tras un error de bus de la IMU (regresión Si7021 del 03/11). |
| `test_statistics.c` | Ventanas `[t0, t1)`, muestra en el límite, valores repetidos, n = 0, ventanas perdidas, begin/end. |
| `test_simu_frame.c` | Vector oficial del enunciado (XOR 3F), sin fix, valores fuera de rango, NaN y TMS de 11 cifras. |
| `test_human_report.c` | Informe TEST/NORMAL, N/D, estadísticas y falta de espacio. |
| `test_tracker_status.c` | Flags, gracias iniciales, prioridad de PROBLEM y recuperación tras 2 s. |

Las suites de formato también se han ejecutado en `qemu_cortex_m3` con
picolibc (el `printf` que usa la placa).

## Placa

Cada ensayo es una aplicación aparte que reutiliza las fuentes reales y abre
consola en LPUART1 (115200 8N1). Producción no la tiene.

| Ensayo | Comando | Para qué |
| --- | --- | --- |
| `board/light` | `west build -b nucleo_wl55jc tests/board/light -d build-light` | Códigos ADC brutos del LDR para calibrar (`capturar_ldr.py`). |
| `board/gps` | `west build -b nucleo_wl55jc tests/board/gps -d build-gps` | Servicio GPS real: enlace, fix, posición y UTC cada segundo. |
| `board/acquisition` | `west build -b nucleo_wl55jc tests/board/acquisition -d build-acquisition` | Servicio de adquisición real con un observador de snapshots y órdenes `b`/`e` de sesión. |

## Evidencia registrada

| Registro | Contenido |
| --- | --- |
| [Deriva D3](../docs/D3_ESTIMACION.md) y `board/sensors/results/` (firmware de ensayo retirado) | 600 s en reposo: \|v\| = 284,24 m/s y \|p\| = 55 567,65 m al final; giro de 90° en Z; orientación relativa. |
| [Calibración LDR](board/light/results/Calibracion_LDR_20261102.md) | Referencias 4062 (oscuridad) y 128 (linterna a 5 cm), 3,3 V sin pull-down. |
| [Ensayo D4](board/acquisition/results/Ensayo_D4_20261102.md) | Arranque, movimiento, luz, sesiones y ausencia/retoma de cada sensor. |
| [Tiempos D4](board/acquisition/results/Tiempos_Adquisicion_D4_20261102.md), [I2C](board/acquisition/results/I2C_D4_20261102.md), [Si7021](board/acquisition/results/Si7021_Diagnostico_20261103.md) | Medidos con la versión `bc0797f` (antes de simplificar la recuperación): vuelta nominal máx. 3,2 ms, bus retenido y la corrección del Si7021. |

## Pendiente en placa (P4)

- Repetir con la versión actual: desconexión de cada sensor, reset del MPU,
  SDA/SCL retenidas y retoma del Si7021.
- Bloquear cada hilo y comprobar el reinicio por `task_wdt`.
- 10 min de SIMU con DroneSIMU: intervalo, jitter y omisiones.
- Pilas reales (`.map` y margen observado) y consumo.
