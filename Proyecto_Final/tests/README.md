# Ensayos

Las pruebas de placa se ejecutan en una aplicacion separada del main de
produccion. Los modulos puros tambien disponen de pruebas de host con CMake
y CTest, sin hardware ni dependencias de Zephyr.

## Placa: MPU6050, calibracion, movimiento y Si7021

La aplicacion de board/sensors comparte los drivers y algoritmos reales.
Aplica primero la configuracion comun y despues su perfil de consola
LPUART1 a 115200 8N1. Produccion conserva las restricciones de build_checks.c.

El main coordina funciones privadas para inicializar, leer, calibrar,
procesar movimiento, atender ambiente y mostrar el resumen. Un temporizador
periodico da una oportunidad de lectura de IMU cada 20 ms. NO_NEW no repite
una medida. Tras calibrar se resta el sesgo antes de calcular el giro usado
por movimiento. Errores, saturacion e intervalos mayores de 50 ms cancelan
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

estimator_init prepara NavigationView con cuaternion identidad, velocidad
y posicion cero y valid=false. estimator_orientation_update actualiza solo
el cuaternion sensor-local Hamilton, a partir del giro corregido y dt real.
Convierte grados/s a radianes/s, avanza por Euler y normaliza un candidato.
Solo confirma el resultado si es numericamente valido. Rechaza intervalos
negativos, no finitos o mayores de 50 ms y conserva el cuaternion anterior.

El modulo es C puro. El llamador sigue siendo responsable de NO_NEW y de
romper la referencia temporal ante error, saturacion o hueco excesivo.
La integracion de velocidad/posicion y la publicacion de navegacion valida
siguen pendientes. El ensayo de placa actual todavia prueba movimiento y
ambiente; no consume esta funcion de orientacion.

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
## Pendientes

Quedan las pruebas de error/recuperacion en placa, la validacion y calibracion
final del LDR y la integracion del estimador y los servicios definitivos.
La aceptacion completa de D2 y D3 sigue abierta.
