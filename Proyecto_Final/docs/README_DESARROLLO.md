# Desarrollo del firmware

## Estado del proyecto

Partimos de D1, ya integrado en `master`, con la configuración de placa y los
contratos públicos compartidos. Tenemos los drivers de IMU, luz y ambiente,
y la calibración inicial del giroscopio. El ensayo independiente de sensores
usa MPU6050 y Si7021; aún no incluye el LDR.

Seguimos con el main mínimo de producción. La adquisición integrada, GPS,
formatos, supervisión y estimación de orientación, velocidad y posición
siguen pendientes. Conservamos las APIs y los tipos acordados en D1.

## Entorno de trabajo

| Componente | Versión / revisión |
| --- | --- |
| Placa / target | NUCLEO-WL55JC / `nucleo_wl55jc/stm32wl55xx` |
| Zephyr | 4.4.2, commit `dccb09599635bdff17633fa7e9dab014b91dce90` |
| SDK / ARM GNU | 1.0.1 / 14.3.0 |
| Python / west | 3.12.10 / 1.5.0 |
| CMake / Ninja / DTC | 4.4.3 / 1.13.2 / 1.6.1 |

En el entorno local de David usamos `C:/ZephyrUPM/zephyrproject-4.4/zephyr`
y `C:/Users/davma/zephyr-sdk-1.0.1`. En cada equipo ajustamos esas rutas en
`.vscode/` y `scripts/`, manteniendo las versiones registradas. Nos queda
aclarar la referencia docente a 4.4.99 frente a esta base 4.4.2.

Trabajamos en
`C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final`
con [Proyecto_Final.code-workspace](../../Proyecto_Final.code-workspace).
`${workspaceFolder}` apunta al proyecto y Git a la carpeta superior.
Conservamos el repositorio de la ubicación anterior como respaldo.

## Compilación, programación y depuración

| Perfil | Compilar | Programar | Directorio |
| --- | --- | --- | --- |
| Producción | `West Build (auto)` o `West Build (pristine)` | `Flash (CubeProgrammer, UR+HWrst)` | `build` |
| Ensayo | `West Build (sensores)` | `Flash (Sensores)` | `build-sensors` |

Las tareas están en [.vscode/tasks.json](../.vscode/tasks.json). Compartimos
el entorno en `options.env` y ejecutamos west desde el checkout de Zephyr.
Usamos `confs/prj_nucleo_wl55jc.conf` y `boards/nucleo_wl55jc.overlay` en los
dos perfiles. El CMake del ensayo aplica después su `prj.conf` y `app.overlay`.

Para las compilaciones limpias desde PowerShell, empezamos en `Proyecto_Final`:

```powershell
$projectDir = (Resolve-Path .).Path.Replace('\', '/')
$zephyrDir = 'C:/ZephyrUPM/zephyrproject-4.4'
$env:ZEPHYR_BASE = "$zephyrDir/zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = 'C:/Users/davma/zephyr-sdk-1.0.1'
Set-Location $env:ZEPHYR_BASE
& "$zephyrDir/.venv/Scripts/west.exe" build -p always -b nucleo_wl55jc/stm32wl55xx $projectDir -d "$projectDir/build" -- '-DCONF_FILE=confs/prj_nucleo_wl55jc.conf' '-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay'
& "$zephyrDir/.venv/Scripts/west.exe" build -p always -b nucleo_wl55jc/stm32wl55xx "$projectDir/tests/board/sensors" -d "$projectDir/build-sensors"
```

Con `List COM Ports` identificamos el puerto real y con `Serial Monitor`
abrimos la salida del ensayo a 115200 8N1. En producción reservamos esa UART
para la aplicación y mantenemos desactivada la consola de Zephyr.

Centralizamos las rutas ARM/GDB en `.vscode/settings.json`. El perfil de
Cortex-Debug usa el ELF de producción y el servidor externo en `localhost:3333`.
Tenemos comprobada la lectura de los ELF con GDB; la sesión completa con
ST-LINK, breakpoints y cierre de OpenOCD sigue pendiente en placa.

## Configuración y contratos comunes

Mantenemos los `BUILD_ASSERT` de `src/build_checks.c` solo en producción,
de forma incondicional. Rechazan consola, UART/stdout console, printk,
banner, shell, logs y los chosen de consola de Devicetree. El ensayo no
compila ese archivo y habilita su consola en su propia configuración.

Heredamos `led0`, LPUART1 a 115200 e I2C2 activo de la placa. En el overlay
conservamos I2C2 a 100 kHz y recuperación GPIO en PA12/PA11; GPS RX PB7 a
9600; ADC1 canal 5/PB1 de 12 bits; y RGB activo bajo en PB5/PB8/PB10.
Usamos SysTick, tickless a 10000 ticks/s, timeouts de 64 bits, PM desactivado
y CRC de Zephyr. La configuración efectiva queda en `.config` y `zephyr.dts`
de cada build.

Los tipos viven en `include/model.h`. Compartimos estas seis operaciones:

| Header | Operaciones |
| --- | --- |
| `acquisition_api.h` | `acquisition_snapshot_get`, `acquisition_stats_begin`, `acquisition_stats_end` |
| `gps_api.h` | `gps_snapshot_get` |
| `simu_frame.h` | `simu_frame_encode` |
| `human_report.h` | `human_report_format` |

Conservamos las firmas, las unidades, los snapshots, `progress` y los
contadores de sustituciones. El tamaño SIMU sigue siendo 221 bytes en cable,
con un byte adicional de buffer para NUL.

## Sensores y correcciones tras la primera auditoría

En el Si7021 evitamos escribir el registro de usuario cuando resolución y
calentador ya coinciden con la configuración. Conservamos el camino de
lectura, modificación y verificación cuando hay diferencias.

Usamos `crc8()` de Zephyr sobre los dos bytes originales de humedad, con
polinomio `0x31`, semilla `0x00` y sin reflexión. Ante CRC incorrecto retornamos
`-EBADMSG` y conservamos la salida. Recuperamos la temperatura de la misma
medida con E0 y publicamos el par solo cuando terminan las dos lecturas.

En el ensayo esperamos con `k_msleep(ENVIRONMENT_CONVERSION_WAIT_MS)` desde
que termina F5. Para el futuro vencimiento absoluto de adquisición conservamos
`ceil(40 ms) + 1 tick`, porque el kernel no compensa los timeouts absolutos.

D1 ya incorporó las guardas incondicionales, la retirada del Kconfig propio,
la simplificación del overlay y la centralización de las rutas de depuración.
Conservamos esos cambios al integrar los avances de D2.

## Organización del trabajo

| Ruta | Responsable |
| --- | --- |
| `src/drivers`, `src/algorithms`, `src/estimator`, `src/statistics`, `src/acquisition` | David |
| `src/gps`, `src/telemetry`, `src/app` | Pablo |
| `src/main.c`, CMake raíz, `confs/` y `boards/` tras D1 | Pablo |
| `include/`, `tests/` y `docs/` | Compartidos; coordinamos los cambios |

Para las ramas nuevas partimos de `origin/master`. Mantenemos cada bloque en
su carpeta y enlazamos las fuentes reales en el CMake correspondiente.

## Captura breve en placa (pendiente)

Tenemos pendiente la captura de inicio y unos 20–30 segundos de lecturas del
ensayo. Usamos alimentación de 3.3 V, masa común e I2C2 en PA12/PA11.
Programamos con `Flash (Sensores)`, abrimos el puerto identificado y mantenemos
el MPU quieto y nivelado durante la calibración. Registramos montaje, puerto,
fecha y firmware junto con las lecturas y los posibles errores de bus/CRC.

La captura serie muestra el comportamiento observado; para comprobar el
ahorro de escrituras del init necesitamos una traza I2C o instrumentación
temporal. La inyección de errores y la conservación de salidas tienen sus
pruebas específicas pendientes.

La captura desde PowerShell, con el puerto real, queda en un archivo local:

```powershell
& 'C:/Program Files/PuTTY/plink.exe' -serial COM5 -sercfg '115200,8,n,1,N' | Tee-Object -FilePath "$env:TEMP/drone-sensors-capture.txt"
```

Al terminar volvemos a producción con `Flash (CubeProgrammer, UR+HWrst)`.

## Pendientes

- Validación física de sensores, fallos/recuperación I2C y sesgo del giroscopio.
- Calibración final del LDR y diagnóstico eléctrico; el ensayo actual no lee luz.
- Adquisición, GPS, formatos, snapshots y supervisión integrados.
- Estimación de orientación, velocidad y posición, con su deriva documentada.
- Revisión conjunta de contratos y reproducción del build en el segundo equipo.
- Sesión completa de Cortex-Debug con placa y aclaración de la versión docente.
- Simplificaciones diferidas de la auditoría: init del Si7021, configuración,
  comentarios/constantes e indentación del ensayo.

D2 y D3 siguen abiertos. Conservamos los detalles de las comprobaciones en los
commits y PR; esta guía recoge cómo trabajamos y el estado vigente.
