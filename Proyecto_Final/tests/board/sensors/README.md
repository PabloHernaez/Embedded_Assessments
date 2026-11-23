# Capturas del ensayo de sensores (D2/D3)

El firmware de este ensayo reimplementaba en su propio `main` la calibracion,
el movimiento y la navegacion. Se retiro cuando el servicio real de
adquisicion paso a observarse con `tests/board/acquisition`; su ultima
version esta en el historial (`git show 0039ec8:Proyecto_Final/tests/board/sensors/src/main.c`).

Se conservan las capturas de `results/`, que respaldan el
[registro D3](../../../docs/D3_ESTIMACION.md), y `capturar_deriva.py`, el
script de PC con el que se tomaron.
