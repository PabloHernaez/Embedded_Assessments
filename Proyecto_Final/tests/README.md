# Ensayos

Las pruebas de placa se ejecutan en una aplicacion separada del main de
produccion. Los modulos puros tambien disponen de pruebas de host con CMake
y CTest, sin hardware ni dependencias de Zephyr.

## Placa: MPU6050, calibracion, movimiento y Si7021

La aplicacion de board/sensors comparte los drivers y algoritmos reales.
Aplica primero la configuracion comun y despues su perfil de consola
LPUART1 a 115200 8N1. Produccion conserva las restricciones de build_checks.c.

El main coordina funciones privadas para inicializar, leer, calibrar,
procesar movimiento y navegacion, atender ambiente y mostrar el resumen. Un temporizador
periodico da una oportunidad de lectura de IMU cada 20 ms. NO_NEW no repite
una medida. Tras calibrar se resta el sesgo antes de calcular el giro usado
por movimiento y navegacion. Errores, saturacion e intervalos mayores de 50 ms cancelan
la confirmacion pendiente conservando el estado de movimiento confirmado.

Movimiento se confirma tras 300 ms con desviacion de aceleracion >0.8 m/s2
o giro >8 grados/s. Reposo exige 2 s con desviacion <0.3 m/s2 y giro
<3 grados/s. Los umbrales quedan en algorithm_config.h.

El Si7021 inicia una conversion aproximadamente cada 2 s y recoge su
resultado en una vuelta posterior. La fecha de recogida se toma despues
de F5 y conserva ceil(40 ms) + 1 tick. No bloquea el hilo durante conversion.
Los mensajes de estado se limitan a uno por segundo, mas cambios de estado
y resultados ambientales.

Usamos las tareas locales West Build (sensores), Flash (Sensores) y Serial
Monitor. Tambien puede construirse por terminal desde el entorno Zephyr:

    west build -b nucleo_wl55jc Proyecto_Final/tests/board/sensors -d Proyecto_Final/build-sensors

Las rutas del ejemplo son relativas a la raiz del repositorio; las rutas
personales y las tareas locales no se incorporan a Git.

## Capturas de movimiento: 26/10/2026

Los archivos originales se conservan en board/sensors/results/.

| Captura | Tramo posterior a calibracion | NEW | NO_NEW | ERR | SAT | GAP |
| --- | --- | --- | --- | --- | --- | --- |
| Ensayo_motion_1.txt | 42 s | 2074 | 5 | 0 | 22 | 0 |
| Ensayo_motion_2.txt | 68 s | 3394 | 7 | 0 | 0 | 0 |

La primera captura registra cambios a movimiento en 19.294 s y a reposo en
29.674 s desde arranque; hubo 22 saturaciones. La segunda registra movimiento
en 13.134 s y reposo en 67.594 s, sin saturacion. En esta segunda prueba
mantuvimos el sensor en movimiento hasta el final y despues lo dejamos quieto.

En ambas capturas los contadores son coherentes con 50 consultas/s.
Se obtuvieron 22 y 35 pares ambientales respectivamente, sin errores
ambientales registrados. GAP cuenta discontinuidades con referencia previa
activa; saturacion y error rompen esa referencia y se cuentan por separado.

Estas capturas respaldan el ciclo nominal reposo-movimiento-reposo. No
registran el comienzo de cada condicion candidata, por lo que no miden
directamente los 300 ms y 2 s de confirmacion.

## Host: movimiento y orientacion

Desde Proyecto_Final:

    cmake -S tests/host -B build-host
    cmake --build build-host --config Debug
    ctest --test-dir build-host -C Debug --output-on-failure

Se necesita un compilador C de host. En el entorno Windows utilizado ya
estan instalados MSVC Build Tools y CMake/CTest; no se anade un framework.

test_motion.c comprueba tiempos de confirmacion, cancelacion, histeresis,
condiciones OR/AND, limites estrictos de giro, NO_NEW omitido por el
llamador e interrupcion explicita de continuidad. Las comprobaciones
siguen activas con NDEBUG.

## Orientacion incremental

estimator_init prepara NavigationView con q y relative_q identidad, v/p
cero y valid=false. estimator_set_initial_tilt prepara q usando la media
de aceleracion corregida de las 100 muestras aceptadas para calibrar gyro.
La referencia q0 se fija entonces una vez; relative_q empieza en identidad.
El limite inicial gyro es 5 grados/s sin corregir, separado del de reposo
posterior de 3 grados/s. Los umbrales no certifican inmovilidad fisica.

estimator_orientation_update integra el giro corregido en grados/s mediante
Euler y normaliza el candidato Hamilton sensor -> local. estimator_update
calcula q, relative_q=q0^-1*q, rota aceleracion, resta 9.80665 m/s2 en Z y
avanza v/p con Euler explicito: la posicion usa la velocidad anterior.
El candidato se confirma completo; ante rechazo se conservan esos valores
y valid pasa a false. Se admiten dt entre 0 y 50 ms inclusive.

Los algoritmos son C puro. El llamador omite NO_NEW, rompe continuidad ante
error/saturacion/hueco y entrega dt=0 en la primera utilizable posterior.
El ensayo consume ahora el estimador, pero el getter y servicio de D4 y los
formateadores de P2 siguen pendientes.

test_estimator.c cubre reposo, giros positivos/negativos en los tres ejes,
composicion de giros en ejes del sensor, norma unitaria, intervalos de 0,
20, 40 y 50 ms, intervalos rechazados y conservacion del estado ante
NaN, infinito, desbordamiento o cuaternion degenerado.
## Verificacion de los incrementos

CTest de movimiento paso en Debug. Tambien se compilaron 12 objetos ARM de
aplicacion con las configuraciones vigentes de produccion y ensayo, usando
-Wextra -Werror. Las cabeceras generadas proceden de los builds configurados
del proyecto original; esta comprobacion no es un enlace ELF completo.

El incremento de orientacion paso ambos ejecutables de CTest en Debug y
Release, y compilo 14 objetos ARM de aplicacion con los mismos perfiles.
Las pruebas se ejecutaron sobre las fuentes que forman estos commits.
Los intentos de build completo de ambos perfiles en la copia aislada se
detuvieron al arrancar el DTC de Windows con codigo 0xc0000142, antes de
compilar las fuentes. Al importar los commits en el repositorio original
el 27/10/2026, los builds completos de produccion y sensores terminaron
con codigo 0 y generaron sus ELF/HEX. CTest tambien paso en Debug y Release
desde esa ubicacion. No hicimos otro ensayo de placa durante la importacion.
## Verificacion del incremento de navegacion y SR4: 02/11/2026

En la copia preparada para este commit pasaron los dos ejecutables de CTest
(movimiento y orientacion) en Debug y Release. Una comprobacion nativa
temporal verifico identidad inicial, inclinacion, giro relativo de 90 grados,
integracion v/p, referencia inmutable y rechazo atomico ante entradas no
finitas, referencia degenerada e intervalo superior a 50 ms.

Se comprobaron tambien 14 fuentes de aplicacion con GCC ARM, -Wall -Wextra
-Werror y -fsyntax-only, usando las configuraciones generadas existentes de
produccion y sensores. Esta comprobacion no es un nuevo enlace ELF/HEX.
Las capturas de placa fueron realizadas por el usuario con sus builds; no
se archivaron los ELF exactos para reconstruir cada imagen historica.
La suite versionada sigue cubriendo movimiento y orientacion; la comprobacion
adicional de navegacion fue temporal, junto con la evidencia real de placa.

## Capturas D3 del 02/11/2026

El [registro D3](../docs/D3_ESTIMACION.md) detalla procedimiento y limites.
Los originales estan en board/sensors/results/ y el capturador de PC en
board/sensors/capturar_deriva.py (requiere pyserial).

| Captura | Duracion | Evidencia |
| --- | --- | --- |
| Ensayo_deriva_600s_20261102_165247_571741.txt | 600 s | 601 reportes, norma(v)=284,24 m/s y norma(p)=55 567,65 m al final. |
| Ensayo_deriva_30s_20261102_172320_736933.txt | 30 s | Giro Z, pausa a yaw estimado 90,79 grados, regreso a -3,25 grados; sin patron angular independiente. |
| Ensayo_deriva_10s_20261102_174455_612323.txt | 10 s | Orientacion relativa inicial identidad calculada para diagnostico. |
| Ensayo_deriva_10s_20261102_181219_304185.txt | 10 s | relative_q calculado por el estimador y leido desde NavigationView. |

Las cuatro capturas tienen ERR/SAT/GAP/NAV_ERR a cero durante navegacion.
No prueban errores fisicos exactos, tiempos de confirmacion sin registrar
condiciones candidatas ni rutas de fallo no ejecutadas. NAV=1 indica
aceptacion numerica y no precision espacial. La publicacion de Euler
relativo y la trama real se comprobaran con P2 y D4.

## Pendientes

Quedan las pruebas de error/recuperacion en placa, la validacion y calibracion
final del LDR y la integracion de adquisicion, formateadores y servicios definitivos.
La aceptacion completa de D2 y D3 sigue abierta.
