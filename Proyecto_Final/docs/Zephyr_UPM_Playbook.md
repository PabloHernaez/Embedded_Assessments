# Nuestro entorno Zephyr en Windows

> **Entorno objetivo actual**
>
> - **Zephyr OS:** 4.4.2
> - **Zephyr SDK:** 1.0.1
> - **Python:** 3.12
> - **Board:** `nucleo_wl55jc` / STM32WL55JC
> - **IDE:** Visual Studio Code
> - **Build tool:** `west` + CMake + Ninja
> - **Flashing:** STM32CubeProgrammer / ST-LINK
> - **Sistema operativo host:** Windows

Aquí recogemos el entorno Zephyr que usamos en `Proyecto_Final`, las tareas y los comandos de trabajo. Mantenemos el estado del firmware y los pendientes en [README_DESARROLLO.md](README_DESARROLLO.md).

Compartimos la instalación de Zephyr, el SDK y el entorno virtual entre prácticas. Mantenemos las fuentes y los resultados de compilación dentro de cada proyecto.

---

# 1. Organización del entorno

Organizamos la instalación en tres capas:

```text
C:\ZephyrUPM
│
├── zephyrproject-4.4
│   ├── .venv
│   ├── zephyr
│   ├── modules
│   ├── bootloader
│   └── ...
│
├── templates
│   └── upm-zephyr-project
│
└── workspace
    ├── blink_basic
    ├── brightness_lab
    ├── imu_test
    └── assesment_ldr_final
        └── Embedded_Assessments
            ├── Assessment_1
            └── Proyecto_Final
```

Y el SDK vive aparte:

```text
C:\Users\<usuario>\zephyr-sdk-1.0.1
```

Ruta local del SDK:

```text
C:\Users\davma\zephyr-sdk-1.0.1
```

## Qué significa cada carpeta

### `zephyrproject-4.4`

Es la **infraestructura Zephyr**.

Contiene:

- el código fuente de Zephyr;
- módulos externos;
- HALs;
- herramientas;
- el entorno virtual de Python;
- `west`.

Mantenemos las fuentes de las prácticas en `workspace`, separadas de la infraestructura Zephyr.

### `workspace`

Aquí mantenemos los proyectos de las prácticas.

Ejemplo:

```text
C:\ZephyrUPM\workspace\imu_test
```

Separamos los directorios de fuentes y de compilación de cada proyecto.

### `templates`

Usamos esta ruta para la plantilla de partida:

```text
C:\ZephyrUPM\templates\upm-zephyr-project
```

A partir de esa plantilla creamos las prácticas nuevas.

---

# 2. Versiones de trabajo

Mantenemos emparejadas las versiones de Zephyr, SDK y build.

La pareja actual de trabajo es:

```text
Zephyr 4.4.2
+
Zephyr SDK 1.0.1
```

Las siguientes combinaciones quedan fuera de nuestro entorno:

```text
Zephyr 4.4.2
+
SDK 0.17.0
```

ni:

```text
west del venv 4.4
+
ZEPHYR_BASE apuntando a Zephyr 4.1
```

Eso genera entornos híbridos difíciles de diagnosticar.

---

# 3. Verificación rápida del entorno

Al empezar una práctica comprobamos el entorno:

```powershell
C:\ZephyrUPM\zephyrproject-4.4\.venv\Scripts\Activate.ps1
```

El entorno activo queda identificado por una salida como esta:

```text
(.venv) PS C:\...
```

Comprobamos:

```powershell
python --version
west --version
cmake --version
ninja --version
git --version
```

También comprobamos:

```powershell
$env:ZEPHYR_BASE = "C:\ZephyrUPM\zephyrproject-4.4\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\Users\davma\zephyr-sdk-1.0.1"
```

Inspeccionamos las variables con:

```powershell
Get-ChildItem Env:ZEPHYR*
```

En nuestro entorno las rutas corresponden a Zephyr 4.4.2 y SDK 1.0.1.

---

# 4. Creación de proyectos a partir de la plantilla

## Copia de la plantilla

Con la plantilla disponible en:

```text
C:\ZephyrUPM\templates\upm-zephyr-project
```

creamos una carpeta de trabajo, por ejemplo:

```powershell
Copy-Item `
  "C:\ZephyrUPM\templates\upm-zephyr-project" `
  "C:\ZephyrUPM\workspace\imu_test" `
  -Recurse
```

Después abrimos el proyecto:

```powershell
code C:\ZephyrUPM\workspace\imu_test
```

---

# 5. Qué significa `${workspaceFolder}`

`${workspaceFolder}` es una variable propia de VS Code.

Usamos el valor que asigna VS Code a la carpeta abierta.

VS Code la asigna automáticamente a la carpeta que se abrió mediante:

```text
File → Open Folder...
```

Ejemplo:

Cuando abrimos:

```text
C:\ZephyrUPM\workspace\blink_basic
```

entonces:

```text
${workspaceFolder}
```

significa:

```text
C:\ZephyrUPM\workspace\blink_basic
```

Si abrimos otro proyecto:

```text
C:\ZephyrUPM\workspace\imu_test
```

entonces exactamente el mismo `tasks.json` interpreta `${workspaceFolder}` como:

```text
C:\ZephyrUPM\workspace\imu_test
```

Por eso usamos `${workspaceFolder}` para las rutas del proyecto en las tareas.

---

# 6. Estructura de este proyecto

```text
Proyecto_Final/
├── .vscode/             settings.json, tasks.json, launch.json
├── boards/             nucleo_wl55jc.overlay
├── confs/              prj_nucleo_wl55jc.conf (base comun)
├── include/            contratos compartidos
├── src/
│   ├── main.c          parpadeo provisional de produccion
│   ├── build_checks.c  invariantes de produccion
│   ├── drivers/        MPU6050, Si7021 y LDR
│   └── algorithms/     calibracion del giroscopio
├── tests/board/sensors/ CMakeLists.txt, prj.conf, app.overlay, src/main.c
├── CMakeLists.txt
├── build/              generado: produccion
└── build-sensors/      generado: ensayo
```

Los dos directorios de build son generados y estan excluidos de Git.

---

# 7. Fuentes CMake y perfiles

El [CMake de produccion](../CMakeLists.txt) compila `src/main.c`,
`src/build_checks.c`, los tres drivers y la calibracion del giroscopio.
Tenemos pendientes las implementaciones de los servicios y formateadores declarados en `include/`.

El [CMake del ensayo](../tests/board/sensors/CMakeLists.txt) compila su propio
`src/main.c`, los drivers IMU/ambiente y la calibracion; excluye `build_checks.c`.
Carga primero la configuracion y el overlay comunes y despues `prj.conf` y
`app.overlay` del ensayo. No necesita un `KCONFIG_ROOT` propio.

Los `BUILD_ASSERT` de produccion son incondicionales. No hay un simbolo Kconfig
propio para desactivarlos: la separacion de ejecutables se hace en CMake.

---

# 8. Nuestra configuración de `settings.json`

Archivo:

```text
.vscode/settings.json
```

Contenido:

```jsonc
{
  "terminal.integrated.env.windows": {
    "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",

    "ZEPHYR_SDK_INSTALL_DIR":
      "C:/Users/davma/zephyr-sdk-1.0.1",

    "ZEPHYR_BASE":
      "C:/ZephyrUPM/zephyrproject-4.4/zephyr",

    "ZEPHYR_DIR":
      "C:/ZephyrUPM/zephyrproject-4.4"
  },

  "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json",

  "cortex-debug.armToolchainPath":
    "C:/Users/davma/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin",

  "cortex-debug.armToolchainPrefix":
    "arm-zephyr-eabi",

  "cortex-debug.gdbPath":
    "C:/Users/davma/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb.exe"
}
```

Las tareas resuelven el directorio del proyecto con `${workspaceFolder}`.

Las rutas ARM/GDB y el prefijo `arm-zephyr-eabi` se mantienen en
[settings.json](../.vscode/settings.json). Comprobamos en el código de la
extension instalada Cortex-Debug 1.12.1 que, si el perfil no los especifica,
hereda `armToolchainPath`, `armToolchainPrefix` y `gdbPath` de la configuracion
`cortex-debug`. El prefijo se asigna a `toolchainPrefix` y la ruta ARM a
`toolchainPath` del depurador. Por ello [launch.json](../.vscode/launch.json)
ya no repite esos tres valores y conserva el servidor externo en
`localhost:3333`, las tareas y la parada en `main`.

GDB abre el ELF de produccion y encuentra `main`; OpenOCD del SDK ejecuta
`--version`. Queda pendiente probar la sesion completa desde VS Code con la
placa conectada: F5, parada en `main`, breakpoint y cierre de OpenOCD.

---

# 9. `tasks.json` del proyecto

Archivo operativo: [.vscode/tasks.json](../.vscode/tasks.json).
`options.env` en la raiz comparte el entorno entre tareas; cada tarea de west
conserva su `options.cwd` en el checkout Zephyr 4.4.2.

- Ctrl+Shift+B: `West Build (auto)`, produccion predeterminada en `build`.
- `West Build (pristine)`: produccion con `-p always`.
- `West Build (sensores)`: ensayo en `build-sensors`, con `-p auto`.
- `Flash (CubeProgrammer, UR+HWrst)`: compila y programa produccion.
- `Flash (Sensores)`: compila y programa el ensayo.
- `List COM Ports` y `Serial Monitor`: puerto real de ST-LINK, 115200 8N1.
- `Zephyr: Debug Server` y `Zephyr: Stop Debug Server`: depuracion de produccion.
- `Clean Build`: solo elimina `build`; `Clean Zephyr Cache`: cache compartida.

La consola de Zephyr solo esta habilitada en el ensayo. El main provisional
de produccion parpadea y no emite diagnosticos por serie.

```jsonc
{
  "version": "2.0.0",
  "options": {
    "env": {
      "ZEPHYR_BASE": "C:/ZephyrUPM/zephyrproject-4.4/zephyr",
      "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
      "ZEPHYR_SDK_INSTALL_DIR": "C:/Users/davma/zephyr-sdk-1.0.1"
    }
  },

  "tasks": [
    {
      "label": "West Build (auto)",
      "type": "process",
      "command": "C:/ZephyrUPM/zephyrproject-4.4/.venv/Scripts/west.exe",
      "args": [
        "build",
        "-p", "auto",
        "-b", "nucleo_wl55jc",
        "${workspaceFolder}",
        "-d", "${workspaceFolder}/build",
        "--",
        "-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay",
        "-DCONF_FILE=confs/prj_nucleo_wl55jc.conf"
      ],
      "options": {
        "cwd": "C:/ZephyrUPM/zephyrproject-4.4/zephyr"
      },
      "group": {
        "kind": "build",
        "isDefault": true
      },
      "problemMatcher": ["$gcc"],
      "presentation": {
        "reveal": "always",
        "panel": "shared",
        "clear": true
      }
    },

    {
      "label": "West Build (sensores)",
      "type": "process",
      "command": "C:/ZephyrUPM/zephyrproject-4.4/.venv/Scripts/west.exe",
      "args": [
        "build",
        "-p", "auto",
        "-b", "nucleo_wl55jc",
        "${workspaceFolder}/tests/board/sensors",
        "-d", "${workspaceFolder}/build-sensors"
      ],
      "options": {
        "cwd": "C:/ZephyrUPM/zephyrproject-4.4/zephyr"
      },
      "problemMatcher": ["$gcc"],
      "presentation": {
        "reveal": "always",
        "panel": "shared",
        "clear": true
      }
    },

    {
      "label": "West Build (pristine)",
      "type": "process",
      "command": "C:/ZephyrUPM/zephyrproject-4.4/.venv/Scripts/west.exe",
      "args": [
        "build",
        "-p", "always",
        "-b", "nucleo_wl55jc",
        "${workspaceFolder}",
        "-d", "${workspaceFolder}/build",
        "--",
        "-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay",
        "-DCONF_FILE=confs/prj_nucleo_wl55jc.conf"
      ],
      "options": {
        "cwd": "C:/ZephyrUPM/zephyrproject-4.4/zephyr"
      },
      "problemMatcher": ["$gcc"],
      "presentation": {
        "reveal": "always",
        "panel": "shared",
        "clear": true
      }
    },

    {
      "label": "Flash (CubeProgrammer, UR+HWrst)",
      "type": "process",
      "command":
        "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
      "args": [
        "--connect",
        "port=swd",
        "mode=UR",
        "reset=HWrst",
        "--download",
        "${workspaceFolder}/build/zephyr/zephyr.hex",
        "--start"
      ],
      "dependsOn": ["West Build (auto)"],
      "dependsOrder": "sequence",
      "problemMatcher": [],
      "presentation": {
        "reveal": "always",
        "panel": "shared"
      }
    },

    {
      "label": "Flash (Sensores)",
      "type": "process",
      "command":
        "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
      "args": [
        "--connect",
        "port=swd",
        "mode=UR",
        "reset=HWrst",
        "--download",
        "${workspaceFolder}/build-sensors/zephyr/zephyr.hex",
        "--start"
      ],
      "dependsOn": ["West Build (sensores)"],
      "dependsOrder": "sequence",
      "problemMatcher": [],
      "presentation": {
        "reveal": "always",
        "panel": "shared"
      }
    },

    {
      "label": "List COM Ports",
      "type": "process",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        "Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name, Description"
      ],
      "problemMatcher": []
    },

    {
      "label": "Serial Monitor",
      "type": "process",
      "command": "C:/Program Files/PuTTY/plink.exe",
      "args": [
        "-serial",
        "${input:serialPort}",
        "-sercfg",
        "115200,8,n,1,N"
      ],
      "problemMatcher": [],
      "presentation": {
        "reveal": "always",
        "panel": "dedicated",
        "clear": true
      }
    },

    {
      "label": "Clean Build",
      "type": "process",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        "Remove-Item -Recurse -Force \"${workspaceFolder}/build\" -ErrorAction SilentlyContinue"
      ],
      "problemMatcher": []
    },

    {
      "label": "Clean Zephyr Cache",
      "type": "process",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        "Remove-Item -Recurse -Force \"C:/ZephyrUPM/zephyrproject-4.4/zephyr/.cache\" -ErrorAction SilentlyContinue"
      ],
      "problemMatcher": []
    },

    {
      "label": "Zephyr: Debug Server",
      "type": "process",
      "command": "C:/ZephyrUPM/zephyrproject-4.4/.venv/Scripts/west.exe",
      "args": [
        "debugserver",
        "-d",
        "${workspaceFolder}/build"
      ],
      "options": {
        "cwd": "C:/ZephyrUPM/zephyrproject-4.4/zephyr"
      },
      "dependsOn": ["West Build (auto)"],
      "dependsOrder": "sequence",
      "isBackground": true,
      "problemMatcher": {
        "owner": "zephyr-debug",
        "pattern": {
          "regexp": "^(.*)$",
          "message": 1
        },
        "background": {
          "activeOnStart": true,
          "beginsPattern": ".*(Open On-Chip Debugger|OpenOCD).*",
          "endsPattern": ".*(Listening on port 3333 for gdb connections|OpenOCD GDB server running on port 3333).*"
        }
      },
      "presentation": {
        "reveal": "always",
        "panel": "dedicated",
        "clear": true
      }
    },

    {
      "label": "Zephyr: Stop Debug Server",
      "type": "process",
      "command": "taskkill",
      "args": [
        "/F",
        "/IM",
        "openocd.exe"
      ],
      "problemMatcher": [],
      "presentation": {
        "reveal": "silent",
        "close": true
      }
    }
  ],

  "inputs": [
    {
      "id": "serialPort",
      "type": "promptString",
      "description": "Puerto COM de la NUCLEO-WL55JC",
      "default": "COM5"
    }
  ]
}
```

---

# 10. Por qué usamos rutas relativas en `DTC_OVERLAY_FILE` y `CONF_FILE`

Usamos:

```jsonc
"-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay",
"-DCONF_FILE=confs/prj_nucleo_wl55jc.conf"
```

evita un problema de Windows + CMake.

`${workspaceFolder}` se expande normalmente a:

```text
C:\ZephyrUPM\workspace\assesment_ldr_final\Embedded_Assessments\Proyecto_Final
```

Si esa ruta se inserta dentro de un argumento CMake, las barras invertidas pueden interpretarse como secuencias de escape:

```text
\Z
```

y producir errores como:

```text
Invalid character escape '\Z'
```

Por eso:

- `${workspaceFolder}` está bien para el source y el build;
- para variables CMake internas usamos rutas relativas al proyecto.

---

# 11. Overlay y pines efectivos

El [overlay comun](../boards/nucleo_wl55jc.overlay) retira `zephyr,console` y
`zephyr,shell-uart` para produccion. Hereda `led0 = &green_led_2` (PB9), LPUART1
(PA2/PA3, 115200) e I2C2 activo de la DTS de la placa.

Conserva los ajustes propios del montaje:

- I2C2: PA12/PA11, 100 kHz y GPIO open-drain para recuperacion del bus.
- USART1: solo RX en PB7, 9600 y pull-up.
- ADC1: canal 5 en PB1, 12 bits, referencia interna y adquisicion maxima;
  `#address-cells = <1>` y `#size-cells = <0>` para describir el canal.
- RGB activo bajo: PB5, PB8 y PB10.

El [overlay de ensayo](../tests/board/sensors/app.overlay) restaura unicamente
`zephyr,console = &lpuart1`. Revisamos `zephyr.dts` de cada build tras cambiar el hardware.

---

# 12. Configuracion comun y ajustes del ensayo

[confs/prj_nucleo_wl55jc.conf](../confs/prj_nucleo_wl55jc.conf) sirve a ambos
perfiles. Desactiva consola, UART/stdout console, printk, banner, shell y logs
para produccion; activa I2C, recuperacion del bus, ADC y CRC de Zephyr. `SERIAL`
y `GPIO` se heredan del defconfig de la placa.

Se mantienen explicitos el reloj SysTick, tickless a 10000 ticks/s, timeouts de
64 bits, PM desactivado, MPU/proteccion de pila, UART por interrupciones y
configuracion en ejecucion, WATCHDOG y HWINFO.

El [prj.conf del ensayo](../tests/board/sensors/prj.conf) se aplica despues:

```conf
# Perfil de ensayo con salida de diagnostico
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_PRINTK=y

# Imprimir valores con decimales
CONFIG_CBPRINTF_FP_SUPPORT=y

# Margen para el formateo de los mensajes
CONFIG_MAIN_STACK_SIZE=3072
```

Mantenemos separados los ajustes de consola del ensayo y de producción.
Las guardas de producción rechazan consola, logs y las asignaciones de consola
de Devicetree.

---

# 13. Build normal desde VS Code

Atajo:

```text
Ctrl + Shift + B
```

Esto ejecuta:

```text
West Build (auto)
```

Con:

```text
-p auto
```

Zephyr decide cuándo necesita limpiar o regenerar partes del build.

Un build correcto termina aproximadamente con:

```text
[xxx/xxx] Linking C executable zephyr\zephyr.elf

Memory region         Used Size  Region Size  %age Used
FLASH:                   ...
RAM:                     ...

Generating files ...
```

---

# 14. Primer build de un proyecto nuevo

En un proyecto recién creado empezamos con una compilación limpia.

Para esa primera compilación usamos:

```text
-p always
```

o ejecutamos:

```text
Clean Build
```

y luego:

```text
West Build (auto)
```

Después del primer build satisfactorio volvemos a:

```text
-p auto
```

---

# 15. Build manual desde PowerShell

Activamos el entorno:

```powershell
C:\ZephyrUPM\zephyrproject-4.4\.venv\Scripts\Activate.ps1
```

Variables:

```powershell
$env:ZEPHYR_BASE = "C:\ZephyrUPM\zephyrproject-4.4\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\Users\davma\zephyr-sdk-1.0.1"
```

Ejemplo robusto:

```powershell
west build -p always `
  -b nucleo_wl55jc `
  C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final `
  -d C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/build `
  -- `
  "-DDTC_OVERLAY_FILE=C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/boards/nucleo_wl55jc.overlay" `
  "-DCONF_FILE=C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/confs/prj_nucleo_wl55jc.conf"
```

Si PowerShell interpreta mal los argumentos multilínea, usamos una sola línea:

```powershell
west build -p always -b nucleo_wl55jc C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final -d C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/build -- "-DDTC_OVERLAY_FILE=C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/boards/nucleo_wl55jc.overlay" "-DCONF_FILE=C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/confs/prj_nucleo_wl55jc.conf"
```

---

Para una construccion limpia del ensayo, con ese mismo entorno:

```powershell
west build -p always -b nucleo_wl55jc C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/tests/board/sensors -d C:/ZephyrUPM/workspace/assesment_ldr_final/Embedded_Assessments/Proyecto_Final/build-sensors
```

El CMake del ensayo selecciona la base comun y sus ajustes. Para repetir una
captura breve en placa usamos el procedimiento de [desarrollo](README_DESARROLLO.md#captura-breve-en-placa-pendiente).


# 16. Artefactos importantes del build

Después de una compilación correcta:

```text
build
└── zephyr
    ├── zephyr.elf
    ├── zephyr.hex
    ├── zephyr.bin
    ├── zephyr.dts
    ├── .config
    └── ...
```

## `zephyr.elf`

Firmware enlazado con información útil para debugging.

## `zephyr.hex`

Formato habitual para flashear el microcontrolador.

## `zephyr.bin`

Imagen binaria del firmware.

## `zephyr.dts`

Devicetree final después de combinar:

```text
board DTS
+
overlay del proyecto
```

Muy útil para debugging de Devicetree.

## `.config`

Configuración Kconfig final después de combinar:

```text
defconfig de la placa
+
confs/prj_nucleo_wl55jc.conf
+
prj.conf del ensayo (solo en build-sensors)
```

---

# 17. Flashear la NUCLEO-WL55JC

Conectamos la placa por USB para programarla.

Desde VS Code:

```text
Ctrl + Shift + P
→ Tasks: Run Task
→ Flash (CubeProgrammer, UR+HWrst)
```

La tarea `Flash (CubeProgrammer, UR+HWrst)` primero compila produccion.
Para programar `build-sensors/zephyr/zephyr.hex` usamos `Flash (Sensores)`.

Después utiliza:

```text
STM32_Programmer_CLI.exe
```

para cargar:

```text
build/zephyr/zephyr.hex
```

mediante ST-LINK / SWD.

---

# 18. Encontrar el puerto COM

Desde VS Code:

```text
Tasks: Run Task
→ List COM Ports
```

O desde PowerShell:

```powershell
Get-CimInstance Win32_SerialPort |
Select-Object DeviceID, Name, Description
```

Identificamos el dispositivo STMicroelectronics / STLink.

Ejemplo:

```text
COM7    STMicroelectronics STLink Virtual COM Port
```

---

# 19. Monitor serie

Desde VS Code:

```text
Tasks: Run Task
→ Serial Monitor
```

Usamos el puerto identificado, por ejemplo:

```text
COM7
```

o el puerto correspondiente.

Configuración usada:

```text
115200 baud
8 data bits
no parity
1 stop bit
```

Es decir:

```text
115200,8,n,1
```

---

# 20. Limpiar el build

Si aparecen problemas extraños después de:

- cambiar de board;
- cambiar mucho el overlay;
- modificar Kconfig;
- cambiar de versión Zephyr;
- cambiar de SDK;
- modificar estructura del proyecto;

ejecutamos:

```text
Tasks: Run Task
→ Clean Build
```

o:

```powershell
Remove-Item -Recurse -Force .\build
```

Después compilamos de nuevo.

**El directorio `build` es desechable.**

---

# 21. Builds separados por versión de Zephyr

Evitamos reutilizar el mismo build con estas dos versiones:

```text
Zephyr 4.1
    ↓
build/
    ↓
Zephyr 4.4
```

Cuando cambiamos de versión:

```text
Eliminar build
→ compilar de cero
```

Para las pruebas en paralelo separamos los directorios:

```text
build-4.1
build-4.4
```

---

# 22. Comprobar qué Zephyr se está usando realmente

Identificamos la versión del build por esta salida:

```text
Zephyr version: 4.4.2
```

Y rutas como:

```text
C:/ZephyrUPM/zephyrproject-4.4/zephyr
```

La ruta siguiente corresponde al entorno antiguo:

```text
C:/ZephyrUPM/zephyrproject/zephyr
```

si ese directorio corresponde al entorno antiguo 4.1.

La toolchain queda identificada por:

```text
Found toolchain: zephyr 1.0.1
```

No:

```text
zephyr 0.17.0
```

---

# 23. Diagnóstico de versiones mezcladas

Si el log muestra algo parecido a:

```text
Python → zephyrproject-4.4/.venv
Zephyr → zephyrproject/zephyr
SDK → 0.17.0
```

el entorno está mezclado.

Revisamos:

```text
settings.json
tasks.json
ZEPHYR_BASE
ZEPHYR_SDK_INSTALL_DIR
cwd
build/
```

Después:

```text
Clean Build
Developer: Reload Window
```

---

# 24. Smart App Control / Device Guard

En este PC, Windows Smart App Control / Code Integrity ha bloqueado algunas herramientas del SDK en ocasiones.

Síntomas típicos:

```text
ha sido bloqueado por la directiva de Device Guard
```

Para comprobar eventos:

```powershell
Get-WinEvent `
  -LogName "Microsoft-Windows-CodeIntegrity/Operational" `
  -MaxEvents 50 |
Where-Object {
    $_.Id -in 3033,3077,3089
} |
Select-Object TimeCreated, Id, Message |
Format-List
```

Revisamos los eventos de Code Integrity antes de atribuir el fallo a Zephyr:

```text
Zephyr está roto
```

Así distinguimos un fallo del entorno de un ejecutable o DLL bloqueado por Windows.

---

# 25. `CONFIG_OUTPUT_STAT`

En caso de que Device Guard bloquee específicamente:

```text
arm-zephyr-eabi-readelf.exe
```

y solo falle la generación de estadísticas después de producir correctamente:

```text
zephyr.elf
zephyr.hex
zephyr.bin
```

puede desactivarse la generación del `.stat` añadiendo temporalmente:

```conf
CONFIG_OUTPUT_STAT=n
```

al `.conf` del proyecto.

Solo aplicamos ese ajuste temporal cuando tenemos confirmado ese bloqueo.

---

# 26. Comprobar Devicetree

Para saber qué define realmente la board:

```powershell
Select-String `
  -Path "C:\ZephyrUPM\zephyrproject-4.4\zephyr\boards\st\nucleo_wl55jc\nucleo_wl55jc.dts" `
  -Pattern "led_"
```

Ejemplo actual:

```text
blue_led_1
green_led_2
red_led_3
```

Esto ayuda a detectar errores como:

```text
undefined node label
```

---

# 27. Devicetree: regla mental

Si aparece:

```text
undefined node label 'xxx'
```

significa:

> El overlay está intentando referenciar una etiqueta que no existe en el Devicetree cargado.

No significa necesariamente:

- conflicto eléctrico;
- PWM incorrecto;
- GPIO ocupado.

Primero comprobamos el nombre real del nodo.

---

# 28. Comprobaciones de un proyecto nuevo

Antes de programar revisamos estos puntos:

- [ ] Proyecto creado dentro de `C:\ZephyrUPM\workspace`
- [ ] VS Code abrió la raíz correcta del proyecto
- [ ] `${workspaceFolder}` corresponde a esa carpeta
- [ ] `CMakeLists.txt` correcto
- [ ] `src/main.c` existe
- [ ] `boards/nucleo_wl55jc.overlay` existe
- [ ] `confs/prj_nucleo_wl55jc.conf` existe
- [ ] `.vscode/settings.json` apunta a Zephyr 4.4.2
- [ ] `.vscode/tasks.json` apunta al venv 4.4
- [ ] SDK apunta a 1.0.1
- [ ] Primer build limpio
- [ ] El log dice `Zephyr version: 4.4.2`
- [ ] El log dice `Found toolchain: zephyr 1.0.1`
- [ ] El overlay fue encontrado
- [ ] El `.conf` fue merged
- [ ] Se generó `zephyr.elf`
- [ ] Se generó `zephyr.hex`
- [ ] Flash funciona
- [ ] Puerto COM identificado
- [ ] Monitor serie funciona

---

# 29. Checklist cuando algo falla

## Error de Devicetree

Buscamos:

```text
devicetree error:
```

Comprobamos:

```text
board DTS
overlay
node labels
aliases
status
```

## Error de Kconfig

Buscamos:

```text
warning:
error:
Merged configuration:
```

Inspeccionamos:

```text
build/zephyr/.config
```

## Error de compiler/linker

Localizamos primero:

```text
FAILED:
```

y la primera línea de error real antes del traceback.

Distinguimos la causa inicial de este resumen final:

```text
FATAL ERROR
```

porque normalmente es el resumen final.

## Error de rutas

En Windows revisamos especialmente:

```text
\
/
espacios
comillas
variables no expandidas
```

Comprobamos que CMake no reciba literalmente argumentos como:

```text
$env:PROJECT_DIR
```

## Error después de cambiar versiones

Primera acción:

```text
Clean Build
```

Después verificamos:

```text
Zephyr version
SDK version
ZEPHYR_BASE
```

---

# 30. Nuestro flujo de trabajo

## Al comenzar

1. Abrimos la carpeta de trabajo:

```powershell
code C:\ZephyrUPM\workspace\assesment_ldr_final\Embedded_Assessments\Proyecto_Final
```

2. Comprobamos que la raíz del Explorer de VS Code es el proyecto.

3. Compilamos:

```text
Ctrl + Shift + B
```

## Durante desarrollo

Trabajamos en:

```text
src/
boards/
confs/
```

Después:

```text
Ctrl + Shift + B
```

Con `-p auto` el build incremental será rápido.

## Para probar en hardware

```text
Tasks: Run Task
→ Flash
```

Luego:

```text
Serial Monitor
```

---

# 31. Flujo conceptual completo

```text
main.c
   │
   │ lógica de aplicación
   ↓

Devicetree / overlay
   │
   │ hardware
   ↓

Kconfig / .conf
   │
   │ funcionalidades de Zephyr
   ↓

CMakeLists.txt
   │
   │ fuentes de la aplicación
   ↓

west
   ↓
CMake
   ↓
Ninja
   ↓
ARM GCC
   ↓
Linker
   ↓

zephyr.elf
zephyr.hex
zephyr.bin

   ↓

STM32CubeProgrammer

   ↓

ST-LINK / SWD

   ↓

STM32WL55JC
```

---

# 32. Diagnóstico por capas

Cuando encontramos un fallo, lo localizamos por capas:

```text
1. VS Code / task
2. variables de entorno
3. west
4. CMake
5. Devicetree
6. Kconfig
7. compilador
8. linker
9. flashing
10. hardware
```

Usamos el primer error para identificar la capa afectada.

La mayoría de los errores quedan confinados a una sola capa.

---

# 33. Comandos habituales

Para una nueva práctica:

```text
1. Copiar template
2. Renombrar proyecto
3. Abrir carpeta raíz en VS Code
4. Confirmar settings/tasks
5. Primer Clean Build
6. Ctrl+Shift+B
7. Confirmar Zephyr 4.4.2 + SDK 1.0.1
8. Flash
9. Serial Monitor
10. Programar
```

Si eso funciona:

Con el entorno verificado continuamos con el código y las pruebas de la práctica.
