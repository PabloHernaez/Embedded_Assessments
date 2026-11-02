# Lectura bruta del LDR

Ensayo minimo: driver real light.c, una conversion cada 500 ms y salida serie
por LPUART1 a 115200 8N1. Comparte canal, referencia, resolucion y tiempo de
adquisicion ADC con produccion. La consola se habilita solo en este ensayo.

Desde Proyecto_Final, con el entorno de Zephyr activo:

    west build -p auto -b nucleo_wl55jc tests/board/light -d build-light

La imagen queda en build-light/zephyr/zephyr.hex. Su main muestra raw en
cuentas ADC de 12 bits (0..4095) o un error; no convierte a lux/porcentaje.

Antes de cada toma, establecer la condicion fisica y mantenerla estable.
Con pyserial disponible y el monitor serie cerrado:

    python tests/board/light/capturar_ldr.py --port COM5 --label oscuridad --seconds 10
    python tests/board/light/capturar_ldr.py --port COM5 --label iluminado --seconds 10

El PC guarda las lineas en results/ y muestra cantidad, minimo, maximo y
media de la toma para revisar su estabilidad. No modifica constantes.
El firmware hace una sola conversion por vuelta, sin promedio ni filtro.

La guia fija pull-down de 47 kohm entre PB1 y GND junto al MCU. Si se mide
sin esa resistencia, registrar explicitamente ese montaje: las referencias
son provisionales y deben repetirse al añadirla. El diagnostico de desconexion
queda pendiente. Registrar tambien alimentacion, condicion de oscuridad,
fuente luminosa, potencia y distancia de la referencia iluminada.

La formula existente admite que raw aumente o disminuya con la luz. Las dos
referencias deben ser medidas y diferentes; el codigo 4095 no se presupone
como referencia iluminada. No inferir una averia electrica de un extremo
legitimo sin medir su margen.

La [sesion del 02/11/2026](results/Calibracion_LDR_20261102.md) conserva las
capturas, las condiciones fisicas y las referencias provisionales medidas.
