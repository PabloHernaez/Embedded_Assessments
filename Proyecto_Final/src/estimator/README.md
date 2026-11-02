# Estimacion y movimiento

La implementacion C pura de D3 esta en ../algorithms: calibracion gyro,
movimiento y estimator.c/.h. El ensayo tests/board/sensors ya consume la
integracion de orientacion, velocidad y posicion.

NavigationView.q conserva el marco local alineado con gravedad.
NavigationView.relative_q expresa orientacion respecto a los ejes iniciales
del sensor y alimenta SR4. La referencia inicial se conserva en el productor.
El servicio de adquisicion de D4 y la salida de P2 todavia deben integrarse.

Vease el [registro D3](../../docs/D3_ESTIMACION.md) para calibracion,
contrato, capturas de deriva y pendientes. D3 no se declara completamente cerrado.
