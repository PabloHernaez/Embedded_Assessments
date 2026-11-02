# Tiempos de adquisicion D4: 02/11/2026

Base 3f9822a en feat/d3-estimacion, con D4 local sin commit. NUCLEO-WL55JC,
Zephyr 4.4.2, SDK 1.0.1/GCC 14.3.0. Perfil D4_TIMING_ONLY=ON: configuracion
comun de produccion, build_checks.c activo, sin consola/printk ni INIT_STACKS.
Hilo de adquisicion prioridad 3 y pila 2048 B; main prioridad 4/pila 2048 B
solo abre estadisticas y duerme. Se grabo/verifico con STM32CubeProgrammer.
El usuario confirma los tres sensores conectados e IMU inmovil al cargar.
El LDR mantiene sus referencias provisionales 4062/128, sin 47 kohm.

HEX SHA256: F5ECC996A7EE51F8DB980EB2B1E585CFB8CF4858DB1479A50314BC0E7C01DD4C.
ELF SHA256: 20566F80FAAFE924D8A92DFB5E83A6A8008B89D746AD7077A6D0749A87392AD9.
FLASH 36324 B, RAM 8832 B en el perfil temporal. Produccion: 36260/8832 B;
el observador con consola tambien compila (46860/9856 B). Frente al build
anterior de produccion, la instrumentacion añade 384 B FLASH y 64 B RAM.

## Metodo y criterio

Se mide la vuelta completa tras acquisition_wait_for_period y hasta terminar
progress. Incluye ADC, IMU/calculos, ambiente, estadisticas, comprobacion de
configuracion y publicaciones, con esperas I2C/mutex e interrupciones. Excluye
la espera periodica y el registro de los propios diagnosticos.
No se suman maximos de operaciones distintas. No se midio un desglose por driver.

k_cycle_get_32 usa SysTick del Zephyr instalado. Reloj efectivo comprobado:
48000000 ciclos/s; 48 ciclos/us. La resta uint32_t admite cruzar el desborde
si la medida dura menos de unos 89 s. Se probaron en host los limites exactos
10/20 ms, categorias, cruce del contador y expiraciones multiples. Pasaron
las seis suites; el ensayo temporal compila con -Wall -Wextra -Werror.

Nominal requiere contextos configurados al empezar y sin fallo/revision en
la vuelta; mantenimiento incluye configuracion/revision/fallos. La calibracion
matematica en vueltas regulares pertenece a nominal. El arranque anterior
al timer, con sus sleeps, se mide por separado. Objetivo de la guia:
nominal <10 ms dentro del periodo de 20 ms. Se cuentan >=10 y >=20 ms.
Los valores en us se redondean hacia arriba, sin descontar instrumentacion.

Los maximos y contadores son acumulados desde el arranque; no se reinician
entre fases. Un maximo sin cambio limita tambien las vueltas nuevas, aunque
no identifica su maximo individual. Leer_tiempos.py resuelve tipos/direcciones
con GDB sin conexion y usa solo upload SWD HOTPLUG. JSON conserva ciclos,
hashes y estado. Las estructuras se leen secuencialmente, no atomicamente.

## Arranque, reposo y movimiento

Capturas: [arranque](Tiempos_D4_arranque_20261102_231225_588992.json),
[reposo](Tiempos_D4_reposo_60s_20261102_231345_057110.json) y
[movimiento](Tiempos_D4_movimiento_20261102_231442_896662.json).
El usuario confirma reposo inicial y despues giros suaves de 6-8 s seguidos
de al menos 10 s quieto. El registro etiquetado reposo_60s corresponde a
mas de 90 s de ejecucion; 60 s era la duracion minima prevista.

| Lectura | Vueltas nominales acumuladas | Maximo nominal, us | >=10 ms | Periodos omitidos |
| --- | --- | --- | --- | --- |
| Primera comprobacion | 631 | 3217 | 0 | 0 |
| Reposo, al menos 60 s | 4605 | 3217 | 0 | 0 |
| Tras movimiento y reposo | 7498 | 3217 | 0 | 0 |

Maximo nominal exacto: 154414 ciclos (3216,9583 us), fechado a 2251 ms.
Margen conservador frente a 10 ms: 6,783 ms. No hay vueltas nominales >=20 ms.
La ultima ventana publicada al terminar reposo es [60000, 90000) ms: se
ejecutaron cierres estadisticos y trabajos lentos durante la medida.
En las tres lecturas los sensores/nav son validos, cal=false y estadisticas
activas; tras los giros moving vuelve a false. Esto verifica el estado al leer,
no constituye una traza de todas las muestras ni fecha la accion fisica.

Arranque antes del timer: 11888331 ciclos, 247674 us redondeados; incluye
las esperas iniciales. Primera vuelta de configuracion: 120503 ciclos,
2511 us. Par de lecturas consecutivas del reloj: 67 ciclos, unos 1,396 us;
es una referencia de sobrecoste, no un limite del coste total del registro.

## Ausencia y retoma de IMU

Capturas: [ausencia](Tiempos_D4_imu_ausente_20261102_231546_975909.json) y
[retoma](Tiempos_D4_retoma_imu_20261102_231624_802577.json). El usuario confirma
la retirada exclusiva de SDA del MPU y despues su reconexion, manteniendo
Si7021 y LDR conectados. Se observan IMU/nav invalidas durante ausencia y
validas tras retoma, cal=false, luz/ambiente validos en ambas lecturas.

La primera lectura acumula 2100 vueltas de mantenimiento; la segunda 3064.
El maximo acumulado de mantenimiento sigue en 2511 us, con cero vueltas >=20 ms
y cero periodos omitidos. Por tanto, las nuevas vueltas del caso ausente/retoma
tambien quedan bajo ese limite observado, aunque el pico pertenece a la
configuracion inicial y no identifica el maximo individual del caso NACK.

## Reinicializacion completa controlada

La [inyeccion registrada](Tiempos_D4_inyeccion_reinit_20261102.json) pone una
sola vez configured=false en RAM via SWD HOTPLUG -w8, a las 23:17:37 +01:00.
Direccion 0x20000400 verificada en el ELF como imu_context.configured, valor
previo 1; se escribe un byte 0. No se reinicia la MCU. El hilo activa su
imu_configuration_init existente y este llama al imu_init real del driver.
Es una inyeccion de estado, no un ensayo de deteccion de reset silencioso.

[Captura posterior](Tiempos_D4_reinit_controlado_20261102_231753_564225.json):

- Una vuelta de mantenimiento >=20 ms, maximo 7075941 ciclos = 147415,4375 us,
  mostrado conservadoramente como 147416 us. Es la vuelta completa, no el
  tiempo aislado de imu_init. El driver contiene sleeps existentes de 100 y 40 ms.
- Pico fechado a 325155 ms de uptime. Se registran 6 periodos omitidos.
- 17040 vencimientos y 13969+3065=17034 vueltas medidas: la diferencia es 6,
  sin reproducir una lectura por cada vencimiento acumulado.
- El maximo nominal permanece en 3217 us, sin excesos de 10/20 ms.
- Despues IMU/nav/luz/ambiente estan validos y calibration_pending=false.

Se leen antes y despues 44 B desde 0x200004a4, que contienen bias_dps y
initial_q junto con los campos intermedios. El bloque completo permanece
identico. El JSON de inyeccion conserva hashes y valores decodificados de
sesgo/q0; una lectura final confirma configured=1 restablecido por el hilo.
Las direcciones solo corresponden a este ELF y no deben reutilizarse con otro.

## Comprobacion final y conclusion

[Lectura final](Tiempos_D4_final_20261102_232009_898070.json):
20790 vueltas nominales, maximo 3217 us, cero >=10 ms y cero >=20 ms.
3065 vueltas de mantenimiento, una >=20 ms (la reinicializacion provocada).
23861 vencimientos - 6 omitidos = 23855 vueltas = 20790+3065.
Las omisiones siguen en 6, sin aumentar despues de la inyeccion.
Sensores/nav validos, cal=false, moving=false y estadisticas activas;
ultimo resultado estadistico [420000, 450000) ms.

El objetivo nominal <10 ms se cumple en estas condiciones, con margen
conservador de 6,783 ms. El arranque de 247674 us transcurre antes de activar
el timer. La vuelta de reinicializacion de 147416 us excede el periodo de
20 ms por sus esperas deliberadas y deja 6 omisiones registradas.
La placa queda con el perfil temporal sin consola, todos los sensores
conectados y la medicion/estadisticas activas. No se hacen commit ni push.

## Limites

Los resultados son maximos observados en este perfil, no un WCET demostrado.
Faltan bus retenido/timeout real con fixture y la concurrencia de la futura
aplicacion integrada con GPS, formato, salida y supervision.
El [ensayo I2C posterior](I2C_D4_20261102.md) cubre bus retenido y timeout
real con otro perfil instrumentado; conserva sus resultados separados y
documenta el defecto de retoma ambiental tras el pulso corto de SDA.
No se midio el desglose de cada transaccion ni la latencia desde el instante
fisico de expiracion hasta que el hilo despierta. El margen de pila 1584 B
del ensayo integrado anterior pertenece a aquella imagen/perfil; no se
barre la pila durante estas medidas temporales.
