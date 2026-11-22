# Desarrollo del firmware

## Entorno

| Componente | Versión / revisión |
| --- | --- |
| Placa / target | NUCLEO-WL55JC / `nucleo_wl55jc/stm32wl55xx` |
| Zephyr | 4.4.2, commit `dccb09599635bdff17633fa7e9dab014b91dce90` |
| SDK / ARM GNU | 1.0.1 / 14.3.0 |
| Python / west | 3.12.10 / 1.5.0 |
| CMake / Ninja / DTC | 4.4.3 / 1.13.2 / 1.6.1 |

Cada uno instala Zephyr, el SDK y las herramientas en su equipo con estas
versiones. `.vscode/`, `scripts/` y las notas locales quedan fuera de Git.
Queda por aclarar la referencia docente a 4.4.99 frente a 4.4.2.

## Compilación

Desde `Proyecto_Final`, con el entorno de Zephyr activo (Windows o Ubuntu):

```sh
west build -p auto -b nucleo_wl55jc . -d build -- -DCONF_FILE=confs/prj_nucleo_wl55jc.conf -DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay
```

`-p always` regenera desde cero. Los ensayos y las pruebas de host están en
[tests/README.md](../tests/README.md).

## Estructura

| Ruta | Contenido | Responsable |
| --- | --- | --- |
| `include/` | Tipos (`model.h`) y las seis operaciones públicas | Compartido |
| `src/drivers` | MPU6050, Si7021 y LDR | David (D2) |
| `src/algorithms` | Calibración, movimiento, estimador y estadísticas (C puro) | David (D3/D4) |
| `src/acquisition` | Hilo de adquisición y publicación | David (D4) |
| `src/gps` | Hilo GPS y parser NMEA | Pablo (P1) |
| `src/telemetry` | Trama SIMU e informe legible | Pablo (P2) |
| `src/main.c`, `src/app` | Modos, estado, LED, salida UART | Pablo (P3) |

| Header | Operaciones |
| --- | --- |
| `acquisition_api.h` | `acquisition_snapshot_get`, `acquisition_stats_begin`, `acquisition_stats_end` |
| `gps_api.h` | `gps_snapshot_get` |
| `simu_frame.h` | `simu_frame_encode` |
| `human_report.h` | `human_report_format` |

## Configuración

`confs/prj_nucleo_wl55jc.conf` y `boards/nucleo_wl55jc.overlay`:

- LPUART1 (VCP) a 115200 solo para los reportes: sin consola, printk, logs
  ni banner; `src/build_checks.c` lo comprueba al compilar (GR5/SM6).
- USART1 a 9600 con RX en PB7 (pull-up) para el GPS; UART por interrupciones.
- I2C2 a 100 kHz (PA12 SCL / PA11 SDA), timeout de 10 ms por transferencia
  y recuperación de bus por GPIO.
- ADC1 canal 5 (PB1), 12 bits, referencia interna, para el LDR.
- RGB activo bajo en PB5/PB8/PB10; B1 de la placa.
- SysTick tickless a 10 000 ticks/s y timeouts de 64 bits.
- `task_wdt` con IWDG de respaldo (2 s), `hwinfo` para la causa de reinicio.
- `printf` con coma flotante de picolibc para los formatos.

## Decisiones y limitaciones

- **Bajo consumo (GR6).** `CONFIG_PM=n`: los modos STOP pararían SysTick y la
  RX del GPS. El consumo se reduce sin ellos: los hilos bloquean en timer,
  cola o timeout absoluto y el idle ejecuta WFI; no hay esperas activas
  (TX por IRQ, Si7021 sin hold); el sensor de temperatura del MPU está
  apagado y el calentador del Si7021 también; no hay hilo de logs.
  Mejora posible: usar el pin INT del MPU en lugar de sondear `INT_STATUS`.
- **Orientación y navegación.** Un solo marco: los ejes del sensor al terminar la
  calibración. `q` empieza en identidad (SR4) y se integra con Euler; la aceleración
  se rota con `q` y se le resta `g0`, la gravedad medida en reposo. Z es altura si
  la placa arranca nivelada ([DEC-D3-02](D3_ESTIMACION.md)).
- **Deriva.** Velocidad y posición se integran sin corrección (FLAGS 0x0040)
  y el yaw no tiene referencia absoluta (0x0080). Con el diseño anterior, en
  600 s en reposo: |v| = 284 m/s, |p| = 55,6 km ([D3](D3_ESTIMACION.md)); hay que
  repetirlo con el actual.
- **LDR.** Referencias medidas con el montaje actual (3,3 V, sin pull-down
  de 47 kΩ); hay que repetirlas si cambia el montaje. No mide lux.
- **Recuperación de la IMU.** Tras 3 errores seguidos o 500 ms sin muestra
  nueva se recupera el bus y se reinicializa, como mucho una vez por
  segundo, conservando sesgo, g0 y trayectoria. El Si7021 se revalida tras
  cualquier error del bus compartido.
- **Impactos.** No se detectan; el flag 0x0010 siempre vale 0.

## Pendientes

- P4 en placa: fallos y retoma de cada sensor con la versión actual, bloqueo
  de cada hilo (reinicio por `task_wdt`), 10 min de SIMU con DroneSIMU,
  pilas y consumo. La evidencia de fallos I2C es de la versión `bc0797f`.
- Revisión cruzada de los cambios de simplificación (D1–D4 por Pablo, P1–P3 por David).
- Reproducir el build en el segundo equipo.
