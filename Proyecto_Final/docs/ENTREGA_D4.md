# Entrega de adquisicion y estadisticas D4

Estado al 03/11/2026: implementacion local construida y ensayada; defecto de
retoma del Si7021 tras SDA retenida 60 ms corregido y repetido en placa.
La entrega queda pendiente de revision/aceptacion conjunta por Pablo.
El usuario autoriza el 03/11/2026 el commit y push de este incremento.
Base 3f9822a, rama feat/d3-estimacion; el commit de entrega se identifica
en el historial de la rama.
No se marca D4-27 como cumplido. El LDR se mantiene por decision del usuario
en su montaje actual, con las limitaciones indicadas abajo.

## Contenido y responsabilidades

| Archivo o grupo | Entrega |
| --- | --- |
| src/acquisition/acquisition.c | Hilo estatico 2048 B/prioridad 3, timer 20 ms, while (true), mutex, publicacion, progreso y sesiones estadisticas. |
| acquisition_imu.c/.h | Contexto privado, calibracion inicial, movimiento, estimador existente y recuperacion IMU. |
| acquisition_light.c/.h | Contexto ADC, cadencia 2 s, referencias medidas y porcentaje relativo. |
| acquisition_environment.c/.h | Contexto ambiental, F5 y recogida posterior, cadencia 2 s, ultimo par y calidad. |
| src/algorithms/statistics.c/.h | Ventanas de 30 s, suma/min/max/count y ultimo resultado de cinco metricas. |
| include/acquisition_api.h y model.h | Contrato de consumo compartido; las tres firmas D4 se conservan. |
| CMakeLists.txt | Fuentes reales de D4, drivers y algoritmos enlazadas con produccion. |
| tests/host | Seis suites de regresion con reloj/drivers simulados y modulos reales. |
| tests/board/acquisition | Consumidor API, perfil temporal sin consola y fixture I2C separado. |

Las funciones de adquisicion documentan proposito, entradas, retorno y cambios
de estado. Las cabeceras de contexto son privadas. Solo el hilo D4 usa los
drivers; hardware/calculos quedan fuera del mutex y las copias dentro.
No se introducen filtros, ZUPT ni otras correcciones de deriva.

## Uso desde aplicacion

El hilo estatico nace al arrancar; no hay una funcion publica de init ni un
protocolo de pausa. Adquiere en todos los modos. El consumidor incluye
acquisition_api.h y usa:

```c
AcquisitionSnapshot snapshot;
acquisition_snapshot_get(&snapshot);
```

snapshot es una copia coherente de medidas, calidad/fechas, movimiento,
navegacion, estadisticas y progreso. progress prueba avance de la vuelta;
last_good_ms fecha un dato utilizable. Hay que considerar ambos por separado.

La aplicacion llama acquisition_stats_begin al entrar en NORMAL y
acquisition_stats_end al salir. begin tambien reinicia una sesion activa;
end descarta parcial y oculta el resultado. El getter no acumula muestras.
Se cuenta cada lectura nueva utilizable una vez, aunque repita su valor.
count=0 se representa N/D. Los limites de ventana son [inicio, fin).

nav.q transforma sensor a marco local; nav.relative_q expresa orientacion
respecto a los ejes iniciales y es el campo que P2 debe usar para Euler.
nav.valid informa de aceptacion numerica, no de exactitud de trayectoria.
moving no congela velocidad/posicion. La retoma usa dt=0 sin integrar el
intervalo desconocido y conserva el sesgo y q0 ya preparados.

El main observador usa la API para medidas y sesiones; adicionalmente consulta
la pila del hilo desde el ensayo. El main de produccion sigue siendo el de
la base: esta entrega no implementa la aplicacion P3, GPS ni telemetria.

## Construccion y comprobaciones

Desde Proyecto_Final, con el entorno Zephyr activo:

```sh
west build -p auto -b nucleo_wl55jc . -d build -- -DCONF_FILE=confs/prj_nucleo_wl55jc.conf -DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay
west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition
west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition-timing -- -DD4_TIMING_ONLY=ON
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug --output-on-failure
```

Construccion comprobada con Zephyr 4.4.2, SDK 1.0.1/GCC 14.3.0; host con MSVC
Debug /W4 /WX. Pasan las seis suites. Produccion ocupa FLASH 36260/RAM 8832 B;
observador FLASH 46868/RAM 9856 B. Las restricciones build_checks.c se aplican
a produccion y perfiles sin consola. El fixture I2C no se enlaza en produccion
ni en el observador: comprobado tambien en sus simbolos finales.

## Evidencia y limites

| Bloque | Evidencia y resultado |
| --- | --- |
| Arranque, calibracion, movimiento, luz y ventanas | [Ensayo integrado](../tests/board/acquisition/results/Ensayo_D4_20261102.md), incluidas sesiones y ausencia/retoma individual de sensores. |
| Fronteras de tiempo/ventana, NO_NEW, saturacion y recuperacion | Pruebas host descritas en [tests/README.md](../tests/README.md). |
| Tiempo nominal | [Medicion sin consola](../tests/board/acquisition/results/Tiempos_Adquisicion_D4_20261102.md): 20790 vueltas nominales, maximo 3,217 ms, cero >=10 ms. |
| Timeout fisico y espaciado | [Repeticion tras corregir](../tests/board/acquisition/results/Si7021_Diagnostico_20261103.md): SCL retenida, vuelta maxima 5,462 ms, cero omisiones y retoma de ambos sensores. |
| SLEEP/reset silencioso MPU | Mismo registro I2C: deteccion por revision lenta y retoma conservando sesgo/q0. Init completo excede 20 ms por sus sleeps; cada caso registra seis omisiones. |
| Retoma ambiental tras SDA breve | [Correccion del 03/11](../tests/board/acquisition/results/Si7021_Diagnostico_20261103.md): revalidar configuracion antes de F5; repetido con SDA de 60 ms y 5 s, sin nuevos resets ni omisiones. |
| LDR | [Referencias medidas](../tests/board/light/results/Calibracion_LDR_20261102.md): oscuro 4062 y linterna del movil al maximo a 5 cm 128; alimentacion 3,3 V, sin 47 kohm adicional. |

El LDR mide luz relativa a esas condiciones. El usuario acepta ese montaje;
la deteccion electrica de desconexion queda limitada. Repetir referencias si
cambia el montaje. No se atribuyen valores de lux ni calibracion universal.

Los tiempos son maximos observados, no WCET demostrado. Falta contrastarlos
con la carga completa de P3. La comprobacion final tras retirar el fixture
observa 1560 B de pila libre en adquisicion y 2088 B en main del observador.
Se publica otra ventana completa de cinco metricas, con todos los sensores
validos y cero omisiones; capturas tras la correccion en el apartado final del
[diagnostico Si7021](../tests/board/acquisition/results/Si7021_Diagnostico_20261103.md).
El margen anterior de 1584 B pertenece a una imagen previa. Ninguno de estos
margenes se extrapola al fixture I2C ni a P3.
La deriva ya documentada del estimador se conserva; las pruebas no acreditan
precision espacial ni cierran la demostracion integrada de D3/P2.

## Trabajo concreto para cerrar

El caso Si7021 se resolvio con configured=false al interrumpir el bus:
environment_init vuelve a validar antes de F5. Pasa regresion host que falla
con el codigo anterior y pruebas de retoma del modulo real. No se atribuye
un mecanismo electrico interno del chip a partir de la traza.

1. Revision de contratos y reproduccion del build en el segundo equipo;
   aceptacion de entrega por Pablo, sin atribuirle una revision aun no hecha.

La integracion P3/P4 debe comprobar carga concurrente, pila, supervision,
consumo de snapshots y formatos. Son validaciones posteriores del sistema,
distintas del defecto concreto de recuperacion ya corregido en D4.
