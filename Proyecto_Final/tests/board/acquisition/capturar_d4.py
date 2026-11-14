"""Guardar una fase del ensayo integrado D4 desde la consola de la placa."""

import argparse
from datetime import datetime
from pathlib import Path
import re
import time

import serial


def main():
    """Guarda la UART. Entradas: CLI/serie. Retorno: 0/1. Modifica: archivo y consola;
    con --command tambien envia b/e para abrir/cerrar la sesion estadistica.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--label", required=True)
    parser.add_argument("--command", choices=("b", "e"))
    args = parser.parse_args()
    if args.seconds <= 0 or not re.fullmatch(r"[a-z0-9_]+", args.label):
        parser.error("Usar segundos positivos y label con letras minusculas, digitos o _")

    folder = Path(__file__).resolve().parent / "results"
    folder.mkdir(exist_ok=True)
    stamp = datetime.now().astimezone()
    path = folder / f"D4_{args.label}_{stamp:%Y%m%d_%H%M%S_%f}.txt"
    reports = 0
    with serial.Serial(args.port, 115200, timeout=1) as connection:
        connection.reset_input_buffer()
        with path.open("x", encoding="utf-8") as output:
            output.write(f"# {stamp.isoformat()} label={args.label} port={args.port} "
                         f"command={args.command or '-'}\n")
            print(f"Captura: {path}", flush=True)
            if args.command:
                connection.write(args.command.encode("ascii"))
                connection.flush()
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                line = connection.readline().decode("utf-8", errors="replace").strip()
                if line:
                    output.write(line + "\n")
                    output.flush()
                    print(line, flush=True)
                    reports += line.startswith("D4 t_ms=")
    print(f"Reportes D4 recibidos: {reports}. Revisar la captura; no es un veredicto automatico.")
    return 0 if reports else 1


if __name__ == "__main__":
    raise SystemExit(main())
