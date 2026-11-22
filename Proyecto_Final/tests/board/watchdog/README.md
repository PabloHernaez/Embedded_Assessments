# Ensayo del watchdog (P4-14)

Compila el firmware de producción sin cambios y añade `src/blocker.c`, que a los
10 s del arranque bloquea un hilo. La salida es la de producción (informes TEST
por LPUART1 cada 2 s): un reinicio se ve porque TMS vuelve a empezar, y el
informe siguiente muestra la causa (`RESET=`, bits de `hwinfo`: 0x1 pin,
0x2 software, 0x10 watchdog).

```sh
west build -p always -b nucleo_wl55jc tests/board/watchdog -d build-wdt -- -DWDT_CASE=acquisition
```

`WDT_CASE`: `acquisition`, `gps`, `main` (suspende ese hilo) o `irq` (bloquea las
interrupciones en un bucle). Volver a grabar producción al terminar.

## Resultado en placa (16/11/2026, firmware `bfb1eb9`)

Captura de 35 s por caso, tres ciclos de arranque → bloqueo → reinicio en cada uno.

| Caso | Último informe antes del reinicio | Causa tras el reinicio | Quién reinicia |
| --- | --- | --- | --- |
| `acquisition` | TMS = 10003 | `0x00000003` (pin + software) | canal de adquisición de `task_wdt` (1 s) |
| `gps` | TMS = 12003 | `0x00000003` | canal GPS de `task_wdt` (3 s) |
| `main` | TMS = 8003 (main deja de informar) | `0x00000003` | canal de main de `task_wdt` (1 s) |
| `irq` | TMS = 8003 | `0x00000011` (pin + watchdog) | IWDG: sin interrupciones `task_wdt` no puede alimentarlo (2 s) |

En los tres primeros casos el reinicio lo hace `task_wdt` con `sys_reboot`; en el
último, el IWDG de respaldo, ~1,1 s más tarde que el caso `main`. Con producción
grabada de nuevo no hay reinicios.
