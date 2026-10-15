# Hardware y compilación

## Montaje confirmado

El KY-018 está alimentado a 3,3 V y su salida `S` está conectada a A0, que
corresponde a PB1/ADC1_IN5. Su ADC baja cuando aumenta la luz, por lo que la
aplicación invierte la escala completa de 12 bits.

El RGB es de ánodo común. La pata común larga está conectada a 3V3. Rojo,
verde y azul están conectados respectivamente a D4, D5 y D6, cada uno mediante
su propia resistencia de 330 ohm.

## Cálculo

```text
adc_average = suma de ocho conversiones / 8
brightness_value = 4095 - adc_average
percent = brightness_value * 100 / 4095
```

El porcentaje mostrado es entero. La clasificación compara la proporción
original para conservar exactamente los límites de 33 % y 66 %.

## Resultados físicos

- Build limpio y flash correctos.
- Lectura inmediata y periodo de 2 segundos confirmados.
- Más luz reduce el ADC y aumenta el porcentaje.
- Rojo, amarillo, verde, azul y apagado confirmados.
- Polaridad de ánodo común confirmada.