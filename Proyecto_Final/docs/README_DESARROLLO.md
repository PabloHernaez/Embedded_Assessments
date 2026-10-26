# Desarrollo del firmware

## Estado implementado y alcance

La base de placa y los contratos publicos de D1 estan presentes. El main de
produccion conserva el parpadeo provisional; compilar drivers no significa
que ya exista un servicio de adquisicion integrado.

Estan implementados los drivers privados MPU6050, Si7021 y luz analogica,
y la calibracion inicial del giroscopio. El ensayo de placa separado ejercita
MPU6050, calibracion y Si7021, con salida de diagnostico. No ejercita el LDR.
Las funciones de servicios y formateadores de `include/` siguen declaradas,
pendientes de implementacion e integracion. D2 y D3 permanecen abiertos.

Esta refactorizacion conserva las APIs, las estructuras `*Reading` privadas y
`*Data` publicadas, la calibracion del giroscopio, las normas calculadas por el
llamador, la interrupcion de calibracion con `NULL`, los flags y `progress`.
No implementa estimacion ni cambia snapshots o supervision.

## Entorno registrado

- Placa: NUCLEO-WL55JC, Cortex-M4.
- Target: `nucleo_wl55jc/stm32wl55xx`.
- Zephyr: 4.4.2 en `C:/ZephyrUPM/zephyrproject-4.4/zephyr`.
- Commit Zephyr: `dccb09599635bdff17633fa7e9dab014b91dce90`.
- Zephyr SDK: 1.0.1 en `C:/Users/davma/zephyr-sdk-1.0.1`.
- Compilador GNU del firmware: 14.3.0.
- Python: 3.12.10; west: 1.5.0; CMake: 4.4.3; Ninja: 1.13.2; DTC: 1.6.1.

No se ha actualizado Zephyr ni el SDK. En otro equipo, adaptar las rutas de
`.vscode/` y `scripts/`, conservando las versiones y la revision registradas.

## Construccion y programacion

Abrir la raiz del proyecto en VS Code. Las tareas completas estan en
[.vscode/tasks.json](../.vscode/tasks.json); `options.env` comparte el entorno,
y las tareas de west conservan su directorio de trabajo en el checkout Zephyr.

| Perfil | Compilar | Programar | Directorio |
| --- | --- | --- | --- |
| Produccion | Ctrl+Shift+B / `West Build (auto)`; limpia: `West Build (pristine)` | `Flash (CubeProgrammer, UR+HWrst)` | `build` |
| Ensayo | `West Build (sensores)` | `Flash (Sensores)` | `build-sensors` |

Ambos usan `confs/prj_nucleo_wl55jc.conf` y
`boards/nucleo_wl55jc.overlay`. El CMake del ensayo aplica despues su
`prj.conf` y `app.overlay` en `tests/board/sensors/`.
Produccion mantiene explicitos `-DCONF_FILE` y `-DDTC_OVERLAY_FILE`.

Para repetir dos compilaciones limpias desde PowerShell:

```powershell
Set-Location C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final
. ./scripts/zephyr-env.ps1
Set-Location $env:ZEPHYR_BASE
west build -p always -b nucleo_wl55jc C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final -d C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/build -- '-DCONF_FILE=confs/prj_nucleo_wl55jc.conf' '-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay'
west build -p always -b nucleo_wl55jc C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/tests/board/sensors -d C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/build-sensors
```

`Serial Monitor` usa 115200 8N1 y solicita el puerto identificado con
`List COM Ports`. El ensayo restaura la consola LPUART1 (PA2/PA3).
Produccion reserva la UART para la aplicacion y no tiene consola Zephyr.

Las rutas ARM/GDB y el prefijo `arm-zephyr-eabi` se centralizan en
`settings.json`. Se comprobo la extension instalada Cortex-Debug 1.12.1:
`resolveDebugConfiguration` y `setOsSpecficConfigSetting`, en su
`dist/extension.js`, heredan `cortex-debug.armToolchainPath` a `toolchainPath`,
`cortex-debug.armToolchainPrefix` a `toolchainPrefix` y `cortex-debug.gdbPath`
a `gdbPath` cuando el perfil no los especifica. Por ello se retiran los tres
valores duplicados de `launch.json`, conservando su servidor externo y tareas.

El 26/10/2026 se verifico que el GDB configurado abre el ELF de produccion,
encuentra `main` en `0x0800042c` y reconoce `armv7e-m`. OpenOCD del SDK tambien
ejecuta `--version` correctamente. Estas comprobaciones no conectan con la
placa; quedan pendientes el arranque completo desde VS Code, los breakpoints
y la terminacion de la sesion con ST-LINK.

## Configuracion y contratos preservados

`src/build_checks.c` se compila solo en produccion. Sus `BUILD_ASSERT`
incondicionales rechazan consola, UART/stdout console, printk, banner, shell,
logs y las asignaciones `zephyr,console` / `zephyr,shell-uart` de Devicetree.
El ensayo no compila ese archivo. Se retira el simbolo propio de produccion,
su Kconfig raiz y el `KCONFIG_ROOT` del ensayo: no quedaba configuracion propia.

La DTS local ya proporciona `led0 = &green_led_2`, LPUART1 activo a 115200 e
I2C2 activo; solo se retiran esas declaraciones repetidas del overlay. Se
conservan I2C2 a 100 kHz, PA12/PA11 y GPIO open-drain para recuperacion;
USART1 RX PB7 a 9600 con pull-up; ADC1 canal 5 PB1 de 12 bits con sus tamanos
de direccion y canal; RGB activo bajo PB5/PB8/PB10 y retirada de consola.

`SERIAL=y` y `GPIO=y` se heredan del defconfig de placa. Siguen explicitos
SysTick, tickless a 10000 ticks/s, timeouts de 64 bits, PM desactivado,
MPU/proteccion de pila, UART por interrupciones/configuracion en ejecucion,
WATCHDOG y HWINFO. `CONFIG_CRC=y` pertenece a la base comun.

`SIMU_FRAME_WIRE_SIZE` sigue siendo 221; el buffer deriva un byte adicional
para NUL. No cambian firmas, `cap`, `len`, `replacements`, capacidad
insuficiente ni `simu_replaced_fields`.

## Secuencia del Si7021

`environment_init()` comprueba el controlador I2C y lee el registro de usuario
para comprobar tambien la respuesta y configuracion del sensor. Si RES1/RES0
y HTRE ya estan a cero (RH12/T14, calentador apagado), termina sin escribir.
En otro caso conserva los demas bits, escribe y verifica los relevantes.
Retorna el primer error de transferencia o `-EIO` si la configuracion no queda
aplicada. No incorpora reset ni nuevas esperas.

`environment_start()` envia F5; `environment_read()` valida el CRC de los dos
bytes originales de humedad con `crc8(..., 2U, 0x31U, 0x00U, false)`, antes de
quitar bits de estado. Un CRC incorrecto retorna `-EBADMSG`, no lee temperatura
y conserva la salida. E0 recupera dos bytes de temperatura de la misma medida,
sin CRC ni nueva conversion. Solo se publica el par al completar ambas lecturas.

La espera relativa del ensayo es `k_msleep(ENVIRONMENT_CONVERSION_WAIT_MS)`.
Se contrasto con el arbol local:

- `include/zephyr/kernel.h`: `k_msleep` llama a `k_sleep(Z_TIMEOUT_MS(ms))`.
- `include/zephyr/sys/clock.h`: `Z_TIMEOUT_MS` usa `k_ms_to_ticks_ceil64`
  con los timeouts de 64 bits configurados.
- `kernel/timeout.c`, `z_add_timeout`: el caso relativo suma un tick para
  compensar la fraccion de tick actual; el caso absoluto no lo suma.

A 10000 ticks/s, 40 ms son 400 ticks. La espera relativa recibe esa compensacion
del kernel y puede prolongarse por planificacion del hilo; presupone que nadie
lo despierte antes con `k_wakeup`, como ocurre en este ensayo. El futuro
`ready_at` absoluto debe conservar `ceil(40 ms) + 1 tick`, calculado despues
de que termine F5. Retirar el tick manual del ensayo no elimina ese margen.

## Comprobaciones ejecutadas — 26/10/2026

Las dos construcciones con `west build -p always` terminaron con codigo 0,
sin avisos ni errores de compilacion/configuracion en sus logs:

| Perfil | Flash usada | RAM usada | Resultado |
| --- | --- | --- | --- |
| Produccion (`build`) | 20676 B | 5632 B | ELF/HEX generados |
| Ensayo (`build-sensors`) | 33152 B | 6656 B | ELF/HEX generados |

Se comprobaron las `.config` efectivas de ambos perfiles: CRC activo;
consola/UART console/printk desactivados en produccion y activos en ensayo;
stdout console, banner, shell y logs desactivados en ambos. Tambien se
verificaron SERIAL/GPIO heredados, reloj, timeouts, energia, pilas, I2C/ADC,
UART_INTERRUPT_DRIVEN, WATCHDOG y HWINFO. Frente a las configuraciones previas,
el unico cambio de valor efectivo es CRC activo y la retirada del simbolo
propio de produccion donde estaba activo.

Se compararon todos los nodos y propiedades de ambos `zephyr.dts` contra las
copias anteriores: son semanticamente iguales. Ademas se comprobaron los
pines, velocidades, GPIO de recuperacion, canal ADC, RGB, alias `led0` y
asignaciones de consola descritos arriba.

`compile_commands.json` confirma `build_checks.c` solo en produccion y el
`crc8_sw.c` local en ambos perfiles. Dos probes temporales, con el mismo
compilador y opciones de produccion, verificaron el rechazo por `BUILD_ASSERT`
al forzar consola o una asignacion `zephyr,console`; el caso correcto compilo.

Se compararon las tareas con su estado inicial: etiquetas, argumentos,
dependencias, directorios de trabajo y tarea predeterminada se conservan;
solo se centraliza el entorno. El ejemplo del playbook coincide con el archivo
operativo. No se han probado las tareas de flash, monitor o depuracion en placa.

El probe temporal del host compila directamente
`subsys/crc/crc8_sw.c` del checkout local de Zephyr, sin copiar ni reimplementar
el algoritmo. Usa MSVC existente solo para ejecutarlo en Windows, con adaptacion
de tipos/cabecera y `__weak`; el firmware sigue usando el compilador del SDK.
Resultado: `68 3A -> 7C`, `68 3B -> 4D`; las 16 alteraciones individuales de un
bit cambian el resultado. No se ha creado un framework de pruebas ni agregado
diagnosticos al main de produccion.

## Archivos de esta refactorizacion

| Archivo | Cambio y motivo |
| --- | --- |
| `src/build_checks.c` | Guardas incondicionales al ser exclusivo de produccion. |
| `Kconfig` (eliminado) | Solo definia el simbolo de produccion retirado. |
| `confs/prj_nucleo_wl55jc.conf` | CRC comun; retira el simbolo y SERIAL/GPIO redundantes del defconfig. |
| `tests/board/sensors/CMakeLists.txt`, `prj.conf` | Retira KCONFIG_ROOT y la asignacion del simbolo; mantiene perfiles superpuestos. |
| `src/drivers/environment.c` | Init sin escritura si ya coincide; CRC de Zephyr y comentarios tecnicos corregidos. |
| `src/drivers/environment.h` | Explica espera relativa y conserva el tick del futuro ready_at absoluto. |
| `tests/board/sensors/src/main.c` | Espera con k_msleep y explicacion de la compensacion del kernel. |
| `boards/nucleo_wl55jc.overlay` | Retira solo alias/estado/LPUART1 identicos a la DTS heredada. |
| `include/simu_frame.h` | Buffer derivado de 221 + 1 y retirada del assert redundante; corrige comentario. |
| `include/model.h` | Mueve clipped a LightData y corrige erratas/comentarios; mismos tipos y campos. |
| `.vscode/tasks.json` | Entorno comun en options.env raiz, conservando tareas y cwd. |
| `.vscode/launch.json` | Tras instalar Cortex-Debug 1.12.1, hereda rutas y prefijo de settings.json sin duplicarlos. |
| `README.md` | Describe el tracker, sus perfiles y enlaces a documentacion. |
| `docs/Zephyr_UPM_Playbook.md` | Ejemplos reales de tareas, rutas, overlays y perfiles. |
| `docs/README_DESARROLLO.md` | Estado, evidencia, limites y procedimiento de captura. |

El CMake de produccion, main de produccion, drivers IMU/luz, calibracion y
contratos restantes se compararon byte a byte con una copia inicial y no se
alteraron. Se mantiene la rama `david/d2-sensores` y el trabajo previo, sin
commits ni push. La revision de whitespace solo detecta los espacios al final
de comentarios `/**` ya presentes en los cambios iniciales de `include/model.h`;
esta refactorizacion no agrega incidencias nuevas.

## Captura breve en placa (pendiente)

1. Con el montaje alimentado a 3.3 V y masa comun, conectar la NUCLEO por
   ST-LINK. Verificar MPU6050 y Si7021 en I2C2 (PA12 SCL, PA11 SDA).
2. Ejecutar `Flash (Sensores)`. Identificar el puerto con `List COM Ports` y
   abrir `Serial Monitor` a 115200 8N1. Reiniciar la placa para capturar el inicio.
3. Mantener el MPU quieto y nivelado hasta completar la calibracion. Guardar la
   salida desde `imu_init: 0` y `environment_init: 0`, el avance/sesgo de
   calibracion y unos 20–30 s de lecturas posteriores (ciclo de unos 2 s).
4. Anotar montaje, puerto, fecha y firmware; comprobar temperatura/humedad
   plausibles y ausencia de errores de bus/CRC. Esa captura no demuestra por
   si sola el ahorro de escrituras del init: para observarlo hace falta traza
   I2C o instrumentacion temporal del ensayo. Los errores inyectados y la
   conservacion de salidas requieren una prueba especifica.
5. Cerrar el monitor; usar `Flash (CubeProgrammer, UR+HWrst)` para volver a
   produccion. El main provisional debe parpadear sin diagnosticos Zephyr.

En PowerShell puede guardarse la captura, sustituyendo COM5 por el puerto real,
detenida con Ctrl+C despues del intervalo deseado:

```powershell
& 'C:/Program Files/PuTTY/plink.exe' -serial COM5 -sercfg '115200,8,n,1,N' | Tee-Object -FilePath "$env:TEMP/drone-sensors-capture.txt"
```

No se ha flasheado ni realizado una captura de hardware durante esta
refactorizacion. Una compilacion y un probe del host no son pruebas de placa.

## Pendientes

- Pruebas fisicas de sensores, errores/recuperacion I2C y sesgo del giroscopio.
- Calibracion final del LDR y diagnostico electrico del montaje; el ensayo
  actual no incluye lectura de luz.
- Integracion de adquisicion, GPS, snapshots, supervision y formateadores.
- Estimador de orientacion, velocidad y posicion, y estudio/documentacion de
  su deriva. Velocidad y posicion siguen siendo requisitos obligatorios del
  apartado 2.4.9 del enunciado; no se retiran esos campos.
- Revision de contratos por Pablo y construccion desde copia limpia por el
  otro integrante.
- Aclarar la referencia docente a Zephyr 4.4.99 frente al 4.4.2 registrado.
- Validar la sesion completa de Cortex-Debug con placa: detenerse en main,
  usar breakpoints y comprobar el cierre de OpenOCD.

No se modifica el enunciado ni documentos congelados de diseno, y no se declara
cerrado D2 ni D3.

## Verificacion del traslado al repositorio compartido (26/10/2026)

La ubicacion de trabajo es
`C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final`.
El [workspace de VS Code](../../Proyecto_Final.code-workspace) abre esta carpeta
como `${workspaceFolder}`; Git pertenece a la raiz `Embedded_Assessments`.

Se importo el historial original y el estado actual de los archivos, incluidos
los drivers, el ensayo de sensores y la eliminacion pendiente de `Kconfig`.
El repositorio original se conserva como respaldo en su ubicacion anterior.

Desde la nueva ruta se regeneraron los directorios `build` y `build-sensors`.
Las dos compilaciones terminaron con codigo 0, sin avisos, y produjeron ELF/HEX.
El GDB configurado abrio ambos ELF, encontro `main` y reconocio `armv7e-m`.
Las pruebas en placa siguen pendientes segun los apartados anteriores.
