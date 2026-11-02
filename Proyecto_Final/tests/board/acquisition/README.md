# Ensayo integrado de adquisicion D4

El perfil adicional de fallos controlados se documenta en
[I2C_ENSAYO.md](I2C_ENSAYO.md). Se compila aparte con D4_I2C_TEST=ON;
su fixture y los envoltorios de observacion no forman parte de produccion.

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

No desconectar el LDR para deducir un fallo: falta el pull-down definitivo.
No se fuerza un bus retenido sin un fixture adecuado. La salida no mide el
coste maximo de cada paso ni permite certificar por si sola el objetivo
nominal de adquisicion menor que 10 ms. Los casos deterministas de frontera,
NO_NEW y dt=0 tras interrupcion se verifican ademas en las pruebas de host.

El [registro del 02/11/2026](results/Ensayo_D4_20261102.md) conserva las
capturas originales, las condiciones de cada fase y su alcance real.

## Medicion temporal sin consola

Para medir la vuelta real sin el formato ni el barrido de pilas del observador:

    west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition-timing -- -DD4_TIMING_ONLY=ON

Este perfil utiliza solo la configuracion/overlay comunes y compila
build_checks.c. timing_main.c abre una sesion estadistica y duerme. No
imprime ni barre pilas; prioridad/pila de adquisicion siguen en 3/2048 B.
Produccion tambien conserva los contadores privados de duracion, sin API nueva.

La marca inicial va despues de acquisition_wait_for_period y la final despues
de incrementar progress. Incluye cierre estadistico, ADC, lectura/calculo IMU,
trabajo ambiental, comprobacion de configuracion y publicacion, con sus esperas
I2C/mutex e interrupciones. Excluye la espera periodica y la actualizacion de
estos contadores. Se guarda una lectura consecutiva del reloj como referencia
de sobrecoste; no se descuenta del maximo.

- Nominal: contextos configurados al comenzar, sin revision ni fallo detectado
  durante la vuelta. Incluye calibracion matematica, trabajos lentos y ventanas.
- Mantenimiento: configuracion/revision o fallo detectado antes/durante la vuelta.
  Se cuentan todas esas vueltas, incluso las breves que esperan un reintento.
- Arranque: tiempo antes del timer, incluida la espera inicial y la configuracion
  IMU con sus sleeps. Se registra separado del trabajo nominal de 20 ms.

El objetivo de la guia es nominal <10 ms dentro del periodo de 20 ms. Se
comparan ciclos enteros, contando >=10 y >=20 ms; el resumen en us redondea
hacia arriba. El maximo obtenido es observado, no un WCET demostrado.

Con el HEX de ese build grabado y verificado, leer desde Proyecto_Final:

    python tests/board/acquisition/leer_tiempos.py --label reposo --serial SERIE_STLINK --gdb RUTA_GDB --programmer RUTA_STM32_PROGRAMMER

GDB se usa sin conexion para resolver direcciones/tipos del ELF; STM32CubeProgrammer
solo solicita upload por SWD HOTPLUG. El script guarda JSON con hashes, contadores
y estado de IMU/luz/ambiente/navegacion/estadisticas. Los datos brutos y salida
de herramientas quedan en .local/d4_timing/. Las estructuras se leen
secuencialmente, sin una instantanea atomica comun ni consumo del status del timer.
La disposicion de timing debe ser de 64 B y el reloj comprobado de 48 MHz.

Mantener los tres sensores conectados y la IMU quieta al cargar. Una toma
nominal de al menos 60 s cubre multiples trabajos lentos y dos cierres de
ventana. Contrastar despues movimiento y caminos de fallo/recuperacion que
se indiquen durante el ensayo. Registrar por separado cualquier interrupcion
introducida deliberadamente y no mezclarla con el criterio nominal.

El [registro temporal del 02/11/2026](results/Tiempos_Adquisicion_D4_20261102.md)
conserva las lecturas y la inyeccion controlada que activa una reinicializacion
completa por el camino existente, con su duracion y periodos omitidos.
