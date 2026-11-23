# Margen de pilas en placa (P4-20/21)

Es el firmware de producción con `CONFIG_INIT_STACKS=y`: Zephyr rellena cada pila con
0xAA al arrancar. Con la placa funcionando, `tests/tools/read_stacks.py` lee la RAM
por SWD (STM32CubeProgrammer en modo HOTPLUG, sin reset) y cuenta los 0xAA que
siguen intactos: es la pila que nunca se ha usado (máximo histórico).

```sh
west build -p always -b nucleo_wl55jc . -d build-stacks -- -DCONF_FILE=confs/prj_nucleo_wl55jc.conf -DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay -DEXTRA_CONF_FILE=tests/board/stacks/stacks.conf
python tests/tools/read_stacks.py --elf build-stacks/zephyr/zephyr.elf --nm RUTA/arm-zephyr-eabi-nm --programmer RUTA/STM32_Programmer_CLI
```

## Resultado (16/11/2026, firmware `bfb1eb9`)

Leído después de recorrer TEST, NORMAL (con una ventana de estadísticas), SIMU,
pulsaciones de B1 y una desconexión del MPU6050 (recuperación del bus y `imu_init`):

| Pila | Tamaño | Máximo usado | Libre |
| --- | ---: | ---: | ---: |
| main | 3072 B | 1420 B | 54 % |
| adquisición | 2048 B | 464 B | 77 % |
| GPS | 1536 B | 552 B | 64 % |
| workqueue del sistema (B1) | 1024 B | 208 B | 80 % |
| interrupciones | 2048 B | 204 B | 90 % |
| idle | 256 B | 64 B | 75 % |

Todas superan el 25 % de margen de la guía. main es la más cargada por `snprintf`
con coma flotante; con 2048 B quedaría en ~31 %, por eso se mantiene en 3072 B
(la RAM total de producción es 15 KB de 64 KB). La pila de interrupciones aparece
como 2112 B porque Zephyr pinta también su zona de guarda.
