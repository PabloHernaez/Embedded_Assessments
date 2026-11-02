# Ensayo I2C de cierre D4

Perfil exclusivo de prueba. Mantiene el servicio/driver reales y su unico hilo
dueno de I2C. Los puntos de observacion se enlazan con GNU --wrap en esta imagen;
no se añaden flags de sensor ficticio ni APIs al firmware de produccion.
Main solo controla un GPIO externo al bus y arma peticiones de ensayo.

    west build -p auto -b nucleo_wl55jc tests/board/acquisition -d build-acquisition-i2c -- -DD4_I2C_TEST=ON

Usa configuracion comun, sin consola ni barrido, con build_checks.c. D8/PC2
se declara en i2c_fault.overlay como GPIO_OPEN_DRAIN y se prepara en alto:
alto significa liberar la linea. Se comprueban sus registros antes de añadir
el jumper. Con alimentacion desconectada, el montaje temporal une D8 con SDA
(D14/PA11), o con SCL (D15/PA12) para el caso de timeout, manteniendo MPU6050
y Si7021 conectados. Se registra la linea elegida para cada toma. No usar otro pin ni un
firmware que conduzca D8 en push-pull durante este ensayo.

El usuario confirma D8 libre, un jumper disponible y ausencia de resistencia
serie. La salida de ensayo solo puede hundir corriente o quedar liberada;
el bus mantiene sus pull-ups existentes. La conexion se retira al finalizar.

## Casos

command se escribe por SWD en el simbolo privado i2c_test.command, resuelto
en el ELF de esta imagen. No reutilizar direcciones numericas de otro build.

| command | Accion | Evidencia buscada |
| --- | --- | --- |
| 1 | Escribir PWR_MGMT_1=0x49 (SLEEP) en el MPU real | NO_NEW sin error de lectura, revision periodica detecta diferencia y reconfigura. |
| 2 | Escribir PWR_MGMT_1=0x80 (RESET) en el MPU real | Detectar valores de fabrica y retomar conservando sesgo/q0. |
| 3 | Retener SDA con D8 durante 60 ms | Error/timeout fisico breve y retoma tras la revision espaciada por bus retenido. |
| 4 | Retener SDA durante 5 s | Timeout real, recuperaciones espaciadas, resto del hilo sigue avanzando y retoma al liberar. |
| 5 | Devolver tres -EIO consecutivos sin transferir esas tres lecturas | Backend simulado en placa: tres fallos a 20 ms, revision siguiente sin demora de 1 s y lectura nueva posterior. |
| 6 | Enviar una sola orden 0xFE al Si7021 | Diagnostico manual de un sensor que dejo de responder; no es recuperacion automatica de produccion. |
| 7 | Ejecutar un bus clear una sola vez con linea liberada | Diagnostico manual tras bloqueo previo; tampoco forma parte de la politica automatica. |
| 8 | Direccionar 0x40 sin enviar datos | Distinguir NACK de direccion de rechazo de un comando concreto. |
| 9 | Leer FC/C9 y registrar sus bytes/CRC brutos | Identificacion puntual orientativa, sin autenticar el modulo; exclusivamente en diagnostico. |

Las escrituras de PWR se ejecutan desde el envoltorio de imu_read en el hilo
D4. Esa invocacion entrega NO_NEW tras la escritura; las siguientes usan
imu_read real, sin ocultar errores ni ausencia de datos del MPU.
Los tres -EIO del caso 5 son simulados; no acreditan un glitch electrico.
Los casos 3/4 retienen SDA fisicamente, por lo que aplican la revision a 1 s
del camino de bloqueo, aunque el pulso del caso 3 sea breve.
Con el jumper en SCL, esos mismos comandos retienen SCL durante 60 ms/5 s;
el campo detail conserva el numero de comando, no identifica la linea.
La conexion fisica declarada en el registro de ensayo es imprescindible.

## Captura

Cada caso abre una traza de 15 s y conserva hasta 96 eventos en RAM; NO_NEW
consecutivos se cuentan, pero solo se imprime su cambio en la traza. Los
errores se registran uno a uno. Main e hilo protegen solamente la copia breve
del evento. El campo dropped debe permanecer en cero.

    python tests/board/acquisition/leer_tiempos.py --i2c --label CASO --serial SERIE_STLINK --gdb RUTA_GDB --programmer RUTA_STM32_PROGRAMMER

El lector solo solicita upload SWD HOTPLUG. Guarda tiempos globales, resultados,
traza, estado del servicio, sesgo y q0. Los eventos incluyen ciclo del hilo,
fecha en ms y duracion en ciclos a 48 MHz. En cada ciclo, tras un retorno
negativo de lectura/revision, comprobar que no se inician ENV ni otra revision
opcional; la recuperacion GPIO pertinente si esta permitida se identifica aparte.
Comprobar intervalos entre revisiones/recuperaciones, ausencia de init repetido
y retoma con sesgo/q0 conservados. Las estructuras se leen secuencialmente.
ENV_INIT registra tambien las configuraciones fallidas que no llegan a F5.

Para diagnosticar Si7021 se observa ademas i2c_stm32_transaction sin cambiar
su resultado. TRANSFER conserva cada mensaje de direccion 0x40 y cualquier
mensaje fallido del bus: direccion, flags, longitud, primer byte utilizable
y banderas STM32 NACK/ARLO/ERR. El lector decodifica estos campos; nunca lee
un buffer de recepcion fallida. Esta observacion usa la cabecera privada
i2c_stm32.h de la version instalada, exclusivamente en el perfil de ensayo.
No modifica Zephyr ni añade esa dependencia a produccion.

Resultados y defecto ambiental reproducido en
[el registro de I2C](results/I2C_D4_20261102.md).

## Referencias del montaje y registros

- [ST UM2592, tabla de conectores](https://www.st.com/resource/en/user_manual/dm00622917-stm32wl-nucleo64-board-mb1389-stmicroelectronics.pdf): D8=PC2 y D14=PA11/SDA.
- [TDK MPU-6000/6050, mapa de registros](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf): PWR_MGMT_1, SLEEP y DEVICE_RESET.
- El overlay efectivo y el controlador GPIO/I2C de Zephyr 4.4.2 instalado
  verifican drenador abierto y restauracion de pinctrl tras recuperacion.
