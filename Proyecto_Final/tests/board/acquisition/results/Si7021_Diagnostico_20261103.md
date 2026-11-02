# Diagnostico Si7021: 03/11/2026

Continua el [defecto SDA breve](I2C_D4_20261102.md). El usuario solicita
resolverlo. Rama feat/d3-estimacion, base 3f9822a y D4 local. Sin commit/push.
Se prueban las hipotesis en una imagen separada; el cambio final del servicio
se incorpora solo despues de contrastar la revalidacion previa en placa.
Los drivers y protocolos publicos no cambian.

## Condiciones y observacion

Se carga y verifica el perfil I2C con observacion por mensaje STM32, sin
consola. El envoltorio retorna exactamente el resultado original y guarda
direccion, flags, longitud, primer byte utilizable y NACK/ARLO/ERR. No accede
al buffer de una lectura fallida. El uso de la cabecera STM32 privada queda
limitado al ensayo; no se modifica Zephyr.

Antes del montaje se verifica PC2 como salida open-drain y ODR=1. El usuario
apaga y conecta D8/PC2 a SDA/D14/PA11, manteniendo los tres sensores, y vuelve
a alimentar. [Referencia](Tiempos_D4_si7021_traza_baseline_20261103_000159_482828.json):
IMU/nav/luz/ambiente validos, calibracion terminada y cero omisiones.

El usuario confirma VCC del Si7021 a 3V3 y mide **3,293 V** entre VCC y GND
en los pines del modulo mientras ENV esta en fallo. Es una medida estatica;
no prueba ausencia de transitorios. No se atribuye el fallo a alimentacion
incorrecta ni se descarta toda causa electrica a partir de esa unica lectura.

## Reproduccion y causa del retorno

[SDA 60 ms con traza](Tiempos_D4_si7021_traza_fallo_sda_20261103_000226_467669.json):
retencion 60094 us, inicio 16104 y fin 16164 ms. IMU falla por ARLO (perdida
de arbitraje) durante la retencion; bus clear=-EBUSY mientras sigue baja.
Despues, F5 retorna 0 a 16428 ms, pero lectura RH retorna -EIO con NACK a
16488 ms. ENV_INIT falla tambien con NACK, aproximadamente cada 2,02 s.
No es un rechazo CRC ni un timeout. IMU/nav/luz continúan.

La [orden de reset 0xFE](Tiempos_D4_si7021_reset_nack_detalle_20261103_000443_751375.json)
tambien recibe NACK. El
[sondeo sin datos a 0x40](Tiempos_D4_si7021_sondeo_sin_datos_20261103_000623_319815.json)
recibe NACK: confirma que en ese estado se rechaza el propio direccionamiento,
sin depender del contenido E7/FE. Estos dos perfiles incluyen un reinicio MCU;
no son comparaciones de sesgo/q0 entre una misma sesion.

last_failed_remaining registra los bytes pendientes que conserva el driver
tras el fallo. No se usa por si solo para decidir en que ACK fallo la trama:
el sondeo de cero bytes es la evidencia independiente del rechazo de direccion.

## Hipotesis de pull-ups GPIO: no resuelve

Pinctrl I2C usa bias-pull-up, pero scl-gpios/sda-gpios del overlay comun solo
declaran open-drain. El controlador GPIO instalado configura el pull segun
los flags; se prueba añadir GPIO_PULL_UP exclusivamente en i2c_fault.overlay.
El DTS efectivo pasa de flags 0x6 a 0x16. No se modifica el overlay comun.

[Antes de repetir](Tiempos_D4_si7021_pullup_antes_clear_20261103_000815_027927.json)
ENV vuelve a estar valido al cargar esa imagen, sin haber ejecutado bus clear;
esta recuperacion no demuestra un efecto causal de los nuevos flags.
[Al repetir SDA 60 ms](Tiempos_D4_si7021_pullup_sda_60ms_20261103_000852_056709.json)
reaparece el mismo NACK tras F5. Un
[bus clear posterior con pull-ups](Tiempos_D4_si7021_pullup_clear_20261103_000934_626488.json)
retorna 0 pero no restaura ENV. La hipotesis no da una correccion suficiente;
se retira el override experimental del ensayo y no se incorpora a produccion.

## Identificacion y recuperacion previa

La [retencion larga](Tiempos_D4_si7021_id_tras_sda_largo_20261103_001324_397708.json)
restablece ENV como en el dia anterior. Una
[lectura puntual FC/C9](Tiempos_D4_si7021_identificacion_20261103_001343_202234.json)
devuelve datos 15 FF FF FF y CRC brutos B5/CB. El byte de modelo 0x15 es
compatible con Si7021; no autentica el circuito. El primer comprobador del
ensayo supuso CRC independientes y marco -EBADMSG. B5 coincide con CRC(15 FF)
y CB con CRC(15 FF FF FF); no se usa aquella suposicion para declarar un
defecto del sensor. La version final del diagnostico registra CRC brutos.
La validacion CRC de las medidas RH permanece intacta y si condiciona su validez.

## Correccion minima en adquisicion

Tras un fallo previo del bus, el modulo omitia I2C esa vuelta pero conservaba
configured=true. El siguiente vencimiento podia enviar F5 directamente.
Se ensaya una unica diferencia en un envoltorio temporal: configured=false
al recibir bus_interrupted, antes de delegar en el modulo real.

Dos pulsos SDA de 60 ms con ese cambio recuperan ENV:
[primero](Tiempos_D4_si7021_revalidar_sda_20261103_001643_124337.json) y
[repeticion](Tiempos_D4_si7021_revalidar_rep2_20261103_001755_585184.json).
La traza muestra E7/lectura del registro antes de F5 y despues RH/CRC/E0
correctos. En la primera toma, liberar a 11870 ms, E7/F5 a 12388 ms y par
valido a 12448 ms. No hay reset Si7021 ni init IMU, y no se omiten periodos.
Esta evidencia acredita la revalidacion previa como correccion del servicio;
no demuestra un mecanismo electrico interno del chip ni corrupcion de registro.

Se añade regresion host: interrumpir el bus sin conversion pendiente, simular
que environment_init falla en el siguiente vencimiento y exigir que no salga
F5 ni se renueve last_good_ms. El codigo anterior falla en la linea 255.
Tras incorporar configured=false al modulo real y actualizar la cuenta del
init esperado despues de cancelar una conversion, pasan las seis suites host.
La prueba conserva los casos de IMU ausente a 1 s para evitar volver a aplazar
indefinidamente ambiente. La cadencia sigue a 2 s; no hay sondeo ACK ni bucle nuevo.

Se retira el envoltorio que cambiaba comportamiento y se construye la imagen
que llama directamente a acquisition_environment_update real. Solo permanece
observacion/inyeccion de ensayo. Produccion ocupa FLASH 36260/RAM 8832 B;
observador 46868/9856 B. La API y los contextos conservan sus tipos.

## Verificacion del modulo real

Se carga la imagen con la correccion en acquisition_environment.c y sin el
envoltorio ambiental provisional. El desensamblado confirma llamada directa
a acquisition_environment_update. Se conservan los GPIO/overlay comunes
originales, sin el override de pull-ups probado anteriormente.

Tres inyecciones consecutivas en el mismo arranque:

| Caso | Captura | Retoma ENV desde liberacion |
| --- | --- | --- |
| SDA 60 ms | [Primera](Tiempos_D4_si7021_fix_breve_20261103_002014_116485.json) | 323 ms |
| SDA 60 ms | [Repeticion](Tiempos_D4_si7021_fix_repeticion_20261103_002042_584160.json) | 595 ms |
| SDA 5 s | [Larga](Tiempos_D4_si7021_fix_largo_20261103_002122_816264.json) | 1627 ms |

En las tres, IMU/nav/luz/ambiente quedan validos, cal=false y los valores de
sesgo/q0 coinciden exactamente con la
[referencia del arranque](Tiempos_D4_si7021_fix_baseline_20261103_001938_454211.json).
No hay init IMU ni reset Si7021. Las trazas no pierden eventos y no aparece
otra transferencia despues de la primera fallida dentro de la misma vuelta.
Las latencias ambientales dependen de la cadencia lenta conservada, no de
un reintento inmediato al liberar el jumper.

Al terminar el caso largo, cero periodos omitidos, maximo nominal observado
3235 us y de mantenimiento 2666 us. Este perfil incluye observacion por
mensaje; no se mezcla con el perfil sin trazas del dia anterior ni constituye
un WCET.

## Timeout SCL con la correccion real

El usuario apaga y mueve D8 de SDA/D14 a SCL/D15/PA12, conservando los sensores.
[Referencia](Tiempos_D4_si7021_fix_scl_baseline_20261103_002250_418526.json):
los tres sensores/nav validos y calibracion terminada. Se retiene SCL 5000095 us.

[Captura](Tiempos_D4_si7021_fix_scl_timeout_20261103_002315_350620.json): lectura
IMU fallida 5095 us; ENV_INIT falla en 5079 us mientras sigue el bloqueo.
Recuperaciones GPIO a 16393/17413/18433/19453/20473 ms (intervalos 1020 ms).
Tras liberar a 21387 ms, NEW IMU a 21509 ms y par ambiental a 22570 ms
(1183 ms desde liberar, segun su cadencia). Sesgo/q0 coinciden exactamente
con la referencia del mismo arranque. No se hacen init IMU ni reset Si7021.
No hay una segunda transferencia despues del primer fallo dentro de la misma
vuelta, no se pierden eventos y no se omiten periodos.

Maximo observado de vuelta completa de mantenimiento 5462 us, dentro de 20 ms;
nominal 3255 us, con cero >=10/20 ms. Todos los sensores/nav quedan validos.
La correccion se acredita en estas condiciones; no es un analisis WCET formal.

## Restauracion y resultado final

El usuario apaga, retira el jumper temporal y vuelve a alimentar conservando
los tres sensores. Se programa/verifica el observador normal corregido, sin
fixture ni envoltorios I2C. HEX SHA256:
D3E292D5FFC96477165C3FE8E92AB254247CBC66C646331F4F45464026D0E970.
Produccion, tambien construida y sin fixture: HEX SHA256
A4A5171AD0084A4A4A2C2002C315719581F2B40C7371FA29DB562505B8913042.

[Captura de 40 s](D4_si7021_corregido_final_20261103_002359_468273.txt):
39 cabeceras D4 y ultimo bloque incompleto al terminar. Dos reportes muestran
calibracion inicial pendiente; despues cal=0 y nav valido. IMU, luz y ambiente
son validos en los bloques completos. La ventana [5, 30005) ms publica las
cinco metricas: luz/T/HR n=15 cada una y aceleracion/giro n=1385 cada una.
Margen de pila observado: adquisicion 1560 B de los 2048 B solicitados y
main 2088 B de 3072 B, consultas sin error. No se extrapola ese margen a P3.

[Lectura SWD final](Tiempos_D4_si7021_final_observador_20261103_002448_674449.json):
2465 vencimientos y cero omisiones; 2464 vueltas nominales, maximo 3221 us
y cero >=10/20 ms. Una vuelta inicial de mantenimiento de 2514 us.
Sensores/nav validos, calibracion terminada y resultado estadistico listo.
La lectura pertenece al observador con consola, separada del perfil temporal
sin consola y del fixture. Los datos SWD se leen secuencialmente.

Pasan las seis suites host y los builds de produccion, observador, perfil
temporal y fixture. Compilacion Python, comprobacion de espacios y enlaces
documentales sin errores. La correccion de produccion consiste en una
asignacion a un estado ya existente y sus comentarios; no añade filtros,
ZUPT, calibraciones, resets de sensor ni una nueva API.

El defecto de retoma Si7021 se considera resuelto para los casos repetidos.
Se conserva toda la evidencia previa fallida. La entrega D4 queda para revision
conjunta, build del segundo equipo y aceptacion por Pablo; no se atribuyen esas
acciones aun no realizadas. Sin commit ni push. Placa con observador corregido,
todos los sensores conectados y jumper retirado.
