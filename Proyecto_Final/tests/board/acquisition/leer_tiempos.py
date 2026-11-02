"""Guardar tiempos y estado D4 mediante lecturas SWD, sin reset ni escritura RAM."""

import argparse
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess


def main():
    """Lee diagnosticos. Entradas: CLI, ELF y placa. Retorno: 0 o error de herramienta.
    Modifica: crea capturas locales y JSON; solo solicita upload SWD HOTPLUG.
    Las lecturas de estructuras son secuenciales, sin una instantanea atomica comun.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--serial", required=True, help="Numero de serie ST-LINK")
    parser.add_argument("--gdb", type=Path, required=True)
    parser.add_argument("--programmer", type=Path, required=True)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--i2c", action="store_true", help="Leer tambien traza/referencias del perfil D4_I2C_TEST")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9_]+", args.label):
        parser.error("label admite letras minusculas, digitos y _")

    root = Path(__file__).resolve().parents[3]
    selected_build = args.build or Path("build-acquisition-i2c" if args.i2c else "build-acquisition-timing")
    build = selected_build if selected_build.is_absolute() else root / selected_build
    elf = build / "zephyr" / "zephyr.elf"
    hex_path = build / "zephyr" / "zephyr.hex"
    expressions = {
        "timing": "&timing", "timing_size": "sizeof(timing)",
        "published": "&published", "published_size": "sizeof(published)",
        "imu_valid": "&published.imu.quality.valid",
        "calibration_pending": "&published.imu.calibration_pending",
        "light_valid": "&published.light.quality.valid",
        "environment_valid": "&published.environment.quality.valid",
        "nav_valid": "&published.nav.valid", "moving": "&published.moving",
        "progress": "&published.progress", "stats": "&stats",
        "stats_size": "sizeof(stats)", "stats_active": "&stats.active",
        "stats_ready": "&stats.result.ready", "window_begin_ms": "&stats.result.begin_ms",
        "window_end_ms": "&stats.result.end_ms", "imu_configured_address": "&imu_context.configured",
    }
    if args.i2c:
        expressions.update({
            "i2c_test": "&i2c_test", "i2c_test_size": "sizeof(i2c_test)",
            "imu_context": "&imu_context", "imu_context_size": "sizeof(imu_context)",
            "bias_dps": "&imu_context.calibration.bias_dps", "initial_q": "&imu_context.initial_q",
            "last_init_ticks": "&imu_context.last_init_ticks", "q": "&published.nav.q",
            "relative_q": "&published.nav.relative_q", "velocity_mps": "&published.nav.velocity_mps",
            "position_m": "&published.nav.position_m", "imu_last_good_ms": "&published.imu.quality.last_good_ms",
            "light_last_good_ms": "&published.light.quality.last_good_ms",
            "environment_last_good_ms": "&published.environment.quality.last_good_ms",
        })
    command = [str(args.gdb), "--batch", "-nx", str(elf)]
    for name, expression in expressions.items():
        command += ["-ex", f'printf "{name}=%u\\n", (unsigned)({expression})']
    layout_text = subprocess.check_output(command, encoding="utf-8", errors="replace")
    layout = {name: int(value) for name, value in re.findall(r"(\w+)=(\d+)", layout_text)}
    if set(layout) != set(expressions) or layout["timing_size"] != 64:
        raise RuntimeError("El ELF no tiene la disposicion temporal esperada; revisar antes de leer")
    if args.i2c and layout["i2c_test_size"] != 2368:
        raise RuntimeError("La traza I2C no tiene el tamaño esperado")

    stamp = datetime.now().astimezone()
    stem = f"Tiempos_D4_{args.label}_{stamp:%Y%m%d_%H%M%S_%f}"
    raw_folder = root / ".local" / "d4_timing" / stem
    raw_folder.mkdir(parents=True)
    result_folder = Path(__file__).resolve().parent / "results"
    result_folder.mkdir(exist_ok=True)
    blobs = {}
    blocks = ("timing", "published", "stats", "i2c_test", "imu_context") if args.i2c else ("timing", "published", "stats")
    for name in blocks:
        address, size = layout[name], layout[name + "_size"]
        if not (0x20000000 <= address < address + size <= 0x20010000):
            raise RuntimeError(f"{name} no esta dentro de la RAM esperada de la placa")
        path = raw_folder / (name + ".bin")
        output = subprocess.check_output([
            str(args.programmer), "--connect", "port=swd", "mode=HOTPLUG",
            "sn=" + args.serial, "--upload", hex(address), hex(size), str(path),
        ], encoding="utf-8", errors="replace", stderr=subprocess.STDOUT)
        (raw_folder / (name + ".txt")).write_text(output, encoding="utf-8")
        blobs[name] = path.read_bytes()
        if len(blobs[name]) != size:
            raise RuntimeError(f"Lectura incompleta de {name}")

    names = ("expired_periods", "missed_periods", "clock_hz", "counter_pair_cycles",
             "startup_cycles", "normal_count", "normal_max_cycles", "normal_max_at_ms",
             "normal_ge_10_ms", "normal_ge_20_ms", "maintenance_count",
             "maintenance_max_cycles", "maintenance_max_at_ms", "maintenance_ge_20_ms")
    timing = dict(zip(names, struct.unpack("<QQ12I", blobs["timing"])))
    if timing["clock_hz"] != 48000000:
        raise RuntimeError("Reloj distinto de los 48 MHz comprobados para este ensayo")
    for name in ("counter_pair", "startup", "normal_max", "maintenance_max"):
        timing[name + "_us_ceil"] = math.ceil(timing[name + "_cycles"] * 1000000 / timing["clock_hz"])
    state = {}
    for name in ("imu_valid", "calibration_pending", "light_valid", "environment_valid", "nav_valid", "moving"):
        state[name] = bool(blobs["published"][layout[name] - layout["published"]])
    state["progress"] = struct.unpack_from("<I", blobs["published"], layout["progress"] - layout["published"])[0]
    for name in ("stats_active", "stats_ready"):
        state[name] = bool(blobs["stats"][layout[name] - layout["stats"]])
    for name in ("window_begin_ms", "window_end_ms"):
        state[name] = struct.unpack_from("<q", blobs["stats"], layout[name] - layout["stats"])[0]

    result = {
        "captured_at": stamp.isoformat(), "label": args.label, "stlink_serial": args.serial,
        "hex_sha256": hashlib.sha256(hex_path.read_bytes()).hexdigest().upper(),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest().upper(),
        "layout": layout, "timing": timing, "state_sequential_read": state,
    }
    if args.i2c:
        control_names = ("command", "phase", "last_command", "gpio_init_result", "injected_ms",
                         "released_ms", "inject_result", "event_count", "dropped", "new_count",
                         "no_new_count", "error_count", "update_max_cycles", "hold_cycles", "cycle", "last_failed_remaining")
        control = dict(zip(control_names, struct.unpack_from("<IIIiIIi9I", blobs["i2c_test"])))
        event_names = ("", "INJECT", "HOLD_BEGIN", "HOLD_END", "READ", "CHECK", "INIT",
                       "RECOVER", "ENV_START", "ENV_READ", "STATE", "ENV_INIT", "TRANSFER", "ID", "ID_CRC_RAW")
        events = []
        for index in range(min(control["event_count"], 96)):
            t_ms, cycle, kind, ret, elapsed, detail = struct.unpack_from("<IIIiII", blobs["i2c_test"], 64 + 24 * index)
            events.append({"t_ms": t_ms, "cycle": cycle, "kind": kind,
                           "name": event_names[kind] if kind < len(event_names) else "UNKNOWN",
                           "result": ret, "elapsed_cycles": elapsed,
                           "us_ceil": math.ceil(elapsed * 1000000 / timing["clock_hz"]), "detail": detail})
            if kind == 12:
                events[-1]["transfer"] = {
                    "address": (detail >> 24) & 255, "flags": (detail >> 16) & 255,
                    "length": (detail >> 12) & 15, "nack": bool(detail & 256),
                    "arbitration_lost": bool(detail & 512), "bus_error": bool(detail & 1024),
                    "first_byte": detail & 255,
                }
        for name, count, block in (("bias_dps", 3, "imu_context"), ("initial_q", 4, "imu_context"),
                                   ("q", 4, "published"), ("relative_q", 4, "published"),
                                   ("velocity_mps", 3, "published"), ("position_m", 3, "published")):
            state[name] = struct.unpack_from("<" + str(count) + "f", blobs[block], layout[name] - layout[block])
        for name in ("imu_last_good_ms", "light_last_good_ms", "environment_last_good_ms"):
            state[name] = struct.unpack_from("<q", blobs["published"], layout[name] - layout["published"])[0]
        state["last_init_ticks"] = struct.unpack_from("<q", blobs["imu_context"], layout["last_init_ticks"] - layout["imu_context"])[0]
        control["update_max_us_ceil"] = math.ceil(control["update_max_cycles"] * 1000000 / timing["clock_hz"])
        control["hold_us_ceil"] = math.ceil(control["hold_cycles"] * 1000000 / timing["clock_hz"])
        result["i2c_control"] = control
        result["i2c_events"] = events
    destination = result_folder / (stem + ".json")
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(destination)
    print(json.dumps({"timing": timing, "state": state}, indent=2))
    if args.i2c:
        print(json.dumps(control, indent=2))
        for event in events:
            print(f"t={event['t_ms']} cycle={event['cycle']} {event['name']} "
                  f"ret={event['result']} us={event['us_ceil']} detail={event['detail']}")
            if "transfer" in event:
                print("  " + json.dumps(event["transfer"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
