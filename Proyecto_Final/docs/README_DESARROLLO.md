# Base D1 y trabajo paralelo

## Alcance publicado

D1 aporta CMake, configuracion de placa, comprobaciones de produccion,
main minimo observable con LED/GDB y los headers compartidos. Los servicios
y formateadores estan declarados y se implementan en los siguientes hitos.
La base no se presenta como firmware final ni como validacion de sensores.

Las correcciones de configuracion se aplican a la base comun: las guardas de
produccion son incondicionales, se retiran los chosen de consola y el overlay
conserva los valores que ya proporciona la placa. No se necesita un Kconfig
propio ni un simbolo para desactivar las guardas.

## Entorno fijado

| Componente | Revision |
| --- | --- |
| Placa / target | NUCLEO-WL55JC / `nucleo_wl55jc/stm32wl55xx` |
| Zephyr | 4.4.2, commit `dccb09599635bdff17633fa7e9dab014b91dce90` |
| Zephyr SDK / ARM GNU | 1.0.1 / 14.3.0 |
| Python / west | 3.12.10 / 1.5.0 |
| CMake / Ninja / DTC | 4.4.3 / 1.13.2 / 1.6.1 |

En el equipo de David, Zephyr esta en `C:/ZephyrUPM/zephyrproject-4.4/zephyr`
y el SDK en `C:/Users/davma/zephyr-sdk-1.0.1`. Pablo adapta esas rutas en
`.vscode/settings.json`, `.vscode/tasks.json` y `scripts/` conservando versiones.
La referencia docente a 4.4.99 queda pendiente de reconciliar; esta base
reproduce la revision 4.4.2 registrada arriba.

## Compilacion desde una copia limpia

Abrir `Proyecto_Final.code-workspace` desde la raiz del repo. El workspace
selecciona `Proyecto_Final` para las tareas y detecta el repositorio Git padre.

Alternativa manual, empezando en la carpeta `Proyecto_Final`:

```powershell
$projectDir = (Resolve-Path .).Path.Replace('\', '/')
$zephyrDir = 'C:/ZephyrUPM/zephyrproject-4.4'
$env:ZEPHYR_BASE = "$zephyrDir/zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = 'C:/Users/davma/zephyr-sdk-1.0.1'
Set-Location $env:ZEPHYR_BASE
& "$zephyrDir/.venv/Scripts/west.exe" build -p always -b nucleo_wl55jc/stm32wl55xx $projectDir -d "$projectDir/build" -- '-DCONF_FILE=confs/prj_nucleo_wl55jc.conf' '-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay'
```

Las tareas de programacion y depuracion usan `build/zephyr/zephyr.hex` y
`build/zephyr/zephyr.elf` de esa misma carpeta. Los directorios de compilacion,
entornos y archivos generados quedan excluidos de Git.

## Configuracion que debe mantenerse

- Consola, UART/stdout console, printk, banner, shell y logs desactivados.
- PM desactivado; SysTick, tickless, timeout de 64 bits y 10000 ticks/s.
- UART por interrupciones y configuracion en ejecucion.
- I2C2 a 100 kHz, timeout STM32 de 5 ms y recuperacion mediante GPIO.
- USART1 RX PB7 a 9600 para GPS; LPUART1 PA2/PA3 a 115200 para la aplicacion.
- ADC1 canal 5/PB1; RGB PB5/PB8/PB10; GPIO, IWDG, HWINFO y proteccion de pila.

`src/build_checks.c` rechaza una produccion con consola/logs o chosen de
consola habilitados. Los valores efectivos se inspeccionan en `build/zephyr/.config`
y `build/zephyr/zephyr.dts` tras compilar.

## Contratos para Pablo

`include/model.h` es la unica definicion compartida de tipos, unidades,
validez, fechas, progreso, navegacion, estadisticas, modos, estado y flags.
Las seis operaciones publicas se declaran en estos headers:

| Header | Operaciones |
| --- | --- |
| `acquisition_api.h` | `acquisition_snapshot_get`, `acquisition_stats_begin`, `acquisition_stats_end` |
| `gps_api.h` | `gps_snapshot_get` |
| `simu_frame.h` | `simu_frame_encode` |
| `human_report.h` | `human_report_format` |

El main incluye todos los headers, que se comprueban durante el build.
Los consumidores usan estos contratos y sus copias de snapshots; los detalles
privados de drivers, locks y algoritmos permanecen fuera de los headers publicos.

## Carpetas y propiedad

| Carpeta / archivo | Responsable |
| --- | --- |
| `src/drivers`, `src/estimator`, `src/statistics`, `src/acquisition` | David |
| `src/gps`, `src/telemetry`, `src/app` | Pablo |
| `src/main.c`, CMake raiz, `confs/`, `boards/` despues de D1 | Pablo |
| `include/` | Ambos; cambios coordinados |
| `tests/`, `docs/` | Cada autor en su bloque y revision cruzada |

Los README de cada carpeta conservan la estructura en Git. Los futuros
ensayos incorporan fuentes reales mediante su propio CMake/configuracion;
los dobles de prueba se limitan a `tests/`.

## Empezar P1/P2 desde esta base

Para crear una rama nueva en un clon existente:

```powershell
git fetch origin
git switch -c feat/gps-application origin/feat/d1-base
```

Pablo puede empezar el parser/servicio GPS y los formatos SIMU/humano sin
esperar los detalles privados de D2-D4. Los servicios se enlazan cuando lleguen;
los formateadores puros se pueden probar con entradas controladas en `tests/`.

## Pendientes de aceptacion

- Revision por Pablo de tipos, propiedad, configuracion y seis operaciones.
- Compilacion desde copia limpia en su equipo y programacion/observacion en placa.
- Verificacion de identidad Git de Pablo y aclaracion de la version docente.
- D2-D4 y P1-P4: sus implementaciones y pruebas no se dan por terminadas en D1.

## Validacion realizada por David (26/10/2026)

- Compilacion pristine desde un worktree nuevo de D1: codigo de salida 0.
- Target confirmado: `nucleo_wl55jc/stm32wl55xx`, Zephyr 4.4.2 y GNU 14.3.0.
- Inspeccion de 26 valores efectivos de produccion en `.config`: correctos.
- Las guardas de consola y chosen se compilaron de forma incondicional.
- Generados `zephyr.elf` y `zephyr.hex`: FLASH 20676 B, RAM 5632 B.
- GDB abre el ELF, encuentra `main` en `0x0800042c` y reconoce `armv7e-m`.
- Los cinco headers compartidos coinciden con los del trabajo actual de David.
- `Assessment_1` permanece intacto; esta entrega no incluye los drivers D2.

Estas comprobaciones permiten compartir la base. La revision de Pablo y la
programacion/observacion desde su copia limpia siguen pendientes.
