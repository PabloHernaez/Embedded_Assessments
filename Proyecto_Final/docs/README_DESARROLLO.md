# Desarrollo del firmware

## Estado del proyecto

D1 esta integrado en `master`. Tenemos los drivers de IMU, luz y ambiente,
la calibracion inicial del gyro y algoritmos de movimiento y navegacion.
El ensayo independiente usa MPU6050 y Si7021, integra q/v/p y contrasta la
orientacion relativa inicial. Ademas, tests/board/acquisition enlaza el servicio
real D4 y ensaya IMU, luz, ambiente, sesiones y estadisticas de 30 s juntos.

Produccion conserva su main minimo y ya enlaza el servicio D4. GPS,
formatos y supervision siguen pendientes de integrar. Se conservan las firmas de las
seis operaciones publicas de aplicacion. `NavigationView` anade `relative_q`
para entregar a P2 la orientacion que comienza en identidad, separada de la
orientacion usada para compensar gravedad.

El [registro de D3](D3_ESTIMACION.md) conserva calibracion, capturas,
deriva medida y limites. El ensayo de 600 s no acredita trayectoria precisa.
D2 y D3 no se declaran cerrados por este incremento.

## Entorno de trabajo

| Componente | Versión / revisión |
| --- | --- |
| Placa / target | NUCLEO-WL55JC / `nucleo_wl55jc/stm32wl55xx` |
| Zephyr | 4.4.2, commit `dccb09599635bdff17633fa7e9dab014b91dce90` |
| SDK / ARM GNU | 1.0.1 / 14.3.0 |
| Python / west | 3.12.10 / 1.5.0 |
| CMake / Ninja / DTC | 4.4.3 / 1.13.2 / 1.6.1 |

Cada uno configura Zephyr, SDK, Python y west en su equipo, conservando las
versiones registradas. Mantenemos las rutas y preferencias personales fuera
de Git: `.vscode/`, `scripts/` y las notas locales de Windows.

Compartimos el firmware, CMake, los contratos y la configuración de placa.
El workspace de VS Code es opcional; también trabajamos por terminal.
Nos queda aclarar la referencia docente a 4.4.99 frente a esta base 4.4.2.

## Compilación

Con el entorno de Zephyr ya activo, desde `Proyecto_Final` usamos los mismos
comandos en Windows y Ubuntu:

```sh
west build -p auto -b nucleo_wl55jc . -d build -- -DCONF_FILE=confs/prj_nucleo_wl55jc.conf -DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay
west build -p auto -b nucleo_wl55jc tests/board/sensors -d build-sensors
west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition
```

Usamos `-p always` para regenerar el build desde cero. Cada equipo conserva
sus propios directorios `build` y `build-sensors`. La programación, el puerto
serie y la depuración se configuran localmente con las herramientas de cada uno.

Antes de actualizar un clon con preferencias propias, guardamos una copia
de `.vscode/` y de los scripts personales. Tras el cambio dejan de estar
versionados; conservamos nuestras copias locales y no resolvemos posibles
conflictos del pull descartando esos ajustes.

## Configuración y contratos comunes

Mantenemos los `BUILD_ASSERT` de `src/build_checks.c` en producción y en los
perfiles D4 sin consola, de forma incondicional. Rechazan consola,
UART/stdout console, printk, banner, shell, logs y los chosen de consola de
Devicetree. El observador con consola no compila ese archivo y habilita su
salida en su propia configuración.

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

El ensayo de sensores y el servicio D4 programan la recogida desde el final
de F5 con `ceil(40 ms) + 1 tick`. Atienden el resultado en una vuelta posterior,
sin mantener el hilo esperando los 40 ms de conversion.

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

## Navegacion, referencia SR4 y capturas

`nav.q` transforma del sensor al marco local con Z arriba, usado para
compensar gravedad e integrar velocidad/posicion. `nav.relative_q` transforma
del sensor actual a sus ejes iniciales y debe alimentar ROLL/PITCH/YAW.
Ambos son cuaterniones Hamilton [w,x,y,z]. La referencia inicial permanece
privada en adquisicion; un cambio de modo o fallo rutinario no la recaptura.
La seleccion del campo queda documentada en simu_frame.h y human_report.h;
el snapshot D4 ya publica ambos campos; la conversion y emision real por P2
siguen pendientes.

La calibracion usa 100 muestras consecutivas aceptadas. El limite inicial de
giro sin corregir es 5 grados/s; el reposo posterior mantiene 3 grados/s.
El ensayo aplica una correccion preliminar de acelerometro medida en seis
posturas y prepara la inclinacion con el promedio de las mismas muestras.
Sus constantes y limitaciones estan en el registro D3.

Las capturas de 2026-11-02 estan en `tests/board/sensors/results/`. Hay un
ensayo de deriva de 600 s, uno dinamico de 30 s y dos comprobaciones de 10 s
para la referencia SR4. La deriva final de 600 s es |v|=284,24 m/s y
|p|=55 567,65 m, sin errores de lectura o numericos durante navegacion.

Desde Proyecto_Final, con pyserial disponible y el monitor habitual cerrado:

```sh
python tests/board/sensors/capturar_deriva.py --port COM5 --seconds 600 --output-dir tests/board/sensors/results
```

Indicar el puerto del equipo; en Linux puede ser /dev/ttyACM0. Pulsar RESET
cuando lo pida el capturador y mantener la postura requerida por el ensayo.
El script guarda el arranque, cuenta desde el primer reporte posterior a la
calibracion y conserva el ultimo reporte completo. Sus avisos de progreso
pertenecen al PC; el archivo conserva la salida recibida del firmware.
Una captura interrumpida o con otro arranque se conserva como parcial.

La evidencia nominal no sustituye la inyeccion de fallos ni una traza I2C
para medir transferencias. Los ELF exactos de cada captura no se archivaron.

## Ensayo integrado D4: 02/11/2026

El [registro de D4](../tests/board/acquisition/results/Ensayo_D4_20261102.md)
conserva el ensayo del servicio real con los tres sensores. Incluye arranque,
movimiento, extremos de luz, ventanas de 30 s, begin/end y ausencia/retoma
individual de IMU y Si7021. La prueba detecto que revisiones IMU fallidas
aplazaban indefinidamente ENV; se corrigio sin estados nuevos y se repitio
en host y placa. Pasaron seis suites host y ambos builds ARM.

En la imagen corregida, una ventana sin IMU contiene luz/T/HR n=15 e IMU n=0;
sin ambiente, T/HR n=0 y aceleracion/giro n=1496. El margen observado de pila
de adquisicion es 1584 B y se registran 0 periodos omitidos en 19927 vencimientos.
No son mediciones del coste maximo de las operaciones. La deriva de navegacion
se conserva y no se acredita trayectoria precisa. Los cambios se recogen en la entrega D4.

La [medicion temporal posterior](../tests/board/acquisition/results/Tiempos_Adquisicion_D4_20261102.md)
desactiva consola y barrido de pilas, mantiene estadisticas activas y lee
diagnosticos privados por SWD. Maximo nominal observado 3,217 ms en 20790
vueltas, sin alcanzar 10 ms; cumple el objetivo nominal de la guia en los
casos ensayados. Una reinicializacion completa provocada tarda 147,416 ms,
con los sleeps existentes y 6 periodos omitidos registrados. Esos resultados
no son un WCET formal ni cubren la futura carga completa de P3.

El [ensayo I2C controlado](../tests/board/acquisition/results/I2C_D4_20261102.md)
verifica SLEEP/reset reales de MPU, tres errores de backend con bus libre y
retenciones fisicas de SDA/SCL. Con SCL retenida se mide el timeout real:
vuelta completa maxima 5,445 ms, sin omisiones y con retoma de ambos sensores.
El pulso corto de SDA reproduce un defecto: IMU retoma pero Si7021 queda
sin responder. Reiniciar MCU y ejecutar bus clear no lo resolvian; la orden
de reset Si7021 tampoco recibia respuesta. El
[ajuste del 03/11](../tests/board/acquisition/results/Si7021_Diagnostico_20261103.md)
deja configured=false al interrumpir el bus, para revalidar antes del siguiente
F5 sin aplazar la cadencia. Pasa regresion host y dos pulsos SDA de 60 ms mas
uno de 5 s con el modulo real; conserva sesgo/q0 y no omite periodos.

## Pendientes

El paquete y los criterios de cierre estan en [ENTREGA_D4.md](ENTREGA_D4.md).

- Revisar y aceptar la entrega D4 con la correccion de retoma Si7021 ya ensayada.
  Reset silencioso, retenciones de bus y ruta rapida de tres errores tienen evidencia; esta
  ultima usa backend simulado, no un glitch electrico. El antiguo retorno -15
  sigue sin causa identificada y no se equipara al -EIO de esta prueba.
- LDR: el usuario acepta cerrar con el montaje actual a 3,3 V sin 47 kohm y
  referencias medidas 4062/128. Se conserva la limitacion del diagnostico
  de desconexion; repetir referencias si cambia el montaje. No bloquea esta entrega.
- Contrastar tiempos bajo carga P3; el presupuesto del servicio actual y el
  camino de timeout ya se midieron en placa. El ensayo I2C añade duraciones
  por llamada, sin constituir un analisis WCET de todas las transferencias.
- Integrar GPS, formatos, supervision y consumo de los snapshots D4 desde la aplicacion.
- Integracion de P2 usando relative_q; validacion de Euler/trama SIMU y demo integrada.
- Revisión conjunta de contratos y reproducción del build en el segundo equipo.
- Sesión completa de Cortex-Debug con placa y aclaración de la versión docente.
- Simplificaciones diferidas de la auditoría: init del Si7021, configuración,
  comentarios/constantes e indentación del ensayo.

D2 y D3 siguen abiertos. Conservamos los detalles de las comprobaciones en los
commits y PR; esta guía recoge cómo trabajamos y el estado vigente.
