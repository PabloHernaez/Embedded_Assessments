# Drone tracker - base compartida D1

Base del proyecto final para NUCLEO-WL55JC. Compila un main minimo de parpadeo,
valida la configuracion de produccion y publica los tipos y las seis APIs
que permiten avanzar en paralelo a David y Pablo.

## Abrir y compilar

Abrir `../Proyecto_Final.code-workspace` con VS Code, o directamente esta
carpeta. `${workspaceFolder}` debe apuntar a `Proyecto_Final`; el repositorio
Git es la carpeta superior `Embedded_Assessments`.

Adaptar las rutas locales de Zephyr, SDK y herramientas en `.vscode/` y
`scripts/` antes de compilar en otro equipo. Mantener las versiones y la
revision registradas en [la guia de D1](docs/README_DESARROLLO.md).

- `Ctrl+Shift+B` / `West Build (auto)`: compilar la base.
- `West Build (pristine)`: generar una compilacion limpia.
- `Flash (CubeProgrammer, UR+HWrst)`: programar la placa.
- `Debug Zephyr - NUCLEO-WL55JC`: depurar con GDB/OpenOCD.

La UART del ST-LINK queda reservada para la aplicacion; produccion desactiva
consola, printk, banner, shell y logs.

## Trabajo paralelo

| Bloque | Responsable | Punto de integracion |
| --- | --- | --- |
| Drivers, estimacion, estadisticas y adquisicion | David | `include/acquisition_api.h` |
| GPS y parser NMEA | Pablo | `include/gps_api.h` |
| Formatos SIMU y humano | Pablo | `include/simu_frame.h`, `include/human_report.h` |
| Aplicacion, UART, modos, estado y RGB | Pablo | Tipos de `include/model.h` |

Los contratos se declaran en `include/`; sus implementaciones corresponden a
D2-D4 y P1-P4. Las carpetas de `src/` contienen indicaciones de responsabilidad,
sin servicios ficticios ni datos simulados en produccion.

Las fuentes se incorporan al CMake raiz cuando exista una implementacion
utilizable. Pablo mantiene `main`, CMake, configuracion y overlay tras D1;
los cambios de contratos compartidos se coordinan entre ambos.

## Estado y documentacion

La base permite iniciar P1 (GPS) y P2 (formatos y salida). Quedan pendientes
la revision de contratos por Pablo y las comprobaciones en su equipo/placa.
Los avances de sensores D2 se conservan aparte de esta entrega.

- [Guia de D1, comandos y pendientes](docs/README_DESARROLLO.md).
- [Playbook general Zephyr en Windows](docs/Zephyr_UPM_Playbook.md).
- [Organizacion de pruebas](tests/README.md).
