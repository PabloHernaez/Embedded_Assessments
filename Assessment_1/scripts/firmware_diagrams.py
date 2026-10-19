#!/usr/bin/env python3
"""Render the firmware PlantUML sources and validate their code hyperlinks.

No arguments: refresh line references and generate SVG/HTML locally.
--check: validate sources and generated artifacts without Java or network access.
--open: validate and open the local gallery in the default browser.
"""

import argparse
from collections import Counter
import hashlib
from html import escape
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from urllib.request import urlopen
import webbrowser
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
DIAGRAM_DIR = ROOT / "docs" / "diagrams"
TEMPLATE = ROOT / "scripts" / "templates" / "firmware-diagram.html"
VERSION = "1.2026.9"
JAR_SHA256 = "aa4a1d06f8911c4b842beff4828dd955be431692b631a312d04c0e9fab9581de"
JAR_PATH = ROOT / ".zephyr-cache" / "tools" / ("plantuml-v" + VERSION + ".jar")
JAR_URL = "https://github.com/plantuml/plantuml/releases/download/v" + VERSION + "/plantuml.jar"
CODE_BASE = "https://github.com/PabloHernaez/Embedded_Assessments/blob/master/Assessment_1/"
DIAGRAMS = (
    ("arranque", "Arranque", "Inicialización del hardware y solicitud de la primera lectura."),
    ("muestreo", "Muestreo y actualización del RGB", "Adquisición del ADC en paralelo con la gestión del botón y el RGB."),
    ("boton", "Botón, temporizador y modos", "Interrupciones, reconocimiento de pulsaciones y transición de modos."),
    ("pausa", "Pausa confirmada antes de BLUE u OFF", "Detención de la adquisición y confirmación al hilo principal."),
)
LINK = re.compile(
    r"\[\[(https://github\.com/PabloHernaez/Embedded_Assessments/blob/"
    r"[^\s{}]+/Assessment_1/(src/[^\s#{}]+)#L\d+)"
    r"(?:\{[^}\r\n]*\})? ([A-Za-z_]\w*(?:\([^\]\r\n]*\))?)\]\]"
)
SVG_NS = "{http://www.w3.org/2000/svg}"
XLINK = "{http://www.w3.org/1999/xlink}href"


class DiagramError(Exception):
    """A rendering failure or an outdated diagram artifact."""


def read(path):
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise DiagramError("No se puede leer {}: {}".format(path, exc)) from exc


def definition_line(relative, label):
    path = PurePosixPath(relative)
    if path.parts[0] != "src" or ".." in path.parts or path.suffix != ".c":
        raise DiagramError("Ruta de codigo no permitida: " + relative)
    symbol = label.split("(", 1)[0]
    source = read(ROOT / path)
    if symbol == "K_THREAD_DEFINE":
        pattern = r"^K_THREAD_DEFINE\(sensor_thread_id,"
    else:
        # Require a definition body, excluding prototypes and function calls.
        pattern = (
            r"^[ \t]*(?:static[ \t]+)?(?:\w+[ \t]+)+\**"
            + re.escape(symbol)
            + r"[ \t]*\([^;{}]*\)[ \t\r\n]*\{"
        )
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise DiagramError("{}: {} definiciones de {}.".format(relative, len(matches), symbol))
    return source.count("\n", 0, matches[0].start()) + 1


def source_links(slug, refresh=False):
    path = DIAGRAM_DIR / (slug + ".puml")
    source = read(path)
    entries = []

    def replace(match):
        _, relative, label = match.groups()
        line = definition_line(relative, label)
        url = CODE_BASE + relative + "#L" + str(line)
        entries.append((url, label))
        return "[[{}{{{}}} {}]]".format(url, label, label)

    updated = LINK.sub(replace, source)
    if not entries or source.count("[[") != len(entries):
        raise DiagramError(slug + ": enlaces de codigo ausentes o no reconocidos.")
    if updated != source:
        if not refresh:
            raise DiagramError(slug + ": referencias desactualizadas; regenera los diagramas.")
        path.write_text(updated, encoding="utf-8", newline="\n")
    return updated, entries


def source_digest(source):
    payload = VERSION + "\0" + read(DIAGRAM_DIR / "theme.puml") + "\0" + source
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def parse_svg(svg, slug):
    try:
        root = ET.fromstring(svg)
    except ET.ParseError as exc:
        raise DiagramError(slug + ": SVG invalido: " + str(exc)) from exc
    text = "".join(root.itertext())
    if root.tag != SVG_NS + "svg" or "Syntax Error" in text or "is ignored" in text:
        raise DiagramError(slug + ": el SVG contiene un error o aviso de PlantUML.")
    return root


def diagram_page(slug, title, description, svg):
    navigation = []
    for name, caption, _ in DIAGRAMS:
        label = {"muestreo": "Muestreo", "boton": "Botón", "pausa": "Pausa"}.get(name, caption)
        current = ' aria-current="page"' if name == slug else ""
        navigation.append('<a href="{}.html"{}>{}</a>'.format(name, current, label))
    values = {
        "TITLE": escape(title), "SLUG": slug, "DESCRIPTION": escape(description),
        "NAVIGATION": "\n".join(navigation), "SVG": svg,
    }
    page = read(TEMPLATE)
    for key, value in values.items():
        page = page.replace("{{" + key + "}}", value)
    return page


def gallery_page():
    cards = "\n".join(
        '<a class="card" href="{0}.html"><img src="{0}.svg" alt="" loading="lazy">'
        '<div><h2>{1:02d} · {2}</h2><p>{3}</p></div></a>'.format(slug, number, escape(title), escape(description))
        for number, (slug, title, description) in enumerate(DIAGRAMS, 1)
    )
    return '''<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Secuencias del firmware · Assessment 1</title>
<style>
:root { color-scheme:dark; font-family:"Segoe UI",sans-serif; color:#e2eaf3; background:#0b1220; }
body { margin:0 auto; max-width:1240px; padding:40px 28px; }
.eyebrow { color:#7fa0b6; font-size:12px; letter-spacing:.12em; text-transform:uppercase; }
h1 { margin:10px 0; font-size:30px; font-weight:600; }
.intro, p { color:#9db6c7; line-height:1.6; }
.grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:24px; margin-top:32px; }
.card { color:inherit; text-decoration:none; border:1px solid #294358; border-radius:12px; overflow:hidden; background:#101c2d; }
.card:hover { border-color:#5eead4; }
.card:focus-visible { outline:2px solid #5eead4; outline-offset:4px; }
.card img { display:block; width:100%; height:240px; object-fit:cover; object-position:top; background:#0b1220; }
.card div { padding:18px 22px; border-top:1px solid #203347; }
h2 { margin:0; font-size:18px; font-weight:600; }
.card p { margin:8px 0 0; font-size:14px; }
@media(max-width:720px) { .grid { grid-template-columns:1fr; } body { padding:24px 18px; } }
</style></head><body>
<p class="eyebrow">Assessment 1 · Firmware</p>
<h1>Secuencias del firmware</h1>
<p class="intro">Arranque, adquisición del ADC, eventos del botón y pausa confirmada.</p>
<main class="grid" aria-label="Diagramas de secuencia">''' + cards + "</main></body></html>\n"


def check_diagrams():
    total = 0
    for slug, title, description in DIAGRAMS:
        source, entries = source_links(slug)
        svg = read(DIAGRAM_DIR / (slug + ".svg"))
        root = parse_svg(svg, slug)
        digest = root.find(SVG_NS + "metadata[@id='firmware-source-digest']")
        if digest is None or digest.text != source_digest(source):
            raise DiagramError(slug + ": SVG desactualizado; regenera los diagramas.")
        anchors = root.findall(".//" + SVG_NS + "a")
        actual = Counter((a.get("href") or a.get(XLINK), "".join(a.itertext())) for a in anchors)
        if actual != Counter(entries) or any(a.get("target") != "_blank" for a in anchors):
            raise DiagramError(slug + ": enlaces SVG distintos de los definidos en PlantUML.")
        if read(DIAGRAM_DIR / (slug + ".html")) != diagram_page(slug, title, description, svg):
            raise DiagramError(slug + ": vista HTML desactualizada; regenera los diagramas.")
        total += len(entries)
    if read(DIAGRAM_DIR / "index.html") != gallery_page():
        raise DiagramError("La galeria esta desactualizada; regenera los diagramas.")
    return total


def plantuml_jar(explicit=None):
    if explicit:
        jar = Path(explicit).resolve()
        if not jar.is_file():
            raise DiagramError("No existe el JAR indicado: " + str(jar))
        return jar
    if not JAR_PATH.exists():
        JAR_PATH.parent.mkdir(parents=True, exist_ok=True)
        print("Descargando PlantUML " + VERSION + " desde su distribucion oficial...")
        try:
            with urlopen(JAR_URL, timeout=60) as response:
                data = response.read()
        except OSError as exc:
            raise DiagramError("No se pudo descargar PlantUML: " + str(exc)) from exc
        if hashlib.sha256(data).hexdigest() != JAR_SHA256:
            raise DiagramError("El JAR descargado no coincide con el SHA256 publicado.")
        JAR_PATH.write_bytes(data)
    if hashlib.sha256(JAR_PATH.read_bytes()).hexdigest() != JAR_SHA256:
        raise DiagramError("El JAR en cache no coincide con la version fijada.")
    return JAR_PATH


def render_diagrams(jar=None):
    java = shutil.which("java")
    if not java:
        raise DiagramError("Java no esta disponible en PATH.")
    jar = plantuml_jar(jar)
    for slug, title, description in DIAGRAMS:
        source, entries = source_links(slug, refresh=True)
        result = subprocess.run(
            [java, "-Djava.awt.headless=true", "-jar", str(jar), "-charset", "UTF-8", "-tsvg", "-nometadata", "-pipe"],
            input=source.encode("utf-8"), capture_output=True, cwd=DIAGRAM_DIR,
        )
        if result.returncode:
            raise DiagramError(slug + ": " + result.stderr.decode("utf-8", errors="replace"))
        svg = result.stdout.decode("utf-8")
        svg = svg[svg.index("<svg"):].strip() + "\n"
        parse_svg(svg, slug)
        svg = svg.replace("<svg ", '<svg aria-label="' + escape(title, quote=True) + '" ', 1)
        end = svg.index(">") + 1
        svg = svg[:end] + '<metadata id="firmware-source-digest">' + source_digest(source) + "</metadata>" + svg[end:]
        (DIAGRAM_DIR / (slug + ".svg")).write_text(svg, encoding="utf-8", newline="\n")
        (DIAGRAM_DIR / (slug + ".html")).write_text(diagram_page(slug, title, description, svg), encoding="utf-8", newline="\n")
        print("  {}: SVG, HTML y {} enlaces de codigo.".format(slug, len(entries)))
    (DIAGRAM_DIR / "index.html").write_text(gallery_page(), encoding="utf-8", newline="\n")
    return check_diagrams()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="validar sin renderizar")
    mode.add_argument("--open", action="store_true", help="abrir la galeria local en el navegador")
    parser.add_argument("--jar", help="ruta opcional a un JAR de PlantUML")
    args = parser.parse_args()
    try:
        count = check_diagrams() if args.check or args.open else render_diagrams(args.jar)
        print("OK: 4 diagramas PlantUML y {} enlaces de codigo.".format(count))
        if args.open and not webbrowser.open((DIAGRAM_DIR / "index.html").as_uri()):
            raise DiagramError("El navegador no pudo abrir la galeria local.")
    except (DiagramError, OSError, ValueError) as exc:
        print("ERROR: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
