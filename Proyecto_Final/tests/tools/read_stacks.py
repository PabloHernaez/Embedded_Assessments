"""Margen de pila de cada hilo, leido por SWD con el firmware en marcha.

Requiere un build con CONFIG_INIT_STACKS=y (tests/board/stacks/stacks.conf): Zephyr
rellena cada pila con 0xAA al arrancar y las pilas crecen hacia abajo, asi que los
0xAA que siguen intactos al principio de la region son la pila que nunca se uso.
Lee la RAM con STM32CubeProgrammer en modo HOTPLUG (sin reset).

  python tests/tools/read_stacks.py --elf build-stacks/zephyr/zephyr.elf \
      --nm RUTA/arm-zephyr-eabi-nm --programmer RUTA/STM32_Programmer_CLI
"""
import argparse
import os
import subprocess
import tempfile

STACKS = {  # simbolo -> (nombre, tamano configurado)
    "_k_thread_stack_acquisition_tid": "adquisicion",
    "z_main_stack": "main",
    "_k_thread_stack_gps_tid": "gps",
    "sys_work_q_stack": "workqueue del sistema",
    "z_interrupt_stacks": "interrupciones",
    "z_idle_stacks": "idle",
}
PAINT = 0xAA
MAX_GUARD = 128  # guarda MPU al principio del objeto de pila


def symbols(nm, elf):
    out = subprocess.run([nm, "-S", elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] in STACKS:
            found[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    return found


def read_ram(programmer, address, size):
    fd, path = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    try:
        subprocess.run([programmer, "-c", "port=SWD", "mode=HOTPLUG", "-u", hex(address), str(size), path],
                       capture_output=True, text=True, check=True)
        return open(path, "rb").read()
    finally:
        os.remove(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--programmer", required=True)
    args = ap.parse_args()

    print(f"{'pila':24s} {'tamano':>7s} {'usado':>7s} {'libre':>7s} {'margen':>7s}")
    for sym, (address, total) in symbols(args.nm, args.elf).items():
        data = read_ram(args.programmer, address, total)
        start = next((i for i in range(min(MAX_GUARD, len(data))) if data[i] == PAINT), None)
        if start is None:
            print(f"{STACKS[sym]:24s} {'sin pintar':>7s}")
            continue
        size = total - start  # sin la guarda
        free = 0
        while start + free < len(data) and data[start + free] == PAINT:
            free += 1
        used = size - free
        print(f"{STACKS[sym]:24s} {size:7d} {used:7d} {free:7d} {100 * free / size:6.0f} %")


if __name__ == "__main__":
    main()
