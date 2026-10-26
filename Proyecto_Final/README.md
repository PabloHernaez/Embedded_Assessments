# Drone tracker — proyecto final IoT UPM

Desarrollamos el firmware para NUCLEO-WL55JC del proyecto final de Embedded
Platforms and IoT. Partimos de la base D1 y de los contratos compartidos de
adquisición, GPS y reportes NORMAL/SIMU.

## Estado actual

Tenemos implementados los drivers privados del MPU6050, el Si7021 y el LDR
KY-018, además de la calibración inicial del giroscopio. En
`tests/board/sensors` mantenemos un ensayo independiente para MPU6050,
calibración y Si7021, con salida por serie.

El main de producción conserva el parpadeo provisional. Seguimos trabajando
en la validación física de sensores, la calibración final del LDR, la
adquisición integrada y la estimación de orientación, velocidad y posición.
D2 y D3 siguen abiertos; GPS, formatos y aplicación tienen sus contratos
preparados para el trabajo en paralelo.

## Entorno y perfiles

Usamos Zephyr 4.4.2, SDK 1.0.1 y el target `nucleo_wl55jc/stm32wl55xx`.
Trabajamos en VS Code con `../Proyecto_Final.code-workspace`; también podemos
abrir directamente esta carpeta. En ambos casos, `${workspaceFolder}` es
`Proyecto_Final` y la raíz Git es `Embedded_Assessments`.

| Perfil | Compilación | Programación | Salida |
| --- | --- | --- | --- |
| Producción | Ctrl+Shift+B / `West Build (auto)`; limpia: `West Build (pristine)` | `Flash (CubeProgrammer, UR+HWrst)` | `build/zephyr`, sin consola Zephyr |
| Ensayo MPU6050 + Si7021 | `West Build (sensores)` | `Flash (Sensores)` | `build-sensors/zephyr`, LPUART1 a 115200 8N1 |

Compartimos la configuración y el overlay de placa entre perfiles; el ensayo
aplica sus ajustes propios después. Mantenemos las restricciones de consola
en producción mediante `src/build_checks.c`.

Las rutas de herramientas de `.vscode/` y `scripts/` corresponden al entorno
local de David. Las ajustamos a cada equipo conservando las versiones fijadas.

## Reparto del trabajo

| Bloque | Responsable | Contrato compartido |
| --- | --- | --- |
| Drivers, estimación, estadísticas y adquisición | David | `include/acquisition_api.h` |
| GPS y parser NMEA | Pablo | `include/gps_api.h` |
| Reportes SIMU y humano | Pablo | `include/simu_frame.h`, `include/human_report.h` |
| Aplicación, modos, UART, estado y RGB | Pablo | Tipos de `include/model.h` |

Mantenemos los tipos y las seis APIs públicas en `include/`, y los detalles
privados de cada bloque en `src/`. Coordinamos los cambios de contratos;
Pablo mantiene `main`, CMake, configuración y overlay tras la base D1.

## Documentación

- [Desarrollo y pendientes](docs/README_DESARROLLO.md).
- [Entorno Zephyr en Windows](docs/Zephyr_UPM_Playbook.md).
- [Ensayos](tests/README.md).
