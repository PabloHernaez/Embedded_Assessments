"""Comprueba tramas SIMU con el parser de DroneSIMU v0.3, en vivo o desde una captura.

Usa simulator/simu_protocol.py de la version reconstruida de DroneSIMU (verificada
contra el bytecode del ejecutable) y el mismo bucle que su SerialReaderThread:
lineas ASCII sin CRLF; lo que no empieza por $SIMU1 se ignora.

  python tests/tools/check_dronesimu.py --simu-dir RUTA/reconstruido --port COM5 --seconds 90
  python tests/tools/check_dronesimu.py --simu-dir RUTA/reconstruido --file captura.bin
"""
import argparse
import sys
import time


def lines_from_port(port, seconds):
    import serial
    with serial.Serial(port, 115200, timeout=1) as ser:
        t0 = time.monotonic()
        while time.monotonic() - t0 < seconds:
            raw = ser.readline()
            if raw:
                yield time.monotonic() - t0, raw


def lines_from_file(path):
    for raw in open(path, "rb").read().split(b"\n"):
        yield None, raw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--simu-dir", required=True, help="carpeta reconstruido de DroneSIMU")
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--file")
    args = ap.parse_args()
    sys.path.insert(0, args.simu_dir)
    from simulator.simu_protocol import SimuProtocolError, parse_simu_frame

    source = lines_from_port(args.port, args.seconds) if args.port else lines_from_file(args.file)
    total = valid = errors = ignored = seq_gaps = 0
    frames, samples = [], []
    for t, raw in source:
        line = raw.decode("ascii", errors="replace").strip("\r\n")
        if not line:
            continue
        total += 1
        if not line.startswith("$SIMU1,"):
            ignored += 1
            continue
        try:
            f = parse_simu_frame(line)
        except SimuProtocolError as exc:
            errors += 1
            samples.append(str(exc))
            continue
        valid += 1
        if frames and f.seq != (frames[-1][1].seq + 1) % 65536:
            seq_gaps += 1
        frames.append((t, f))

    print(f"lineas={total} validas={valid} errores={errors} ignoradas={ignored} saltos_seq={seq_gaps}")
    for e in samples[:3]:
        print("error:", e)
    if len(frames) > 1:
        d = [b.t_ms - a.t_ms for (_, a), (_, b) in zip(frames, frames[1:])]
        print(f"intervalo TMS min/medio/max = {min(d)}/{sum(d) / len(d):.2f}/{max(d)} ms")
        if frames[0][0] is not None:
            span = frames[-1][0] - frames[0][0]
            print(f"{len(frames)} tramas en {span:.1f} s de PC -> {(len(frames) - 1) / span:.2f} tramas/s")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
