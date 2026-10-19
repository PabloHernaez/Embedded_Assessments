#!/usr/bin/env python3
"""Validate the firmware CodeTour and refresh its Markdown source index.

Run from any directory:
    python scripts/check-firmware-docs.py --refresh
    python scripts/check-firmware-docs.py

The tour's unique source patterns locate the Markdown source index. --refresh
also updates PlantUML code links and renders the SVG/HTML views locally.
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
from urllib.parse import unquote, urlsplit

from firmware_diagrams import DIAGRAMS, DiagramError, check_diagrams, render_diagrams


ROOT = Path(__file__).resolve().parent.parent
TOUR_PATH = ROOT / ".tours" / "firmware-original.tour"
DOC_PATH = ROOT / "docs" / "FIRMWARE_SEQUENCE.md"
START_MARKER = "<!-- firmware-source-index:start -->"
END_MARKER = "<!-- firmware-source-index:end -->"
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
DOC_LINK = re.compile(r"\[[^\]]*\]\(<?((?:\./|/)docs/[^\s)>]+)>?\)")
INTERNAL_LINK = re.compile(r"\[[^\]]*\]\(<?#([^\s)>]+)>?\)")


class ValidationError(Exception):
    """An actionable error in a documentation input."""


def read_utf8(path):
    try:
        # Decode bytes directly to preserve newlines outside the generated block.
        return path.read_bytes().decode("utf-8")
    except (OSError, UnicodeError) as exc:
        raise ValidationError("No se puede leer {}: {}".format(path, exc)) from exc


def safe_file(relative, allowed_roots, context):
    if not isinstance(relative, str) or not relative:
        raise ValidationError("{}: falta una ruta de archivo.".format(context))
    path = PurePosixPath(relative)
    if (
        "\\" in relative
        or ":" in relative
        or path.is_absolute()
        or ".." in path.parts
        or not path.parts
        or path.parts[0] not in allowed_roots
    ):
        raise ValidationError(
            "{}: ruta no permitida {!r}; usa {} dentro del proyecto.".format(
                context, relative, " o ".join(sorted(allowed_roots))
            )
        )
    resolved = (ROOT / path).resolve()
    try:
        resolved.relative_to(ROOT)
    except ValueError as exc:
        raise ValidationError(
            "{}: la ruta sale del proyecto: {!r}.".format(context, relative)
        ) from exc
    if not resolved.is_file():
        raise ValidationError(
            "{}: no existe el archivo {!r}.".format(context, relative)
        )
    return resolved


def markdown_anchors(text, context):
    """Collect explicit HTML anchors and ordinary Markdown heading slugs."""
    anchors = set()
    slugs = {}
    opened = None
    mermaid_count = 0
    internal_links = []
    for number, line in enumerate(text.splitlines(), 1):
        fence = FENCE.match(line)
        if opened:
            if (
                fence
                and fence.group(1)[0] == opened[0]
                and len(fence.group(1)) >= opened[1]
                and not fence.group(2).strip()
            ):
                opened = None
            continue
        if fence:
            opened = (fence.group(1)[0], len(fence.group(1)), number)
            language = fence.group(2).strip().split()
            if language and language[0].lower() == "mermaid":
                mermaid_count += 1
            continue
        for link in INTERNAL_LINK.finditer(line):
            internal_links.append((number, unquote(link.group(1))))
        for anchor in re.finditer(
            r"<[A-Za-z][^>]*\b(?:id|name)\s*=\s*(['\"])(.*?)\1", line
        ):
            anchors.add(anchor.group(2))
        heading = re.match(r"^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$", line)
        if heading:
            title = re.sub(r"<[^>]*>", "", heading.group(1))
            title = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", title)
            slug = re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")
            occurrence = slugs.get(slug, 0)
            slugs[slug] = occurrence + 1
            anchors.add(slug if occurrence == 0 else "{}-{}".format(slug, occurrence))
    if opened:
        raise ValidationError(
            "{}: bloque de codigo sin cerrar, abierto en la linea {}.".format(
                context, opened[2]
            )
        )
    for line, fragment in internal_links:
        if fragment not in anchors:
            raise ValidationError(
                "{}:{}: el enlace interno #{} no tiene ancla de destino.".format(
                    context, line, fragment
                )
            )
    return anchors, mermaid_count


def check_doc_links(description, context, documents):
    links = list(DOC_LINK.finditer(description))
    if not links:
        raise ValidationError(
            "{}: incluye al menos un enlace documental con ruta "
            "./docs/... y sin fragmento, compatible con CodeTour.".format(context)
        )
    for match in links:
        if match.group(1).startswith("/docs/"):
            raise ValidationError(
                "{}: CodeTour requiere ./docs/...; no resuelve la ruta "
                "absoluta {}.".format(context, match.group(1))
            )
        target = urlsplit(match.group(1))
        if target.fragment:
            raise ValidationError(
                "{}: CodeTour interpreta #{} como parte del nombre de archivo. "
                "Enlaza ./docs/... sin fragmento y usa las anclas dentro del Markdown."
                .format(context, target.fragment)
            )
        if target.query:
            raise ValidationError(
                "{}: enlace documental con consulta no admitida: {}.".format(
                    context, match.group(1)
                )
            )
        relative = unquote(target.path)[2:]
        path = safe_file(relative, {"docs"}, context)
        if path not in documents:
            text = read_utf8(path)
            documents[path] = (text, markdown_anchors(text, relative)[0])


def load_steps(documents):
    try:
        tour = json.loads(read_utf8(TOUR_PATH).lstrip("\ufeff"))
    except json.JSONDecodeError as exc:
        raise ValidationError(
            "JSON invalido en {}: linea {}, columna {}: {}".format(
                TOUR_PATH, exc.lineno, exc.colno, exc.msg
            )
        ) from exc
    if not isinstance(tour, dict) or not isinstance(tour.get("steps"), list):
        raise ValidationError("El tour debe ser un objeto JSON con una lista steps.")
    if not tour["steps"]:
        raise ValidationError("El tour no contiene pasos.")
    rows = []
    for number, step in enumerate(tour["steps"], 1):
        context = "Paso {}".format(number)
        if not isinstance(step, dict):
            raise ValidationError("{}: debe ser un objeto JSON.".format(context))
        if "line" in step:
            raise ValidationError(
                "{}: elimina line y usa un pattern unico para enlazar al codigo.".format(
                    context
                )
            )
        title = step.get("title")
        if not isinstance(title, str) or not title.strip():
            raise ValidationError("{}: falta title.".format(context))
        context += " ({})".format(title)
        source = safe_file(step.get("file"), {"src", "include"}, context)
        pattern = step.get("pattern")
        if not isinstance(pattern, str) or not pattern:
            raise ValidationError("{}: falta pattern.".format(context))
        try:
            expression = re.compile(pattern)
        except re.error as exc:
            raise ValidationError(
                "{}: pattern no es una expresion regular valida: {}.".format(context, exc)
            ) from exc
        matches = [
            line
            for line, content in enumerate(read_utf8(source).splitlines(), 1)
            if expression.search(content)
        ]
        if len(matches) != 1:
            raise ValidationError(
                "{}: pattern {!r} coincide con {} lineas de {}; se requiere una."
                .format(context, pattern, len(matches), step["file"])
            )
        description = step.get("description")
        if not isinstance(description, str) or not description.strip():
            raise ValidationError("{}: falta description.".format(context))
        check_doc_links(description, context, documents)
        rows.append((number, title, source.relative_to(ROOT).as_posix(), matches[0]))
    return rows


def source_index(rows, newline):
    lines = [
        "",
        "",
        "| Paso | Punto del recorrido | Codigo original |",
        "| --- | --- | --- |",
    ]
    for number, title, file, line in rows:
        title = title.replace("\\", "\\\\").replace("|", "\\|")
        title = " ".join(title.split())
        lines.append(
            "| {} | {} | [`{}:{}`](../{}#L{}) |".format(
                number, title, file, line, file, line
            )
        )
    lines.extend(["", ""])
    return newline.join(lines)


def validate(refresh):
    document = read_utf8(DOC_PATH)
    anchors, mermaid_count = markdown_anchors(document, DOC_PATH.relative_to(ROOT))
    if mermaid_count:
        raise ValidationError(
            "{}: quedan {} bloques Mermaid; las secuencias usan PlantUML.".format(
                DOC_PATH.relative_to(ROOT), mermaid_count
            )
        )
    for slug, _, _ in DIAGRAMS:
        for extension in ("puml", "svg", "html"):
            reference = "diagrams/{}.{}".format(slug, extension)
            if reference not in document:
                raise ValidationError("Falta la referencia documental a " + reference)
    documents = {DOC_PATH.resolve(): (document, anchors)}
    rows = load_steps(documents)
    if document.count(START_MARKER) != 1 or document.count(END_MARKER) != 1:
        raise ValidationError(
            "El documento debe contener una sola pareja de marcadores firmware-source-index."
        )
    start = document.index(START_MARKER) + len(START_MARKER)
    end = document.index(END_MARKER)
    if start > end:
        raise ValidationError("Los marcadores firmware-source-index estan invertidos.")
    newline = "\r\n" if "\r\n" in document else "\n"
    generated = source_index(rows, newline)
    if document[start:end] != generated:
        if not refresh:
            raise ValidationError(
                "El indice de codigo esta desactualizado. Ejecuta "
                "python scripts/check-firmware-docs.py --refresh."
            )
        updated = document[:start] + generated + document[end:]
        DOC_PATH.write_bytes(updated.encode("utf-8"))
        print("Indice de codigo actualizado en docs/FIRMWARE_SEQUENCE.md.")
    else:
        print("Indice de codigo al dia.")
    diagram_links = render_diagrams() if refresh else check_diagrams()
    print(
        "OK: {} pasos con patrones unicos, {} documentos enlazados, "
        "4 diagramas PlantUML y {} enlaces de codigo en SVG/HTML.".format(
            len(rows), len(documents), diagram_links
        )
    )
    for number, title, file, line in rows:
        print("  {}. {} -> {}:{}".format(number, title, file, line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="actualizar el indice, los enlaces PlantUML y sus vistas SVG/HTML",
    )
    arguments = parser.parse_args()
    try:
        validate(arguments.refresh)
    except (ValidationError, DiagramError, OSError, ValueError) as exc:
        print("ERROR: {}".format(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
