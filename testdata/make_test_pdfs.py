#!/usr/bin/env python3
"""Create small PDFs used to smoke-test PdfQtViewer without external assets.

The static XFA document contains an XFA template packet only. This is the
read-only XFA subset supported by PDF4QT: static page geometry, draw elements
and a text field with a border. The base PDF page deliberately uses A4 while
the XFA page area uses Letter, so page repagination is exercised as well.
"""

from __future__ import annotations

import argparse
import pathlib


def build_pdf(objects: dict[int, bytes], root: int, info: int | None = None) -> bytes:
    output = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets: dict[int, int] = {}

    for number in sorted(objects):
        offsets[number] = len(output)
        output += f"{number} 0 obj\n".encode()
        output += objects[number]
        if not objects[number].endswith(b"\n"):
            output += b"\n"
        output += b"endobj\n"

    xref_offset = len(output)
    max_number = max(objects)
    output += f"xref\n0 {max_number + 1}\n".encode()
    output += b"0000000000 65535 f \n"
    for number in range(1, max_number + 1):
        if number in offsets:
            output += f"{offsets[number]:010d} 00000 n \n".encode()
        else:
            output += b"0000000000 65535 f \n"

    trailer = f"trailer\n<< /Size {max_number + 1} /Root {root} 0 R"
    if info is not None:
        trailer += f" /Info {info} 0 R"
    trailer += " >>\nstartxref\n"
    output += trailer.encode()
    output += f"{xref_offset}\n%%EOF\n".encode()
    return bytes(output)


def stream(data: bytes, extra: bytes = b"") -> bytes:
    header = f"<< /Length {len(data)}".encode()
    if extra:
        header += b" " + extra
    header += b" >>\nstream\n"
    return header + data + b"\nendstream"


def make_normal_pdf() -> bytes:
    content = b"""0.10 0.25 0.55 rg
72 690 451 70 re f
0 0 0 rg
BT /F1 22 Tf 72 720 Td (Normal PDF page) Tj ET
BT /F1 12 Tf 72 665 Td (Vector text and graphics - direct CUPS path) Tj ET
0 0 0 RG 2 w
40 40 515 110 re S
0.8 0.1 0.1 RG 4 w
40 40 m 555 150 l S
"""
    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>"),
        4: stream(content),
        5: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        6: b"<< /Title (Normal PDF test page) /Producer (PdfQtViewer test data) >>",
    }
    return build_pdf(objects, root=1, info=6)


def make_normal_2page_pdf() -> bytes:
    page_one = b"""0.10 0.25 0.55 rg
72 690 451 70 re f
0 0 0 rg
BT /F1 22 Tf 72 720 Td (Two-page PDF - page 1) Tj ET
"""
    page_two = b"""0.55 0.20 0.10 rg
40 500 250 50 re f
0 0 0 rg
BT /F1 18 Tf 40 520 Td (Two-page PDF - page 2) Tj ET
"""
    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
        3: (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 7 0 R >> >> /Contents 5 0 R >>"),
        4: (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 420 595] "
            b"/Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>"),
        5: stream(page_one),
        6: stream(page_two),
        7: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        8: b"<< /Title (Two-page print test) /Producer (PdfQtViewer test data) >>",
    }
    return build_pdf(objects, root=1, info=8)


def make_static_xfa_pdf() -> bytes:
    template = b"""<?xml version="1.0" encoding="UTF-8"?>
<template xmlns="http://www.xfa.org/schema/xfa-template/3.3/">
  <subform name="form" layout="tb" locale="en_US">
    <pageSet>
      <pageArea name="page1" id="page1">
        <medium stock="custom" short="8.5in" long="11in"/>
        <contentArea x="0.5in" y="0.5in" w="7.5in" h="10in"/>
      </pageArea>
    </pageSet>
    <field name="message" x="1in" y="1in" w="4.5in" h="0.65in">
      <ui><textEdit multiLine="0"/></ui>
      <font typeface="Helvetica" size="14pt"/>
      <value><text>XFA STATIC FIELD</text></value>
      <border>
        <edge stroke="solid" thickness="1.5pt">
          <color value="220,30,30"/>
        </edge>
      </border>
      <margin leftInset="6pt" rightInset="6pt" topInset="3pt" bottomInset="3pt"/>
    </field>
    <draw name="notice" x="1in" y="2in" w="6in" h="0.5in">
      <font typeface="Helvetica" size="12pt"/>
      <value><text>Rendered from the static XFA template packet</text></value>
    </draw>
  </subform>
</template>
"""
    # The base page intentionally differs from the XFA page area (A4 vs Letter).
    content = b"0.9 0.9 0.9 rg 0 0 595 842 re f\n"

    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            b"/Resources << /Font << /F1 6 0 R >> >> /Contents 4 0 R >>"),
        4: stream(content),
        5: b"<< /Fields [] /XFA [(template) 7 0 R] >>",
        6: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        7: stream(template, b"/Type /EmbeddedFile /Subtype /text#2Fxml"),
        8: b"<< /Title (Static XFA test form) /Producer (PdfQtViewer test data) >>",
    }
    return build_pdf(objects, root=1, info=8)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    normal = args.output / "normal.pdf"
    two_page = args.output / "normal-2page.pdf"
    xfa = args.output / "static-xfa.pdf"
    normal.write_bytes(make_normal_pdf())
    two_page.write_bytes(make_normal_2page_pdf())
    xfa.write_bytes(make_static_xfa_pdf())
    print(f"wrote {normal} ({normal.stat().st_size} bytes)")
    print(f"wrote {two_page} ({two_page.stat().st_size} bytes)")
    print(f"wrote {xfa} ({xfa.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
