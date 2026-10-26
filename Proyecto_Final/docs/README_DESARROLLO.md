# Desarrollo del firmware

## Estado de la base D1

Configuración de placa, guardas de producción y contratos públicos
compilados correctamente.

El main conserva el parpadeo provisional de la plantilla.
Las funciones de los servicios y formateadores están declaradas;
sus implementaciones corresponden a los siguientes hitos.

## Entorno registrado

- Placa: NUCLEO-WL55JC, Cortex-M4.
- Target: nucleo_wl55jc/stm32wl55xx.
- Zephyr: 4.4.2.
- Commit Zephyr: dccb09599635bdff17633fa7e9dab014b91dce90.
- Zephyr SDK: 1.0.1.
- Compilador GNU: 14.3.0.
- Python: 3.12.10.
- west: 1.5.0.
- CMake: 4.4.3.
- Ninja: 1.13.2.
- DTC: 1.6.1.

## Construcción y programación

Abrir la raíz del proyecto en VS Code.

- Compilación habitual: Ctrl+Shift+B, West Build (auto).
- Primera construcción desde una copia limpia: West Build (pristine).
- Programación: Flash (CubeProgrammer, UR+HWrst).

Las tareas utilizan:
- confs/prj_nucleo_wl55jc.conf
- boards/nucleo_wl55jc.overlay

En otro equipo, adaptar las rutas locales de Zephyr, SDK y herramientas
en .vscode y scripts. Mantener la revisión registrada de Zephyr.

## Contratos y configuración

Los tipos y contratos públicos viven en include/.
Las comprobaciones de producción viven en src/build_checks.c.

La UART del ST-LINK queda reservada para la salida de la aplicación.

## Pendientes de revisión

- Revisión de los contratos por Pablo.
- Construcción de la base desde una copia limpia por el otro integrante.
- Aclarar la referencia a Zephyr 4.4.99 del material docente frente
  a la versión 4.4.2 utilizada en esta base.