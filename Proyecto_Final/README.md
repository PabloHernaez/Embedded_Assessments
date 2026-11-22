# Drone tracker — proyecto final IoT UPM

Firmware para NUCLEO-WL55JC (Zephyr 4.4.2) de un nodo de seguimiento y
telemetría: GPS, MPU6050, LDR y Si7021, LED RGB y salida por el VCP del
ST-LINK en tres modos (TEST, NORMAL y SIMU) que se cambian con B1.

## Arquitectura

Tres hilos, cada uno con su canal de `task_wdt` (respaldado por el IWDG):

| Hilo | Prioridad | Despierta | Hace |
| --- | --- | --- | --- |
| Adquisición (`src/acquisition`) | 3 | `k_timer` de 20 ms | IMU a 50 Hz, luz y ambiente cada 2 s, calibración, movimiento, navegación y estadísticas de 30 s. |
| main (`src/main.c`, `src/app`) | 4 | Slot absoluto de 100 ms | Modo (B1), estado IDLE/FLYING/PROBLEM, LED y envío por LPUART1. |
| GPS (`src/gps`) | 5 | Línea NMEA de la ISR (`k_msgq`), máx. 1 s | Checksum, GGA, enlace, fix y UTC. |

Los hilos comparten datos solo mediante copias (`*_snapshot_get`) protegidas
por un mutex por servicio. Los drivers (`src/drivers`) son propios y sin
estado; los formatos (`src/telemetry`) son funciones puras.

| Modo | Salida | LED |
| --- | --- | --- |
| TEST | Informe legible cada 2 s, con causa de reinicio y watchdog | Azul |
| NORMAL | Informe cada 5 s y estadísticas de 30 s | Verde IDLE / amarillo FLYING / rojo PROBLEM |
| SIMU | Solo tramas `$SIMU1` de 221 bytes cada 100 ms | Cian |

## Compilar

Con el entorno de Zephyr activo, desde esta carpeta:

```sh
west build -p auto -b nucleo_wl55jc . -d build -- -DCONF_FILE=confs/prj_nucleo_wl55jc.conf -DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay
```

Producción no tiene consola: `src/build_checks.c` impide compilarla con
consola, printk o logs. Los ensayos están en [tests](tests/README.md).

## Reparto

| Bloque | Responsable |
| --- | --- |
| Drivers, algoritmos y adquisición (D1–D4) | David |
| GPS, formatos, aplicación y validación (P1–P4) | Pablo |

## Documentación

- [Desarrollo: entorno, configuración, decisiones y pendientes](docs/README_DESARROLLO.md)
- [Ensayos y evidencia](tests/README.md)
- [Estimación y deriva (D3)](docs/D3_ESTIMACION.md)
