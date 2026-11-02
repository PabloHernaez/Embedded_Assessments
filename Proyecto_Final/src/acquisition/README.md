# Adquisición

Reservamos esta carpeta para el servicio de adquisición: lecturas, cálculo, estadísticas y publicación de snapshots mediante `include/acquisition_api.h`.

Este bloque corresponde a David en D4. La integración del servicio y sus contextos privados sigue pendiente.

## Contrato de navegacion preparado para D4

El contexto privado conserva la referencia inicial q0. El snapshot copia el
NavigationView completo, incluidos q y relative_q. El primero conserva el
marco local con Z arriba; el segundo alimenta ROLL/PITCH/YAW de SR4.
No recapturar q0 por NO_NEW, fallos rutinarios o cambios de modo.
Las firmas de acquisition_api.h se mantienen. La implementacion de este
servicio y su validacion de continuidad siguen pendientes.
