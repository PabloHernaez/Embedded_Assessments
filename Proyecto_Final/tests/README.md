# Ensayos

Mantenemos aquí las pruebas de los bloques del firmware, separadas del main
de producción. Por ahora tenemos el ensayo de placa en `board/sensors` para
MPU6050, calibración inicial del giroscopio y Si7021.

El ensayo tiene su propio CMake, configuración y main. Comparte los drivers
y la calibración con el proyecto, aplica primero la configuración común y
habilita después la consola de diagnóstico en LPUART1. En producción
conservamos las restricciones de `src/build_checks.c`.

Compilamos con `West Build (sensores)`, programamos con `Flash (Sensores)` y
leemos la salida a 115200 8N1. Los resultados generados quedan en
`build-sensors`, fuera de Git.

Seguimos pendientes de la captura física, las pruebas de error y la validación
del LDR. Las pruebas de GPS, formatos y aplicación se incorporarán con sus
bloques correspondientes. Los dobles de prueba quedan dentro de `tests/`.
