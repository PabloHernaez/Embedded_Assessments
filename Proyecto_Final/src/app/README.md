# Aplicación (P3)

`src/main.c` es el hilo main (prioridad 4), despertado por un `k_timer` de
100 ms. En cada vuelta:

1. Alimenta su canal de watchdog.
2. Lee las pulsaciones de B1 y, si no hay un envío en curso, cambia de modo
   TEST → NORMAL → SIMU → TEST. Entrar o salir de NORMAL abre o cierra la
   sesión de estadísticas.
3. Copia los snapshots de adquisición y GPS y decide el estado (`tracker_status.c`).
4. Pone el color del LED (`board_ui.c`) y cuenta vueltas: envía el mensaje del
   modo cada 20 (TEST, 2 s), 50 (NORMAL, 5 s) o 1 (SIMU, 100 ms). Al cambiar de
   modo la cuenta vuelve a cero, así que SIMU envía su primera trama ya.

| Archivo | Qué hace |
| --- | --- |
| `board_ui.c` | B1 mediante el subsistema `input` de Zephyr (nodo `gpio-keys` de la placa, antirrebote de 30 ms; cuenta al soltar) y LED RGB por GPIO: azul TEST; verde/amarillo/rojo IDLE/FLYING/PROBLEM en NORMAL; cian SIMU. |
| `tracker_status.c` | Flags SIMU y estado con un solo criterio: la edad del último dato bueno (desde el arranque si nunca lo hubo). PROBLEM tiene prioridad: IMU 250 ms sin dato o calibración pendiente (tras 10 s de arranque), luz o ambiente 10 s sin dato, o GPS 10 s sin NMEA. Sale de PROBLEM tras 2 s sin causas. La falta de fix solo activa el flag 0x0002. Un impacto (`imu.last_impact_ms`) activa 0x0010 y PROBLEM durante 5 s. |
| `output.c` | LPUART1 a 115200 8N1 con la API asíncrona de UART: `uart_tx` entrega el mensaje al DMA y el driver avisa con `UART_TX_DONE` al salir el último bit. El plazo (duración en el cable + 20 ms) se pasa a `uart_tx`: si vence, el driver aborta (`UART_TX_ABORTED`) y la salida no se queda ocupada ni bloquea el cambio de modo. |

## Watchdog

`task_wdt` de Zephyr: main (1 s), adquisición (1 s) y GPS (3 s) tienen
cada uno su canal. Si un hilo deja de alimentarlo, el sistema se reinicia. El
IWDG (2 s) respalda al propio `task_wdt`. La causa de reinicio (`hwinfo`) y
el estado del watchdog salen en el informe de TEST.
