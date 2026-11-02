# D3: ensayo de deriva y decisión sobre la base inercial

**Fecha:** 2 de noviembre de 2026.
**Responsable del desarrollo:** David. **Revisión cruzada con Pablo:** pendiente.
**Alcance:** aplicación de ensayo `tests/board/sensors`; evidencia para D3-14/T10 y decisión inicial de D3-15. No constituye el cierre completo de D3 ni la aceptación de producción.

## Resultado

La captura registra un único arranque y 600 segundos de navegación, con 601 reportes completos. No aparecen errores de lectura, saturaciones, huecos superiores al límite ni rechazos numéricos durante la navegación. Sin embargo, la posición y la velocidad estimadas acumulan una deriva muy grande en reposo: al finalizar, sus magnitudes alcanzan 55 567,65 m y 284,24 m/s.

La inicialización de inclinación mediante gravedad reduce claramente la aceleración horizontal ficticia del arranque. Permanecen errores de magnitud del acelerómetro y giro residual; al integrar sin referencias correctoras, ambos vuelven a producir deriva. `NAV=1` indica que el cálculo fue aceptado numéricamente, no que la trayectoria tenga precisión física garantizada.

## Evidencia y condiciones

- Captura original: [Ensayo_deriva_600s_20261102_165247_571741.txt](../tests/board/sensors/results/Ensayo_deriva_600s_20261102_165247_571741.txt).
- Herramienta: [capturar_deriva.py](../tests/board/sensors/capturar_deriva.py), que espera RESET y conserva el último reporte completo que alcanza 600 s desde el primer reporte de navegación.
- Placa: NUCLEO-WL55JC, target `nucleo_wl55jc/stm32wl55xx`.
- Entorno registrado del proyecto: Zephyr 4.4.2, SDK 1.0.1, ARM GNU 14.3.0 y west 1.5.0.
- Perfil: ensayo de sensores con consola y diagnóstico; serie 115200, 8N1. No es una captura del firmware integrado de producción.
- Condición solicitada: MPU6050 inmóvil sobre un soporte estable durante calibración y medición. No se utilizó una referencia externa de orientación o posición.
- La duración se obtiene del reloj del kernel. Este ensayo no contrasta la precisión del reloj con un patrón externo.
- El nombre del archivo identifica el inicio de la captura; no se deduce de él una zona horaria metrológicamente verificada.

| Registro | Valor |
| --- | --- |
| Primer reporte | `t=2274 ms` |
| Último reporte | `t=602274 ms` |
| Tiempo transcurrido | `600000 ms` |
| Reportes de navegación | 601, separados por 1000 ms de uptime |
| Sesgo gyro calculado al arrancar | `[-2.805, -0.646, -0.377] °/s` |
| Aceleración inicial media, ya corregida | `[0.6234, 0.0407, 9.8815] m/s²` |
| Norma inicial media impresa | `9.9013 m/s²` |
| Cuaternión inicial impreso, Hamilton `[w,x,y,z]` | `[0.9995, 0.0021, -0.0315, 0.0001]` |

La rama inspeccionada al redactar es `feat/d3-estimacion`, con HEAD `9dc45f1` (`feat: add quaternion orientation integration`). Ese HEAD no identifica por sí solo las modificaciones posteriores utilizadas en la captura. Antes de la entrega se debe vincular el código completo, su build y esta evidencia a una revisión reproducible.

**Integridad de la captura original:** 203209 bytes; SHA-256 `6e4924514f9fa20dd20b83ab829944c1439e4f9910de60009e9d0f90baa53e88`.

## Cambios abordados antes de este ensayo

### Límite específico de calibración del giroscopio

La calibración inicial necesita 100 muestras nuevas, utilizables y consecutivas aceptadas por su criterio de reposo. `NO_NEW` conserva el progreso; un rechazo de reposo, error o saturación reinicia la secuencia pendiente.

Se utilizaba el límite de reposo de 3 °/s sobre el giro todavía sin corregir. El sesgo observado rondaba 2,96 °/s de magnitud, por lo que pequeñas fluctuaciones podían superar el límite y reiniciar el contador. Se separó `TRACKER_GYRO_CALIBRATION_MAX_DPS=5.0f` del límite de reposo posterior, que sigue siendo 3 °/s. Es un margen inicial justificado por las lecturas; no demuestra por sí solo inmovilidad.

El usuario observó también un retorno negativo `-15`. Su origen no se identificó. La mejora de la calibración y la ausencia de errores en esta captura no acreditan que aquel incidente esté resuelto.

### Corrección preliminar del acelerómetro

Se promediaron 500 lecturas nuevas en cada una de seis posturas. Los resultados siguientes fueron proporcionados por el usuario en la conversación, en el orden solicitado; no se inventa un archivo original independiente para esas seis mediciones.

| Postura | X, m/s² | Y, m/s² | Z, m/s² |
| --- | ---: | ---: | ---: |
| +Z | 0,92416 | −0,00153 | 9,86308 |
| −Z | 0,07998 | −0,20392 | −9,96534 |
| +X | 9,96603 | −0,13425 | −0,69727 |
| −X | −9,71785 | 0,09026 | 0,26911 |
| +Y | 0,29383 | 9,77774 | −0,37772 |
| −Y | 0,11177 | −9,90102 | 0,09263 |

Para cada eje se calculó `sesgo=(lectura_+ + lectura_-)/2`, `factor=2g/(lectura_+ - lectura_-)` y `a_corregida=(a_medida-sesgo)*factor`, con `g=9.80665 m/s²`.

| Eje | Sesgo, m/s² | Factor multiplicador usado |
| --- | ---: | ---: |
| X | +0,12409 | 0,9964143 |
| Y | −0,06164 | 0,9966736 |
| Z | −0,05113 | 0,9891509 |

Las constantes son preliminares: las componentes transversales muestran desviaciones respecto a las posturas ideales y pueden incluir desalineación y errores del sensor. No se acreditó una calibración metrológica ni se separaron todos esos efectos. En el ensayo la corrección se aplica una vez en `imu_sample_read()`, antes de que calibración, movimiento y navegación consuman la lectura. Los valores AX/AY/AZ conservan los ejes del sensor y la gravedad.

### Inicialización de la inclinación

Antes se comenzaba siempre con cuaternión identidad, suponiendo el sensor nivelado. Se añadió `estimator_set_initial_tilt()` y un acumulador de aceleración sincronizado con las mismas 100 muestras aceptadas para calibrar el giroscopio. Si la secuencia se interrumpe, ambas sumas pendientes se reinician; `NO_NEW` no incorpora ni borra muestras.

Al completar la secuencia, el promedio de aceleración corregida determina roll y pitch iniciales. Se elige yaw inicial cero en el marco local alineado con la gravedad. El cuaternión Hamilton resultante transforma del sensor al marco local, con Z hacia arriba. No determina norte ni otra referencia absoluta de rumbo.

Esta preparación se realiza una vez antes de integrar, conservando velocidad y posición iniciales cero. La primera muestra utilizable posterior recibe `dt=0`. No es renivelado continuo ni una modificación de offsets para absorber la inclinación.

### Verificación dinámica y contrato SR4

Después del ensayo de deriva se realizó una captura dinámica de 30 s: [giro de ida y regreso](../tests/board/sensors/results/Ensayo_deriva_30s_20261102_172320_736933.txt). El yaw estimado se mantuvo entre 90,74° y 90,84° durante la pausa de 11–16 s, con promedio 90,79°. Al finalizar quedó en −3,25°, frente a −0,01° al iniciar. El retorno no se midió con un patrón angular independiente; la diferencia puede incluir exceso de giro manual y error del sensor, por lo que no constituye una medida de precisión angular certificada.

El detector registró movimiento/reposo a los 4,18 / 12,16 / 17,28 / 24,38 s respecto al primer reporte. La secuencia es coherente con la manipulación. No se registraron los comienzos de cada condición candidata y no se validan así los tiempos exactos de confirmación. El pitch cambió unos 1,23° durante la ida; no se separaron inclinación manual y error de estimación. La prueba respalda una demostración breve de giro alrededor de Z, no la validación dinámica de los tres ejes ni una trayectoria precisa.

Para SR4 se añadió `relative_q` a `NavigationView`. `q` conserva la transformación del sensor al marco local con Z arriba para navegación. `relative_q=q0^-1 ⊗ q` transforma del sensor actual a sus ejes iniciales y es la entrada acordada en los headers para obtener ROLL/PITCH/YAW. `q0` permanece en el contexto privado del productor, se fija al terminar la preparación inicial y no se recaptura por pausas, modos o fallos rutinarios.

`estimator_update` recibe esa referencia y calcula un candidato con ambas orientaciones, velocidad y posición. Sólo confirma el conjunto si las actualizaciones son utilizables. Los seis servicios públicos de aplicación mantienen sus firmas y la trama sigue teniendo 221 bytes; el tipo compartido `NavigationView` incorpora un cuaternión adicional y debe llegar completo al snapshot.

Se comprobaron dos capturas de reposo de 10 s: [cálculo relativo en el ensayo](../tests/board/sensors/results/Ensayo_deriva_10s_20261102_174455_612323.txt) y [lectura desde el modelo compartido](../tests/board/sensors/results/Ensayo_deriva_10s_20261102_181219_304185.txt). Ambas contienen 11 reportes, orientación relativa inicial identidad y ERR/SAT/GAP/NAV_ERR a cero. En la segunda, los Euler relativos finales aproximados fueron roll +0,023°, pitch −0,126° y yaw +0,149°. La inclinación interna de navegación se conserva.

Esto verifica la preparación del dato relativo en el productor de ensayo. La conversión y emisión real de Euler por P2, la publicación mediante D4 y su integración siguen pendientes. Si posteriormente se corrige `q` con el acelerómetro, esa corrección también aparecerá en `relative_q` y habrá que revisar la interpretación de SR4.

## Resultados de los diez minutos

### Velocidad y posición

Las magnitudes se calcularon como normas euclídeas de los tres componentes impresos. Bajo la condición de reposo, representan deriva respecto al estado inicial.

| Tiempo desde el primer reporte | Magnitud de velocidad, m/s | Magnitud de posición, m |
| --- | ---: | ---: |
| 1 s | 0,098 | 0,047 |
| 5 s | 0,480 | 1,203 |
| 10 s | 0,963 | 4,785 |
| 15 s | 1,447 | 10,806 |
| 30 s | 2,942 | 43,581 |
| 60 s | 6,327 | 180,200 |
| 120 s | 15,796 | 809,808 |
| 300 s | 71,330 | 8040,128 |
| 600 s | 284,242 | 55 567,650 |

Valores finales: `v=[-219.388,-173.721,49.833] m/s`; `p=[-43724.824,-30130.648,16372.157] m`.

El primer reporte con magnitud de posición superior a 1 m aparece a los 5 s. Esto no fija un requisito de precisión ni el instante exacto del cruce entre reportes; describe el resultado observado.

### Orientación

Para analizar el log se normalizaron los cuaterniones impresos a cuatro decimales y se convirtieron a Euler ZYX. Las cifras son aproximadas por ese redondeo.

| Ángulo | Inicial | Final | Diferencia final menos inicial |
| --- | ---: | ---: | ---: |
| Roll | +0,241° | +3,910° | +3,669° |
| Pitch | −3,610° | −8,741° | −5,130° |
| Yaw | +0,004° | +5,304° | +5,300° |

La rotación neta obtenida de `q_inicial^-1 ⊗ q_final` es aproximadamente 8,49°. Las diferencias de ángulos de la tabla no deben confundirse con los ángulos de ese cuaternión relativo.

A los 30 s, las diferencias de roll/pitch/yaw eran aproximadamente `+0.047°, -0.207°, +0.308°`; a los 60 s, `+0.113°, -0.484°, +0.683°`. Esto respalda una presentación breve de orientación en reposo, pero no sustituye los ensayos dinámicos con giros conocidos.

### Giro residual y adquisición

Hay 598 diagnósticos de aceleración/giro: tres reportes de navegación coinciden con `IMU_NO_NEW` y omiten esas líneas. El promedio del giro corregido de los diagnósticos es aproximadamente `[+0.00712,-0.00758,+0.01011] °/s`. Es una estimación a partir de la salida de aproximadamente 1 Hz, no del conjunto de muestras incorporadas a 50 Hz.

| Contador o intervalo | Resultado final |
| --- | --- |
| NEW | 29666 |
| NO_NEW | 335; aproximadamente 1,12 % de las consultas contabilizadas |
| ERR / SAT / GAP / NAV_ERR | 0 / 0 / 0 / 0 |
| `dt` mínimo / máximo aceptado registrado | 6,0 / 40,2 ms |
| `moving` | 0 en los 601 reportes |
| `NAV` | 1 en los 601 reportes |

Los contadores son coherentes con 50 consultas/s. Hay variación en los intervalos de lectura; los extremos registrados permanecen dentro del límite de 50 ms. El log resume el último intervalo y los extremos acumulados: no permite reconstruir un histograma de todos los `dt`, medir la precisión del reloj externo ni demostrar rutas de fallo que no se ejecutaron.

`moving=false` no autoriza automáticamente una corrección de velocidad cero: el detector arranca así y conserva su estado ante condiciones intermedias. No certifica reposo físico. En este ensayo no se aplican ZUPT ni congelación de posición.

## Interpretación

La norma inicial supera la gravedad constante restada en aproximadamente `0.09465 m/s²`. Su integración predice `0.5*0.09465*10²=4.7325 m` en Z a los 10 s; el log muestra 4,780 m. La coincidencia explica la deriva vertical inicial.

La deriva posterior de orientación es compatible con el giro residual y vuelve a introducir componentes de gravedad en los ejes horizontales. La captura no permite atribuir cada error a una causa única ni demostrar un origen térmico: el Si7021 mide ambiente, no la temperatura interna del MPU.

No se observaron fallos numéricos o de lectura durante la navegación. Eso no acredita precisión espacial, inmunidad a fallos intermitentes ni la aceptación completa del servicio definitivo.

## Decisión DEC-D3-01: conservar la base y declarar su deriva

**Decisión actual:** mantener la integración inercial de la base para avanzar conforme al plan. Conservar la corrección preliminar del acelerómetro, el límite específico de calibración gyro y la inicialización de inclinación ya incorporados al ensayo. Documentar estas ampliaciones y sus límites.

El enunciado, sección 2.4.9, admite que las estimaciones por integración acumulen deriva y exige discutirla. Esta evidencia respalda esa explicación; no convierte las estimaciones en una trayectoria físicamente precisa.

**Demostración preliminar:** plantear segmentos breves de orientación, del orden de 10–30 s, y explicar por separado la integración de velocidad/posición y su deriva medida. Existe ya una captura dinámica de 30 s de giro alrededor de Z y regreso, con el alcance descrito arriba. La demostración integrada y su criterio de aceptación siguen pendientes. No hay un tramo acreditado para mostrar desplazamientos pequeños con precisión, ni se ha acordado una tolerancia espacial cuantitativa.

ZUPT, congelación o retroceso de posición, sesgo gyro adaptativo, gravedad adaptativa `g_ref`, renivelado continuo y corrección por GPS quedan como propuestas no implementadas. Si se necesitan para la demostración acordada, se diseñarán y comprobarán como una decisión posterior explícita. No se clasifican automáticamente como ADVANCED; ese alcance depende de la asignación docente.

En P2 se debe conservar el recorte de los campos emitidos, sin limitar artificialmente el estado del estimador: posición `±9999.99 m` y velocidad `±999.99 m/s`. En esta captura el primer reporte con alguna componente de posición fuera del rango del formato aparece a los 357 s. Los flags `0x0040` y `0x0080` siguen correspondiendo a la base sin corrección GPS y sin referencia fiable de yaw.

## Pendientes para cerrar D3

1. **Integración de SR4.** El cálculo relativo y su campo en el modelo están implementados y contrastados en el ensayo. Queda revisar el contrato con Pablo y verificar la conversión y emisión de ROLL/PITCH/YAW desde `relative_q` en P2, junto con el snapshot real de D4. El cuaternión de navegación no debe convertirse directamente para esos campos.
2. **D3-15.** Completar la demostración integrada y explicar el comportamiento aceptado de posición/velocidad. La captura dinámica alrededor de Z y la decisión de conservar la base están registradas; no se acredita precisión angular metrológica, trayectoria precisa ni toda la demo integrada.
3. **Trazabilidad.** Asociar fuentes y build a un commit del incremento completo y conservar las capturas. La actualización de `tests/README.md`, `docs/README_DESARROLLO.md` y el contrato de telemetría se incluye en la preparación del commit. No se archivaron los ELF exactos de cada captura y no se atribuye retroactivamente su compilación a un hash nuevo.
4. **Revisión cruzada y pruebas restantes.** Pablo debe revisar y explicar la actualización y la compensación de gravedad. Conservar como pendientes las pruebas de fallos, recuperación e integración no respaldadas por esta captura. No se marca todo D3 ni T07–T10 como aprobado por haber realizado el ensayo de deriva.

**Estado:** D3-14 medido y documentado; giro dinámico alrededor de Z y dato relativo SR4 contrastados; decisión base registrada; integración de P2/D4, D3-15 y cierre completo de D3 pendientes.

## Referencias locales

- Plan de trabajo de ingenieria del firmware, bloque D3 (documento de planificacion externo al repositorio).
- Guia tecnica de implementacion, Estimacion y movimiento (documento de planificacion externo al repositorio).
- [Ensayo nav 1](../tests/board/sensors/results/Ensayo_nav_1.txt), [nav 2](../tests/board/sensors/results/Ensayo_nav_2.txt), [nav 3](../tests/board/sensors/results/Ensayo_nav_3.txt) y [nav 4](../tests/board/sensors/results/Ensayo_nav_4.txt).
- Enunciado: `Proyecto_Final/Guidelines/emb_pla_com_final_project_2627.pdf`, SR4 y sección 2.4.9.

Las capturas breves tienen duraciones y condiciones distintas; no se presentan como un experimento controlado que cuantifique aisladamente cada corrección. La captura de 600 s y el código del capturador se conservan sin alteraciones por esta actualización documental.
