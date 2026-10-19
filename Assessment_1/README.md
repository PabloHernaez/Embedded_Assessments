# Brightness monitoring con Zephyr

Alumni: Pablo Hernaez, David Matamoros Ghisays

Aplicación para la NUCLEO-WL55JC1 que mide la iluminación con un KY-018 y
muestra el nivel mediante un RGB externo.

## Comportamiento

El sistema arranca en NORMAL. El botón B2 controla los modos:

| Modo | Funcionamiento | Pulsación corta |
|---|---|---|
| NORMAL | Mide la luz y muestra su nivel en el RGB | Pasa a BLUE |
| BLUE | RGB azul y adquisición ADC pausada | Vuelve a NORMAL |
| OFF | RGB apagado y adquisición ADC pausada | Vuelve a NORMAL |

Mantener B2 pulsado durante al menos 1 segundo activa OFF desde NORMAL o
BLUE, sin esperar a soltarlo. Soltar después no provoca una pulsación corta.

En NORMAL se realiza una primera lectura inmediata y después una cada
2 segundos. Cada muestra es el promedio de ocho conversiones ADC. Como el
KY-018 entrega menos tensión con más luz, se calcula el brillo con
`4095 - adc_average`, respecto a la escala completa de 12 bits (0 a 4095).

- Menos del 33 %: rojo.
- Del 33 % al 66 %, incluidos: amarillo.
- Más del 66 %: verde.

El terminal serie (115200 8N1) muestra los cambios de modo y, en NORMAL, el
ADC y el porcentaje de brillo. Al entrar en OFF muestra `System OFF`.

## Cableado

| Función | Conexión |
|---|---|
| KY-018 `S` | A0 / PB1 / ADC1_IN5 |
| KY-018 `+` | 3V3 |
| KY-018 `-` | GND |
| Común del RGB | 3V3, ánodo común |
| Rojo | D4 / PB5 mediante 330 ohm |
| Verde | D5 / PB8 mediante 330 ohm |
| Azul | D6 / PB10 mediante 330 ohm |

Cada canal del RGB necesita su propia resistencia. No conectar un canal
directamente al GPIO.

## Hilos y comunicación

Se usan dos hilos de aplicación:

- `main.c`: recibe los eventos de B2 y las muestras; `modes.c` decide el modo
  y el color del RGB.
- `sensor_thread.c`: realiza las lecturas cada 2 segundos. Se mantiene
  separado para que la adquisición no bloquee la atención al botón.

El botón usa interrupciones para enviar eventos a una cola. Un temporizador
genera el evento de pulsación larga al cumplirse 1 segundo.
El circuito antirrebote de B2 se describe en [Hardware](docs/HARDWARE_AND_BUILD.md).

El hilo del sensor recibe órdenes START/PAUSE por una cola y entrega las
muestras por otra. La cola de muestras conserva solo el resultado más reciente
para evitar datos atrasados. Un semáforo confirma que el sensor se ha pausado
antes de fijar azul o apagado. Los indicadores atómicos permiten compartir
el estado del servicio entre los dos hilos sin accesos simultáneos inseguros.

El sensor tiene un único bucle y un estado local activo/pausado. Pausado espera
una orden indefinidamente; activo espera una orden hasta la próxima muestra.
START permite una lectura inmediata y PAUSE se confirma en un único punto.

Estas herramientas permiten esperar a eventos sin hacer espera activa.
Cuando está pausado, el hilo del sensor espera una orden y no inicia lecturas.
La gestión de energía usa el modo system-managed de Zephyr.

## Archivos principales

- `brightness.c`: ADC, promedio, inversión, porcentaje y clasificación.
- `rgb_led.c`: control lógico de los cinco colores.
- `sensor_thread.c`: periodo, pausa y entrega de resultados.
- `button.c`: interrupciones de B2 y temporizador de pulsación larga.
- `modes.c`: transiciones entre NORMAL, BLUE y OFF.
- `main.c`: inicialización y recepción de eventos y muestras.

La polaridad del RGB está declarada como `GPIO_ACTIVE_LOW` en Devicetree.

## Recorrido del firmware en VS Code

Abrir [firmware-study.code-workspace](firmware-study.code-workspace) con VS Code.
El espacio de trabajo recomienda la extensión **CodeTour** (`vsls-contrib.codetour`).
Después, `Ctrl+Shift+P` → **CodeTour: Start Tour** inicia las 28 paradas del
[recorrido del firmware original](.tours/firmware-original.tour).

Los [cuatro diagramas de secuencia](docs/FIRMWARE_SEQUENCE.md) explican arranque,
muestreo, botón y pausa. Sus fuentes [PlantUML](docs/diagrams/) generan las
vistas previas SVG del Markdown y una [galería HTML](docs/diagrams/index.html).
En las vistas HTML, los nombres de función sobre las flechas enlazan a su
implementación en GitHub. La tarea **Firmware: abrir diagramas** abre la galería
local en el navegador, con navegación entre las cuatro secuencias y zoom.
Los HTML también funcionan al abrirlos directamente, sin servidor ni extensiones.
El recorrido usa patrones del código, y el documento incluye un índice de
enlaces a las líneas actuales.

Al cambiar funciones, ejecutar la tarea **Firmware: actualizar referencias**;
para comprobar los enlaces, **Firmware: comprobar documentación**. También se
puede usar Python 3 directamente desde esta carpeta:

```powershell
python scripts/check-firmware-docs.py --refresh
python scripts/check-firmware-docs.py
python scripts/firmware_diagrams.py --open
```

La regeneración requiere Java en PATH y Python 3. La primera ejecución descarga
PlantUML 1.2026.9 de su distribución oficial a `.zephyr-cache/tools/` y verifica
su SHA256. Las siguientes ejecuciones usan esa copia local. La tarea
**Firmware: regenerar diagramas** actualiza únicamente las vistas y sus enlaces;
**Firmware: actualizar referencias** actualiza también el índice de CodeTour.
El verificador no necesita Java ni acceso a Internet: comprueba las referencias,
los enlaces de los SVG y la correspondencia de fuentes y vistas HTML.

El comportamiento de las secuencias se mantiene en los `.puml`; no se deduce
automáticamente del C. Las referencias a funciones se localizan por su definición
y enlazan a la rama `master`; los SVG y HTML se incluyen en el repositorio.
El tema visual común está definido en `docs/diagrams/theme.puml`.

## Compilar y cargar

Se necesita Zephyr 4.4.2, su entorno Python con west, Zephyr SDK 1.0.1 y
STM32CubeProgrammer. Este último debe estar disponible en el PATH para cargar
el firmware por ST-LINK.

Desde `Assessment_1`, ejecutar en PowerShell. Ajustar las rutas si el entorno
está instalado en otra carpeta:

```powershell
$env:ZEPHYR_BASE = "C:\ZephyrUPM\zephyrproject-4.4\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\Users\davma\zephyr-sdk-1.0.1"
$zephyrPython = "C:\ZephyrUPM\zephyrproject-4.4\.venv\Scripts\python.exe"

& $zephyrPython -m west build -b nucleo_wl55jc -d build -- "-DCONF_FILE=confs/prj_nucleo_wl55jc.conf" "-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay"
```

Conectar la placa por USB al ST-LINK y cargar:

```powershell
& $zephyrPython -m west flash -d build
```

## Comprobación en placa

1. En NORMAL, variar la luz y comprobar ADC, porcentaje y colores cada 2 segundos.
2. Pulsar B2 brevemente: BLUE fijo y sin nuevas lecturas. Repetir para volver a NORMAL.
3. Mantener B2 pulsado desde NORMAL y desde BLUE: al segundo debe apagarse el
   RGB y aparecer `System OFF`, antes de soltar.
4. Soltar tras la pulsación larga: debe seguir en OFF. Una nueva pulsación
   corta debe restaurar NORMAL.
