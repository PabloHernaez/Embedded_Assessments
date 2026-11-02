# Servicio de adquisicion (D4 en desarrollo)

El [estado de entrega D4](../../docs/ENTREGA_D4.md) resume construccion,
contrato de consumo, evidencias y la correccion de retoma ambiental tras
interrumpir el bus compartido.

## Responsabilidades

| Archivo | Responsabilidad |
| --- | --- |
| acquisition.c | Hilo/timer, orden de trabajo, mutex, snapshots y sesiones estadisticas. |
| acquisition_imu.c/.h | Estado privado, lectura, calibracion, movimiento, navegacion, correcciones y calidad de IMU. |
| acquisition_light.c/.h | Cadencia de luz, configuracion/lectura ADC, conversion y calidad. |
| acquisition_environment.c/.h | Cadencia, inicio/recogida del Si7021 y calidad del par ambiental. |
| ../algorithms/statistics.c/.h | Acumuladores y ventanas de 30 s sin dependencias de Zephyr. |

Las cabeceras acquisition_imu.h, acquisition_light.h y acquisition_environment.h
son privadas del servicio. Los consumidores incluyen solamente include/acquisition_api.h.
Las tres firmas publicas de adquisicion permanecen iguales.

Solo el hilo de adquisicion utiliza los contextos privados y los drivers.
Hardware y procesamiento se ejecutan fuera del mutex. acquisition.c copia
los resultados bajo un bloqueo breve y acumula solo lecturas nuevas validas.
El getter toma las medidas publicadas y StatsCtx.result bajo ese mismo mutex.

## Arranque y ciclo actual

El hilo estatico solicita 2048 bytes de pila, prioridad 3 y espera inicial
unica de 100 ms. Prepara los contextos e intenta configurar IMU antes de
iniciar el timer de 20 ms. El ciclo usa while (true): espera del periodo,
cierre estadistico, luz, IMU, ambiente, comprobacion lenta de configuracion y progreso.
Si se acumulan vencimientos, realiza
una vuelta actual y registra los periodos no atendidos individualmente.

Luz se intenta cada 2 s por reloj, independientemente del resultado de IMU.
Tras un fallo de configuracion o lectura ADC, se reintenta preparar el ADC en
el siguiente vencimiento de luz. Los valores retenidos no se acumulan.

Las referencias de luz se midieron el 02/11/2026: raw_dark=4062 y
raw_bright=128. Son provisionales para el KY-018 a 3,3 V sin pull-down
adicional de 47 kohm. La referencia iluminada usa la linterna del movil a
maxima potencia y 5 cm. Hay que repetirlas al añadir la resistencia o
cambiar el montaje. Capturas y alcance en
[el registro del LDR](../../tests/board/light/results/Calibracion_LDR_20261102.md).
La conversion y clipped reutilizan el driver existente, sin filtros nuevos.
El usuario acepta este montaje para el cierre de D4. La deteccion de
desconexion del LDR queda limitada; no se acredita con este ensayo.

## Ambiente: iniciar, esperar y recoger

AcquisitionEnvironment conserva la configuracion, el ultimo par ambiental y
dos vencimientos: next_read_ticks para iniciar otra medida y ready_ticks para
recoger la pendiente (-1 significa que no hay conversion pendiente).

El flujo reutiliza el driver y el ensayo de sensores, en el mismo hilo que IMU:

1. Al vencer la cadencia, configura el Si7021 si hace falta y envia F5 mediante
   environment_start. Conserva capture_begin_ticks desde antes de F5.
2. Al terminar F5, programa ready_ticks = tick_actual + ceil(40 ms) + 1 tick,
   conforme al contrato existente del driver. La cadencia siguiente queda
   aproximadamente 2 s despues del final de esta fase de inicio.
3. Mientras no vence ready_ticks, retorna al coordinador. Las siguientes
   vueltas siguen atendiendo IMU; no hay una espera de conversion dentro del paso.
4. La primera vuelta que alcanza ready_ticks llama a environment_read. El driver
   valida y devuelve temperatura y humedad como un solo par. sample_ticks se
   toma despues de recogerlo y fecha su last_good_ms.

Las transacciones I2C siguen siendo sincronas. La separacion evita mantener
el hilo esperando los 40 ms de conversion. Ambiente se atiende tambien durante
la calibracion inicial IMU.

acquisition_environment_update devuelve false cuando no hay datos/calidad
que publicar: durante la cadencia, la espera o un inicio correcto. Ese inicio
si modifica el contexto privado, aunque aun no tenga un nuevo par.
Devuelve true al recoger el par o invalidar calidad por un fallo.

Un error propio de configuracion, inicio o recogida invalida calidad y deja
configured=false para reconfigurar en el siguiente vencimiento. Conserva el
ultimo par y su fecha buena; no publica valores parciales.

Un fallo previo o recuperacion fisica del bus omite I2C ambiental esa vuelta.
Tambien deja configured=false: en el siguiente vencimiento permitido revisa
el Si7021 mediante environment_init antes de volver a enviar F5. Conserva la
cadencia; no presupone que la configuracion siga acreditada tras interrumpir el bus.
Si habia una conversion pendiente, la cancela, invalida calidad y pospone
el siguiente inicio 2 s desde esa vuelta. Si no habia pendiente, conserva
el vencimiento y calidad ambiental: las revisiones IMU repetidas a 1 s no
deben aplazar indefinidamente un sensor ambiental disponible.
La bandera bus_interrupted de cada contexto se reinicia en cada paso y marca
si hubo un fallo de I2C/cancelacion en esa vuelta. La IMU la activa ante error
de lectura, revision o init; NO_NEW y saturacion no la activan.
Un fallo ambiental impide la comprobacion opcional de configuracion posterior.

El coordinador publica el par bajo acquisition_lock e incorpora una sola vez
cada medida nueva valida a STATS_TEMP y STATS_RH, con las mismas fechas.
capture_begin_ticks pertenece al inicio de la conversion, no a su recogida:
si una sesion estadistica comienza mientras el sensor convierte, statistics_add
rechaza ese par para la sesion nueva. Los valores retenidos no se vuelven a contar.

## Calibracion inicial de IMU

El contexto IMU conserva GyroCalibration, la suma de aceleracion corregida y
la referencia initial_q. Reutiliza gyro_calibration_update y
estimator_set_initial_tilt con la misma secuencia del ensayo de sensores.

Solo las muestras NEW sin saturacion participan. Se necesitan 100 muestras
aceptadas con desviacion de la norma de aceleracion menor de 0.3 m/s2 respecto
a gravedad y norma del gyro sin corregir menor de 5 grados/s. NO_NEW conserva
el avance; una muestra fuera de esos limites, error o saturacion reinicia la
secuencia pendiente y la suma de aceleracion.

La media de aceleracion de esas mismas 100 muestras prepara la inclinacion
inicial. Si se rechaza, se repite la calibracion completa. Si se acepta, se
fija initial_q una sola vez, relative_q queda identidad y calibration_pending
pasa a false. Desde esa muestra se publica el gyro descontando su sesgo.
Antes se publica sin descontarlo y con calibration_pending=true.

Los errores posteriores conservan el sesgo y la referencia inicial. Durante
la calibracion nav.valid sigue false y velocidad/posicion en cero. La muestra
que la completa inicia la navegacion con dt=0.

## Movimiento confirmado

AcquisitionImu conserva un MotionDetector y previous_imu_ticks. El primero
recuerda el estado confirmado y el inicio de una posible transicion; el
segundo fecha la ultima muestra utilizable posterior a calibracion. Mientras
la calibracion esta pendiente, moving permanece false. La muestra que la
completa ya puede iniciar la evaluacion con giro corregido.

El flujo de una muestra utilizable queda asi:

    lectura -> correccion de aceleracion / sesgo gyro -> magnitudes
            -> motion_update -> ctx.moving -> snapshot.moving

La norma de aceleracion incluye gravedad. El detector compara su desviacion
respecto a 9.80665 m/s2 y la norma del gyro corregido, sin cambiar umbrales:

| Transicion | Condicion | Confirmacion |
| --- | --- | --- |
| Reposo a movimiento | Desviacion >0.8 m/s2 O giro >8 grados/s | 300 ms |
| Movimiento a reposo | Desviacion <0.3 m/s2 Y giro <3 grados/s | 2 s |

Por ejemplo, giro corregido de 9 grados/s desde t=1000 ms inicia una
confirmacion. Si las lecturas siguientes mantienen la condicion, moving
sigue false a t=1280 ms y pasa a true a t=1300 ms.

NO_NEW no evalua el detector ni cambia previous_imu_ticks. Error o saturacion
cancelan la confirmacion pendiente y borran la referencia temporal. En la
siguiente utilizable, un hueco mayor de 50 ms tambien cancela la confirmacion;
50 ms exactos se permiten. La lectura actual puede empezar una nueva cuenta.

Las interrupciones conservan moving, el ultimo estado confirmado. La calidad
IMU informa por separado de los fallos. moving representa actividad inercial
confirmada; la navegacion procesa tambien las muestras con moving=false.
El coordinador ya publica este campo bajo su mutex y no necesita cambios.

## Navegacion inercial

inertial_step coordina movimiento y navegacion despues de calibrar. Calcula
una sola vez el tiempo entre muestras utilizables, usando ticks del kernel:

    dt_s = elapsed_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC

Por ejemplo, 200 ticks con el reloj de 10000 ticks/s equivalen a 0.020 s.
Se integran intervalos de hasta 50 ms inclusive. La primera muestra tras
calibrar usa dt=0. Error, saturacion, hueco mayor de 50 ms o rechazo del
estimador requieren retomar con dt=0 en una lectura utilizable.

navigation_time_pending recuerda esa referencia temporal pendiente. No
modifica initial_q, la referencia fija de orientacion. NO_NEW conserva el
estado y la referencia temporal: la siguiente utilizable calcula su intervalo
desde la ultima utilizable, comprobando entonces el limite de 50 ms.

navigation_step llama a estimator_update con aceleracion corregida y gyro
descontando sesgo. El estimador existente prepara un candidato completo:

1. Integra el gyro corregido, convertido de grados/s a radianes/s, mediante
   Euler sobre el cuaternion q y lo normaliza.
2. Calcula relative_q = initial_q^-1 * q.
3. Rota la aceleracion al marco local usando el nuevo q y resta gravedad en Z.
4. Integra velocidad y posicion con Euler explicito:

       posicion_nueva = posicion_anterior + velocidad_anterior * dt
       velocidad_nueva = velocidad_anterior + aceleracion_lineal * dt

La posicion usa la velocidad anterior. Por ejemplo, con aceleracion lineal
constante de 1 m/s2 en X, velocidad/posicion iniciales cero y dt=0.020 s:

| Tiempo integrado | Velocidad X | Posicion X |
| --- | --- | --- |
| 0 ms | 0 m/s | 0 m |
| 20 ms | 0.020 m/s | 0 m |
| 40 ms | 0.040 m/s | 0.0004 m |

El candidato se confirma completo y nav.valid pasa a true. Un rechazo
conserva q, relative_q, velocidad y posicion y deja nav.valid=false. La
primera lectura aceptada al retomar usa dt=0: conserva esos valores y valida
de nuevo el resultado sin integrar el intervalo desconocido. No reinicia v/p.

El estado de movimiento no interviene en estas ecuaciones. Se conserva el
estimador existente con su integracion inercial y su deriva. nav.valid indica
aceptacion numerica de la actualizacion, no precision espacial.

El coordinador ya copia NavigationView completo bajo el mismo mutex que IMU
y movimiento. El snapshot recibe por tanto q, relative_q, v/p y valid juntos.

## Estadisticas de IMU en ventanas de 30 s

El motor statistics.c conserva sus acumuladores y reglas. La integracion
añade a STATS_ACCEL_NORM y STATS_GYRO_NORM las magnitudes de cada muestra
IMU nueva, utilizable y con preparacion inicial terminada.

1. Tras corregir aceleracion y descontar el sesgo gyro, inertial_step calcula
   accel_norm_mps2 = sqrt(ax*ax + ay*ay + az*az) y gyro_norm_dps de igual forma.
   Guarda esos escalares en AcquisitionImu. Movimiento y estadisticas usan
   exactamente los mismos valores; las raices se calculan fuera del mutex.
2. acquisition_imu_step solo continua si acquisition_imu_update devuelve true.
   Bajo acquisition_lock publica IMU/nav/moving y llama a
   acquisition_imu_statistics_add. Esta funcion exige calidad valida,
   calibration_pending=false y ambas magnitudes finitas.
3. Cada magnitud se ofrece una sola vez a statistics_add con los mismos
   capture_begin_ticks (antes de imu_read) y sample_ticks (despues de leer).
   statistics_add exige sesion activa, excluye capturas anteriores a la sesion
   y avanza la ventana segun el instante de obtencion antes de acumular.
4. El acumulador actualiza sum, min, max y count. Al cerrar 30 s publica
   mean=sum/count cuando count>0, conservando el resultado completo para el
   getter. El cierre periodico sigue funcionando aunque no llegue ninguna
   muestra nueva; count=0 identifica una metrica sin observaciones.

La aceleracion incluye gravedad: en reposo su magnitud ronda 9.81 m/s2.
El giro esta en grados/s y ya tiene el sesgo descontado. Se promedian
magnitudes individuales, no las componentes X/Y/Z ni aceleracion lineal de
navegacion. moving y nav.valid no condicionan estas estadisticas de sensores.

NO_NEW conserva los datos sin volver a contarlos. Error, saturacion y
calibracion pendiente se excluyen; la muestra que completa la preparacion ya
puede incorporarse. Dos NEW con igual valor son dos observaciones diferentes.
El getter solo copia resultados: leer diez snapshots no añade diez muestras.

Los limites de ventana siguen anclados al comienzo de la sesion, aunque al
principio falten muestras por calibracion. Una captura que comienza antes de
una nueva sesion queda fuera de ella. Una lectura que cruza el fin de ventana
pertenece a aquella en la que termina; exactamente en el limite va a la siguiente.

Ejemplo sintetico de tres observaciones admitidas en una misma ventana:

| Muestra | Magnitud aceleracion (m/s2) | Magnitud giro (grados/s) |
| --- | --- | --- |
| 1 | 13 | 5 |
| 2 | 13 | 5 |
| 3 | 5 | 12 |

Al cerrar: aceleracion tiene count=3, media=31/3, min=5, max=13;
giro tiene count=3, media=22/3, min=5, max=12. Estos numeros son datos de
prueba, no referencias nuevas de calibracion del montaje.

## Recuperacion IMU

El mismo ciclo de adquisicion atiende tres rutas, en este orden:

1. Si hay revision pendiente y vencio, lee PWR y las escalas mediante
   imu_check_configuration. Si coinciden habilita la lectura de la proxima
   vuelta; si difieren deja configuracion pendiente de init.
2. Si falta configuracion, intenta imu_init cuando ha pasado al menos 1 s
   desde el comienzo del intento anterior. No reinicializa AcquisitionImu.
3. En otro caso lee una muestra y aplica el procesamiento habitual.

El estado de recuperacion es un contador de errores, review_pending y tres
fechas: next_review_ticks, last_init_ticks y next_configuration_check_ticks.
NEW utilizable limpia el contador; NO_NEW y saturacion lo conservan. Tras
tres errores, la primera revision se atiende en la vuelta siguiente. Solo
las revisiones fallidas se posponen 1 s; un glitch no añade esa espera inicial.

Ejemplo, con tiempos relativos al comienzo del fallo:

| Tiempo | Trabajo |
| --- | --- |
| 20 / 40 / 60 ms | Tres errores de lectura; revision pendiente. |
| 80 ms | Registros correctos; no se escribe configuracion. |
| 100 ms | Primera NEW utilizable con dt=0. |

Un error invalida calidad y nav.valid, conservando valores y last_good_ms.
La interrupcion conserva el sesgo terminado, initial_q, q/relative_q y v/p;
solo una calibracion aun pendiente se reinicia. La primera NEW al retomar
usa dt=0, sin integrar el intervalo desconocido. Revision e init no añaden
observaciones a estadisticas. imu_init mantiene sus esperas existentes de
reset/estabilizacion, fuera del mutex, y el contador de periodos registra
los vencimientos que se acumulen durante esa operacion excepcional.

El STM32 instalado puede devolver -EIO tanto para NACK como para timeout.
Tras el error se observan SCL/SDA sin reconfigurar los GPIO ni transferir I2C.
Solo una linea baja permite intentar i2c_recover_bus; un timeout explicito
tambien espacia la revision, aunque las lineas esten libres. La revision a
1 s limita los intentos fisicos, sin otro temporizador. Un fallo al observar
GPIO no es evidencia de bloqueo. La revision posterior verifica la retoma.

La configuracion efectiva ya incluye recuperacion STM32 y scl-gpios/sda-gpios
en PA12/PA11. Despues de cualquier recuperacion fisica se descarta ENV pendiente.
Cada 2 s, al terminar ambiente y solo si el bus sigue permitido, se reutiliza
la misma comprobacion de PWR/escalas para detectar un reset silencioso/SLEEP.
Un fallo tardio de esa comprobacion cancela ENV sin mas transferencias.
La politica de STATE/PROBLEM y su histeresis permanecen en la aplicacion.

## Limites actuales

- Las referencias de luz son provisionales para el montaje sin 47 kohm.
  Hay que repetirlas con el circuito definitivo; el diagnostico de
  desconexion sigue pendiente.
- La aceleracion usa las mismas constantes preliminares del ensayo D3.
- NO_NEW conserva calidad/fecha; error o saturacion invalidan calidad y
  navegacion, conservando los ultimos valores utilizables.
- Ausencia/retoma individual de IMU y Si7021 se contrastaron en placa.
  Bus retenido, glitch breve y reset silencioso necesitan ensayos controlados;
  las pruebas host simulan driver, niveles GPIO y reloj.
- El margen observado de pila esta registrado para el perfil de ensayo;
  la medida temporal sin barrido cumple el objetivo nominal en los casos
  registrados. Faltan bus retenido/timeout, desglose de operaciones y carga P3.

## Referencias de orientacion

El contexto IMU conserva initial_q (q0) como referencia privada fija.
NavigationView.q usa el marco local con Z arriba; relative_q alimenta
ROLL/PITCH/YAW de SR4.
No recapturar q0 por NO_NEW, fallos rutinarios o cambios de modo.
El servicio actualiza ambos campos al navegar y los publica en NavigationView.
Este incremento no cierra D4 ni agrega algoritmos nuevos.

## Compilacion

El CMake raiz incluye las cuatro fuentes de acquisition, los drivers y los
algoritmos usados. Construir con la tarea habitual de produccion. El ensayo
independiente tests/board/sensors conserva sus fuentes y flujo propios.

El ensayo integrado tests/board/acquisition enlaza estas mismas fuentes y
observa la API publica, con consola solo en su perfil. El
[registro de placa](../../tests/board/acquisition/results/Ensayo_D4_20261102.md)
conserva arranque, ventanas, movimiento, luz, sesiones y ausencia/retoma
individual de IMU y ambiente. Detecto y contrasto la correccion del aplazamiento
indefinido de ENV durante revisiones IMU fallidas. El perfil observado dejo
1584 B de pila libre en adquisicion y registro 0 periodos omitidos en 19927
vencimientos. Esos datos pertenecen al perfil/imagen de aquella captura.

La [medicion temporal posterior](../../tests/board/acquisition/results/Tiempos_Adquisicion_D4_20261102.md)
usa D4_TIMING_ONLY=ON, sin consola ni barrido, con estadisticas activas.
Los 64 B privados de timing conservan reloj, arranque, numero de vueltas,
maximos nominal/mantenimiento y excesos de 10/20 ms; se leen por SWD sin
añadir API ni imprimir desde adquisicion. La resta de k_cycle_get_32 mide
el trabajo despues de esperar el timer y hasta progress, incluidas esperas
I2C/mutex e interrupciones; excluye el propio registro temporal.

Se observaron 20790 vueltas nominales con maximo 3,217 ms y cero >=10 ms.
Una vuelta con reinicializacion completa provocada duro 147,416 ms y registro
6 periodos omitidos; conserva los sleeps existentes de 100+40 ms. Sesgo y q0
permanecieron identicos. Es un maximo observado con margen nominal de 6,783 ms,
no un WCET garantizado ni una medida aislada de cada transferencia.
