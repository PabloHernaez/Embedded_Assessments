# Brightness monitoring con Zephyr

Aplicación para la NUCLEO-WL55JC1 que mide la iluminación con un KY-018 y
muestra el nivel mediante un RGB externo.

## Comportamiento

- Promedia ocho conversiones ADC.
- Convierte el resultado a un porcentaje entero de 0 a 100.
- Menos del 33 % enciende rojo.
- Del 33 % al 66 %, incluidos, enciende amarillo.
- Más del 66 % enciende verde.
- Produce una primera lectura inmediata y después una cada 2 segundos.
- Permite activar y pausar la adquisición desde el controlador de estados.
- Utiliza únicamente gestión de energía system-managed de Zephyr.

## Cableado

| Función | Conexión |
|---|---|
| KY-018 `S` | A0 / PB1 / ADC1_IN5 |
| KY-018 `+` | 3V3 |
| KY-018 `-` | GND |
| Común del RGB | 3V3, ánodo común |
| Rojo | D4 / PB5 mediante 330 ohm |
| Verde | D5 / PB8 mediante 330 ohm |
| Azul | D6 / PB10 mediante 330 ohm |

Cada canal del RGB necesita su propia resistencia. No conectar un canal
directamente al GPIO.

## Estructura

- `brightness.c`: ADC, promedio, inversión, porcentaje y clasificación.
- `rgb_led.c`: control lógico de los cinco colores.
- `sensor_thread.c`: periodo, pausa y entrega de resultados.
- `main.c`: consumidor NORMAL temporal.

La polaridad del RGB está declarada como `GPIO_ACTIVE_LOW` en Devicetree.

El terminal serie utiliza 115200 8N1.

## Integración

1. Inicializar RGB y servicio del sensor.
2. Llamar a `sensor_start()` al entrar en NORMAL.
3. Consumir solo resultados cuyo `status` sea cero.
4. Llamar a `sensor_pause()` antes de entrar en BLUE u OFF.
5. Fijar azul o apagado desde el controlador de estados.

