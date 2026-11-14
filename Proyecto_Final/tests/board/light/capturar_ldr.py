"""Guardar una toma breve del LDR; resumir los codigos sin cambiar el firmware."""

import argparse
from datetime import datetime
from pathlib import Path
import re
import time

import serial


def main():
    """Entradas: CLI/UART. Retorno: 0/1. Efectos: guarda captura y muestra resumen."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--label", choices=("respuesta", "oscuridad", "iluminado"), required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds debe ser positivo")

    folder = Path(__file__).resolve().parent / "results"
    folder.mkdir(exist_ok=True)
    stamp = datetime.now().astimezone()
    path = folder / f"LDR_{args.label}_{stamp:%Y%m%d_%H%M%S_%f}.txt"
    samples = []
    errors = 0

    with serial.Serial(args.port, 115200, timeout=1) as connection:
        connection.reset_input_buffer()
        with path.open("x", encoding="utf-8") as output:
            output.write(f"# {stamp.isoformat()} label={args.label} port={args.port}\n")
            print(f"Captura: {path}", flush=True)
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                line = connection.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                output.write(line + "\n")
                output.flush()
                print(line, flush=True)
                match = re.fullmatch(r"t_ms=\d+ raw=(\d+)", line)
                if match:
                    samples.append(int(match.group(1)))
                if "err=" in line or (line.startswith("light_init=") and line != "light_init=0"):
                    errors += 1

    if not samples or errors:
        print(f"Toma no utilizable: muestras={len(samples)} errores={errors}")
        return 1
    print(f"RESUMEN n={len(samples)} min={min(samples)} max={max(samples)} "
          f"media={sum(samples) / len(samples):.2f}")
    print("Resumen de la toma; no se ha escrito ninguna referencia de calibracion.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
