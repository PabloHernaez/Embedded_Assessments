# Desarrollo del firmware

## Estado del proyecto

D1 esta integrado en `master`. Tenemos los drivers de IMU, luz y ambiente,
la calibracion inicial del gyro y algoritmos de movimiento y navegacion.
El ensayo independiente usa MPU6050 y Si7021, integra q/v/p y contrasta la
orientacion relativa inicial; todavia no incluye el LDR.

Produccion conserva su main minimo. La adquisicion integrada de D4, GPS,
formatos y supervision siguen pendientes. Se conservan las firmas de las
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
```

Usamos `-p always` para regenerar el build desde cero. Cada equipo conserva
sus propios directorios `build` y `build-sensors`. La programación, el puerto
serie y la depuración se configuran localmente con las herramientas de cada uno.

Antes de actualizar un clon con preferencias propias, guardamos una copia
de `.vscode/` y de los scripts personales. Tras el cambio dejan de estar
versionados; conservamos nuestras copias locales y no resolvemos posibles
conflictos del pull descartando esos ajustes.

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

## Navegacion, referencia SR4 y capturas

`nav.q` transforma del sensor al marco local con Z arriba, usado para
compensar gravedad e integrar velocidad/posicion. `nav.relative_q` transforma
del sensor actual a sus ejes iniciales y debe alimentar ROLL/PITCH/YAW.
Ambos son cuaterniones Hamilton [w,x,y,z]. La referencia inicial permanece
privada en adquisicion; un cambio de modo o fallo rutinario no la recaptura.
La seleccion del campo queda documentada en simu_frame.h y human_report.h;
la conversion y emision real por P2 y el snapshot de D4 siguen pendientes.

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

## Pendientes

- Completar fallos/recuperacion I2C; el retorno negativo -15 informado sigue sin causa identificada.
- Calibración final del LDR y diagnóstico eléctrico; el ensayo actual no lee luz.
- Adquisición, GPS, formatos, snapshots y supervisión integrados.
- Integracion de D4 y P2 usando relative_q; validacion de Euler/trama SIMU y demo integrada.
- Revisión conjunta de contratos y reproducción del build en el segundo equipo.
- Sesión completa de Cortex-Debug con placa y aclaración de la versión docente.
- Simplificaciones diferidas de la auditoría: init del Si7021, configuración,
  comentarios/constantes e indentación del ensayo.

D2 y D3 siguen abiertos. Conservamos los detalles de las comprobaciones en los
commits y PR; esta guía recoge cómo trabajamos y el estado vigente.
