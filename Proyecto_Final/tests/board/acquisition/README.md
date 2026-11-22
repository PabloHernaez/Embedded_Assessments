# Ensayo integrado de adquisicion D4

Esta aplicacion enlaza las cuatro fuentes reales de acquisition, sus drivers
y sus algoritmos. Solo sustituye el main de produccion por un observador de
la API publica. No hace lecturas directas ni duplica el procesamiento.
El hilo real conserva prioridad 3, pila solicitada de 2048 B y timer de 20 ms.

## Construccion y captura

Desde Proyecto_Final, con el entorno Zephyr activo:

    west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition

La imagen queda en build-acquisition/zephyr/zephyr.hex. El perfil de ensayo
habilita consola LPUART1, 115200 8N1, formato decimal y marcas para consultar
el espacio de pila sin usar. El main de ensayo solicita 3072 B. El perfil
de produccion y build_checks.c conservan sus restricciones de consola.

Con pyserial disponible y el monitor serie cerrado:

    python tests/board/acquisition/capturar_d4.py --label arranque_reposo --seconds 40
    python tests/board/acquisition/capturar_d4.py --label movimiento --seconds 12
    python tests/board/acquisition/capturar_d4.py --label fin_sesion --command e --seconds 4
    python tests/board/acquisition/capturar_d4.py --label nueva_sesion --command b --seconds 33

El puerto predeterminado es COM5. El script guarda las lineas originales en
results/ con fecha y zona horaria. Contar reportes recibidos no declara el
ensayo aprobado: hay que revisar datos, fechas, condiciones y limitaciones.

## Lectura de los reportes

- D4: uptime, progress y su incremento dp durante dt_ms; calidad/edad IMU,
  calibracion, movimiento, saturacion y validez de navegacion.
- IMU: aceleracion en m/s2 con gravedad y giro en grados/s, corregido de
  sesgo al terminar la calibracion inicial.
- NAV: q sensor -> local, rel respecto a la postura inicial, velocidad m/s
  y posicion m. Se observa la deriva del estimador existente, sin filtros
  ni correcciones nuevas; moving=false no detiene su integracion.
- SLOW: luz relativa/recorte y par temperatura/humedad, con calidad y edad.
- STACK: resultado de la consulta Zephyr y bytes de pila aun sin usar.
  Es el margen observado durante este perfil y estas condiciones.
- WINDOW y STAT: ultimo resultado completo, sus limites y las cinco metricas.
  Cada ventana se imprime una vez. n=0 se muestra como N/D.

El main consulta snapshots cada 100 ms y reporta aproximadamente una vez
por segundo. Usa una copia antes del formato, fuera del mutex del servicio.
La consulta repetida no añade muestras a las estadisticas.
Envia b para reiniciar/abrir una sesion y e para cerrarla. Cada orden imprime
SESSION y su uptime; no reinicia los sensores ni la navegacion.

## Fases guiadas

1. Arranque en reposo durante unos 40 s, con los tres sensores conectados.
   La sesion inicial comienza en main antes de terminar la calibracion IMU.
   Esperar paso de cal=1 a cal=0 y nav_ok=1; giro corregido proximo a cero.
   Comprobar fechas renovadas, progress cercano a 50 vueltas/s y ventana
   end_ms-begin_ms=30000. Los conteos IMU excluyen la calibracion inicial;
   aceleracion y giro deben tener igual conteo, igual que temperatura y HR.
   Luz y ambiente aportan aproximadamente 15 muestras por ventana completa
   de funcionamiento estable; el arranque y la fase de cada lectura influyen.
2. Mover/girar suavemente la IMU unos segundos y volver a dejarla quieta.
   Observar moving, gyro, q/rel y continuidad de v/p. No usar esta salida de
   1 Hz para acreditar los tiempos exactos de confirmacion de 300 ms/2 s.
3. Tapar el LDR y despues repetir la linterna al maximo a 5 cm, en tomas
   separadas. Luz debe aproximarse a 0/100 % mientras IMU/ambiente siguen.
   Referencias provisionales: 4062/128, a 3,3 V sin pull-down de 47 kohm.
4. Cerrar y abrir sesiones. Comprobar que ready se oculta al cerrar y que
   una nueva ventana completa necesita otros 30 s desde begin.
5. Fallos/retoma: preparar primero un cambio de conexion controlado, segun
   el montaje disponible. Revisar invalidacion, conservacion de fecha/datos,
   avance del hilo y recuperacion sin repetir calibracion acreditada.
   La IMU se reinicia tras 3 errores seguidos o 500 ms sin muestras nuevas,
   como mucho una vez por segundo; el Si7021 se revalida tras cualquier
   error del bus compartido.

No desconectar el LDR para deducir un fallo: falta el pull-down definitivo.
No se fuerza un bus retenido sin un fixture adecuado. La salida no mide el
coste maximo de cada paso ni permite certificar por si sola el objetivo
nominal de adquisicion menor que 10 ms. Los casos deterministas de frontera,
NO_NEW y dt=0 tras interrupcion se verifican ademas en las pruebas de host.

El [registro del 02/11/2026](results/Ensayo_D4_20261102.md) conserva las
capturas originales, las condiciones de cada fase y su alcance real.

## Evidencia anterior a la simplificacion

Los registros de results/ del 02 y 03/11/2026 (tiempos por SWD, fallos I2C
con fixture y diagnostico del Si7021) se obtuvieron con la version de
adquisicion de bc0797f, que tenia revision periodica de configuracion,
contadores de tiempo y perfiles D4_TIMING_ONLY/D4_I2C_TEST. Esos perfiles
se retiraron al simplificar el servicio; las medidas se conservan como
historico y los casos de fallo deben repetirse en P4 con la version actual.
