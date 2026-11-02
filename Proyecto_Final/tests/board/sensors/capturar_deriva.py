"""Captura D3-14: un arranque completo y diez minutos de navegacion."""

import argparse
from datetime import datetime
from pathlib import Path
import re
import sys
import time

import serial


def capture(connection, output, seconds):
    """Guardar UART; terminar tras el reporte completo que alcanza la duracion."""
    boot_seen = False
    calibration_seen = False
    start_ms = None
    previous_ms = None
    finish_report = False
    next_progress_s = 0
    started = time.monotonic()
    last_rx = started

    while True:
        now = time.monotonic()
        if not boot_seen and now - started > 90:
            raise RuntimeError("No llego un arranque en 90 s. Repite y pulsa RESET.")
        if now - started > seconds + 180:
            raise RuntimeError("Tiempo total agotado; la captura quedo incompleta.")
        if boot_seen and now - last_rx > 15:
            raise RuntimeError("Sin datos serie durante 15 s; captura incompleta.")

        raw = connection.readline()
        if not raw:
            continue
        last_rx = time.monotonic()
        line = raw.decode("utf-8", errors="replace").strip()

        if line == "Ensayo MPU6050 + Si7021":
            if boot_seen:
                raise RuntimeError("Se detecto otro RESET; captura incompleta.")
            boot_seen = True
            print("Arranque detectado. Guardando calibracion...", flush=True)

        # Ignorar la sesion anterior hasta que se pulse RESET.
        if not boot_seen:
            continue
        output.write(raw)
        output.flush()

        if line.startswith("ACC inicial media:"):
            calibration_seen = True
            print(line, flush=True)

        match = re.match(r"^t=(\d+)\s*\|", line)
        if match and calibration_seen:
            sample_ms = int(match.group(1))
            if previous_ms is not None and sample_ms < previous_ms:
                raise RuntimeError("El tiempo retrocedio; captura incompleta.")
            previous_ms = sample_ms
            if start_ms is None:
                start_ms = sample_ms
                print("Navegacion iniciada. Comienza la cuenta.", flush=True)

            elapsed_s = (sample_ms - start_ms) / 1000.0
            if elapsed_s >= next_progress_s:
                print(f"Navegacion: {elapsed_s:.0f}/{seconds} s", flush=True)
                next_progress_s = (int(elapsed_s) // 30 + 1) * 30
            finish_report = elapsed_s >= seconds

        # Conservar q, v y p del ultimo reporte antes de cerrar el archivo.
        if finish_report and line.startswith("v=") and "| p=" in line:
            return (previous_ms - start_ms) / 1000.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument(
        "--output-dir", type=Path, default=Path(__file__).resolve().parent
    )
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds debe ser positivo")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    path = args.output_dir / f"Ensayo_deriva_{args.seconds}s_{stamp}.txt"
    try:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        with serial.Serial(args.port, 115200, timeout=1) as connection:
            with path.open("xb") as output:
                print(f"Archivo: {path}", flush=True)
                print("Pulsa RESET en la Nucleo y manten el MPU inmovil.", flush=True)
                elapsed_s = capture(connection, output, args.seconds)
        print(f"Captura completa: {elapsed_s:.1f} s de navegacion.", flush=True)
        print(f"Archivo guardado: {path}", flush=True)
        return 0
    except KeyboardInterrupt:
        print("\nCaptura interrumpida; conserva el archivo parcial.", file=sys.stderr)
    except (OSError, RuntimeError, serial.SerialException) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        print("Si COM esta ocupado, cierra antes Serial Monitor.", file=sys.stderr)
    if path.exists():
        print(f"Archivo parcial: {path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
