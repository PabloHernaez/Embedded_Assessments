# Drivers de sensores (D2)

Drivers propios, sin la capa `sensor` de Zephyr, para poder explicar cada
registro y cada conversión:

| Archivo | Sensor | Bus | Qué hace |
| --- | --- | --- | --- |
| `imu.c` | MPU6050 | I2C2 0x68 | Reset, configuración con relectura, lectura en ráfaga desde `INT_STATUS`, m/s² y grados/s. |
| `environment.c` | Si7021 | I2C2 0x40 | RH12/T14 sin calentador, F5 sin hold, CRC-8 con `crc8()`, temperatura con E0. |
| `light.c` | LDR KY-018 | ADC1 canal 5 (PB1) | Conversión de 12 bits y porcentaje con las referencias medidas del montaje. |

Los drivers no esperan conversiones, no reintentan ni guardan estado: la
cadencia, los reintentos y la calidad los decide `src/acquisition`.
