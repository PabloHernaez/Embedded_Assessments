# Formatos de salida

Este bloque corresponde a Pablo en P2. Los contratos publicos estan en
include/simu_frame.h e include/human_report.h; los formateadores puros y
su validacion de trama siguen pendientes.

## Entrada de orientacion para SR4

- acquisition->nav.relative_q: cuaternion Hamilton sensor actual -> ejes
  iniciales del sensor. Empieza en identidad y es la entrada de ROLL/PITCH/YAW.
- acquisition->nav.q: sensor -> marco local con Z arriba; se usa en navegacion.
- Velocidad y posicion siguen expresadas en el marco local alineado con gravedad.
- nav.valid conserva el significado de actualizacion numericamente aceptada;
  no garantiza precision fisica ni ausencia de deriva.

Adquisicion conserva q0 privado y prepara relative_q; los formateadores no
recalibran ni conservan estado. P2 convierte el cuaternion relativo a Euler
ZYX en grados y aplica las reglas de valores no finitos y formato fijo.
La trama sigue siendo de 221 bytes. La integracion con el getter de D4 y la
validacion de Euler/trama real quedan pendientes.

El [registro D3](../../docs/D3_ESTIMACION.md) contiene las capturas y limites.
