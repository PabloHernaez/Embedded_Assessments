# Caracterizacion provisional del LDR: 02/11/2026

Montaje declarado por el usuario: KY-018 alimentado a 3,3 V, salida a PB1,
sin el pull-down adicional de 47 kohm que pide la guia. Las referencias de
esta sesion solo corresponden a ese montaje; deben repetirse al añadirlo.
El diagnostico de desconexion sigue pendiente.

Se utiliza el driver real light.c y el overlay comun: ADC1, canal 5,
resolucion de 12 bits, ganancia 1 y ADC_ACQ_TIME_MAX. El lector hace una
conversion cada 500 ms. Los resumenes de las tomas se calculan en el PC;
no se añade ningun promedio o filtro al firmware.

| Toma | Condicion | Muestras | Minimo | Maximo | Media |
| --- | --- | --- | --- | --- | --- |
| Respuesta inicial | Condicion luminosa sin fijar; solo comprueba la lectura | 9 | 2974 | 2991 | 2982,89 |
| Oscuridad | Usuario confirma la parte sensible completamente tapada | 21 | 4059 | 4066 | 4061,52 |
| Iluminado | Linterna del movil a maxima potencia, apuntando al LDR a 5 cm | 21 | 109 | 147 | 128,14 |

Capturas originales:

- [Respuesta](LDR_respuesta_20261102_222519_029243.txt).
- [Oscuridad](LDR_oscuridad_20261102_222752_381860.txt).
- [Iluminado](LDR_iluminado_20261102_222858_267326.txt).

No hubo errores de lectura en estas tomas. Redondeando cada media al entero
mas cercano, las referencias provisionales incorporadas en
src/acquisition/acquisition_light.c son raw_dark=4062 y raw_bright=128.

La respuesta disminuye al aumentar la luz. La conversion lineal existente
equivale a p=100*(4062-raw)/3934: raw=4062 da 0 %, raw=128 da 100 % y
raw=2095 da 50 %. Es una escala relativa entre condiciones, no lux ni
una medida lineal de iluminancia fisica.

Las muestras de una misma condicion fluctuan. Algunas quedan fuera del
intervalo definido por las medias redondeadas: la conversion existente las
recorta a 0/100 % y activa clipped, manteniendo la lectura valida si el ADC
no falla. clipped por si solo no diagnostica una averia.

La oscuridad queda cerca del rail alto; no se ha demostrado margen para
distinguirla de un fallo electrico mediante un umbral de rail. No se añade
ese diagnostico. Faltan la calibracion del montaje definitivo con 47 kohm
y los ensayos integrados de adquisicion/estadisticas en placa.

Verificacion: el lector independiente compilo, se grabo y se verifico con
STM32CubeProgrammer; COM5 recibio las tres tomas sin errores. La imagen
principal tambien compilo correctamente tras incorporar las referencias
(FLASH 35876 B, RAM 8768 B). La placa conserva por ahora el lector temporal;
esa compilacion de produccion no equivale al ensayo integrado de D4.
