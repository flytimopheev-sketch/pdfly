#!/usr/bin/env bash
# Build the PdfQtViewer binary RPM on RedOS 8 x86_64.
#
# Prerequisites:
#   - CMake, Ninja, GCC C++, Qt 6.7+ development files, CUPS development files,
#     patchelf, rpm-build, python3.
#   - PDF4QT Core 1.6.0.0 built and installed in PDF4QT_ROOT (default
#     /opt/pdf4qt); see the main README for the exact build commands.
#
# Usage:
#   ./packaging/build-rpm-redos.sh
#   PDF4QT_ROOT=/path/to/pdf4qt ./packaging/build-rpm-redos.sh
#
# The finished RPM and checksum are copied to ./dist/.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
NAME=pdfqtviewer
VERSION=${VERSION:-$(sed -n 's/^project(PdfQtViewer VERSION \([^ ]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -n1)}
PDF4QT_ROOT=${PDF4QT_ROOT:-/opt/pdf4qt}
TOP=${RPM_TOPDIR:-"$ROOT/.rpmbuild"}
DIST=${DIST_DIR:-"$ROOT/dist"}

if [ -z "$VERSION" ]; then
    echo "Could not determine the project version from CMakeLists.txt." >&2
    exit 1
fi

command -v rpmbuild >/dev/null || {
    echo "rpmbuild is missing. Install rpm-build on RedOS 8." >&2
    exit 1
}
test -d "$PDF4QT_ROOT/include/Pdf4QtLibCore" || {
    echo "PDF4QT Core was not found in $PDF4QT_ROOT." >&2
    echo "Build and install PDF4QT first, see README.md." >&2
    exit 1
}

mkdir -p "$TOP"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS} "$DIST"

# Build the source archive from the committed tree so the RPM source and the
# GitHub tag are identical. Fall back to a plain tar for an uncommitted tree.
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if ! git -C "$ROOT" diff --quiet || ! git -C "$ROOT" diff --cached --quiet; then
        echo "Warning: the working tree has uncommitted changes; only HEAD is packaged." >&2
    fi
    git -C "$ROOT" archive --format=tar.gz \
        --prefix="$NAME-$VERSION/" \
        -o "$TOP/SOURCES/$NAME-$VERSION.tar.gz" HEAD
else
    tar -C "$ROOT" \
        --exclude=.git --exclude=.rpmbuild --exclude=dist --exclude=build \
        --transform "s,^\.,$NAME-$VERSION," \
        -czf "$TOP/SOURCES/$NAME-$VERSION.tar.gz" .
fi

rpmbuild -bb "$ROOT/packaging/rpm/pdfqtviewer.spec" \
    --define "_topdir $TOP" \
    --define "_pdf4qt_root $PDF4QT_ROOT"

rm -f "$DIST"/*.rpm "$DIST"/SHA256SUMS
find "$TOP/RPMS" -type f -name '*.rpm' -exec cp -v {} "$DIST/" \;
(cd "$DIST" && sha256sum *.rpm > SHA256SUMS)

echo
echo "RPMs are in $DIST:"
ls -lh "$DIST"
