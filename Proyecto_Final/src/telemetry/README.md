# Formatos de salida (P2)

Funciones puras: reciben copias de los snapshots y escriben texto; no tocan
hardware, reloj ni estado.

| Archivo | Qué genera |
| --- | --- |
| `simu_frame.c` | Trama `$SIMU1` de 221 bytes. Un solo `snprintf` cuya cadena de formato sigue la tabla 3 del enunciado campo a campo; cada valor se redondea y se acota antes para que el ancho sea exacto. Checksum XOR entre `$` y `*`. |
| `human_report.c` | Informe de TEST (cada 2 s) y NORMAL (cada 5 s, con estadísticas de 30 s). `N/D` cuando nunca hubo dato. |
| `attitude.h` | ROLL/PITCH/YAW (ZYX) desde `nav.q`, sin ramas: recorte del seno antes de `asinf`. |

Reglas de la trama (§2.4.9): sin fix, GF = 0 y SAT = 00 y se conserva la
última posición (ceros si nunca la hubo); NaN/Inf se escriben como cero;
los valores fuera de rango se recortan al máximo del campo. La validez la
indican STATE y FLAGS, que decide `src/app/tracker_status.c`.

`printf` con coma flotante viene de picolibc (`CONFIG_REQUIRES_FLOAT_PRINTF`).
`tests/host/test_simu_frame.c` comprueba el vector oficial (XOR 3F) y los
límites; también se ha ejecutado en `qemu_cortex_m3` con picolibc.
