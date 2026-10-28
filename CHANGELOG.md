# Changelog

## 0.1.0 — 2026-10-28

Initial release.

- Read-only PDF viewing with C++20, Qt 6 Widgets, and PDF4QT Core.
- Static XFA rendering, XFA pagination, and annotation compositing.
- Page navigation, zoom, fit width/page, actual size, drag-and-drop, and
  password-protected document handling.
- CUPS printing in two modes:
  - direct streaming of the original PDF bytes for lossless output;
  - per-page rasterized flatten mode for static XFA and encrypted documents.
- Flatten mode preserves the rotated MediaBox, annotations, and print-only
  content, with 150/300/600 DPI and page ranges.
- Automatic flatten fallback for XFA and encrypted PDFs; encrypted PDFs are
  never sent to CUPS without decryption.
- RedOS 8 x86_64 RPM with the PDF4QT Core library bundled in a private
  directory and an `$ORIGIN`-relative RPATH.
- Offscreen smoke tests for normal rendering, static XFA, repagination,
  flattening, and CUPS direct/flatten/multi-document submission.

Validated on RedOS 8 with GCC 13.3.1, Qt 6.9.3, CUPS 2.4.20, and PDF4QT
1.6.0.0 (commit `67b6b99`). Static XFA support is limited to the subset
implemented by PDF4QT; the included XFA fixture is synthetic.
