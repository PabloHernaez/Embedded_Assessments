# Aplicación (P3)

`src/main.c` es el hilo main (prioridad 4). Cada 100 ms, contados desde el
arranque con un timeout absoluto:

1. Alimenta su canal de watchdog y vigila el plazo de la TX (`output.c`).
2. Lee las pulsaciones de B1 y, si no hay un envío en curso, cambia de modo
   TEST → NORMAL → SIMU → TEST. Entrar o salir de NORMAL abre o cierra la
   sesión de estadísticas.
3. Copia los snapshots de adquisición y GPS y decide el estado (`tracker_status.c`).
4. Pone el color del LED (`board_ui.c`) y envía el mensaje del modo si toca:
   TEST cada 2 s, NORMAL cada 5 s, SIMU cada 100 ms.

| Archivo | Qué hace |
| --- | --- |
| `board_ui.c` | B1 mediante el subsistema `input` de Zephyr (nodo `gpio-keys` de la placa, antirrebote de 30 ms; cuenta al soltar) y LED RGB por GPIO: azul TEST; verde/amarillo/rojo IDLE/FLYING/PROBLEM en NORMAL; cian SIMU. |
| `tracker_status.c` | Flags SIMU y estado. PROBLEM tiene prioridad: IMU 250 ms sin dato (tras 10 s de gracia), calibración pendiente tras 10 s, luz o ambiente 10 s sin dato, GPS 10 s sin NMEA (tras 30 s de gracia) o fallo de la TX. Sale de PROBLEM tras 2 s sin causas. La falta de fix no provoca PROBLEM. |
| `output.c` | LPUART1 a 115200 8N1 con TX por interrupción; plazo de envío y fallo de enlace. |

## Watchdog

`task_wdt` de Zephyr: main (1 s), adquisición (2 s) y GPS (3 s) tienen
cada uno su canal. Si un hilo deja de alimentarlo, el sistema se reinicia. El
IWDG (2 s) respalda al propio `task_wdt`. La causa de reinicio (`hwinfo`) y
el estado del watchdog salen en el informe de TEST.
