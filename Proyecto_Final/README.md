# Drone tracker — proyecto final IoT UPM

Firmware para la NUCLEO-WL55JC del proyecto de Embedded Platforms and IoT.
Integra en desarrollo un MPU6050, un Si7021 y un LDR KY-018, con contratos
compartidos para adquisicion, navegacion, GPS y reportes NORMAL/SIMU.

El main de produccion conserva el parpadeo provisional. Los drivers de sensores
y la calibracion del giroscopio estan implementados; el ensayo independiente
permite ejercitar MPU6050 y Si7021 con salida serie. La adquisicion integrada,
GPS, supervision, formateadores y estimador de orientacion, velocidad y posicion
siguen pendientes. D2 y D3 permanecen abiertos, incluida la calibracion final
y el diagnostico electrico del LDR.

## Entorno

- Zephyr 4.4.2: `C:/ZephyrUPM/zephyrproject-4.4/zephyr`.
- Zephyr SDK 1.0.1, Python 3.12 y placa `nucleo_wl55jc/stm32wl55xx`.
- VS Code, STM32CubeProgrammer/ST-LINK y PuTTY/plink para el ensayo.

## Compilar y programar

Abrir `C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final` en VS Code.

Tambien se puede abrir `../Proyecto_Final.code-workspace`. En ambos casos,
`${workspaceFolder}` debe ser `Proyecto_Final` para que las tareas de `.vscode/`
resuelvan correctamente las fuentes y los directorios de compilacion.
La raiz Git es la carpeta superior `Embedded_Assessments`, compartida con
`Assessment_1`.

| Perfil | Compilacion | Programacion | Salida |
| --- | --- | --- | --- |
| Produccion | Ctrl+Shift+B / `West Build (auto)`; limpia: `West Build (pristine)` | `Flash (CubeProgrammer, UR+HWrst)` | `build/zephyr`, sin consola Zephyr |
| Ensayo MPU6050 + Si7021 | `West Build (sensores)` | `Flash (Sensores)` | `build-sensors/zephyr`, consola LPUART1 a 115200 8N1 |

Las tareas comparten el entorno definido en `.vscode/tasks.json`.
Produccion usa `confs/prj_nucleo_wl55jc.conf` y `boards/nucleo_wl55jc.overlay`;
el ensayo aplica despues sus ajustes en `tests/board/sensors/`.
`src/build_checks.c` impide activar consola, printk, banner, shell o logs en
produccion y comprueba la retirada de las asignaciones de consola de Devicetree.

`List COM Ports` permite identificar el puerto real y `Serial Monitor` abrirlo.
La compilacion por si sola no demuestra funcionamiento en placa.

## Documentacion

- [Guia de desarrollo](docs/README_DESARROLLO.md): contratos, estado implementado,
  validaciones realizadas, pendientes y captura breve en placa.
- [Playbook Zephyr en Windows](docs/Zephyr_UPM_Playbook.md): entorno, tareas,
  compilaciones limpias, programacion y depuracion.

Los tipos y contratos publicos estan en `include/`; los drivers privados en
`src/drivers/` y la calibracion en `src/algorithms/`.
