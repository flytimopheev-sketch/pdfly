Name:           pdfly
Version:        0.2.0
Release:        1%{?dist}
Summary:        Lightweight read-only PDF viewer with static XFA support

License:        MIT
URL:            https://github.com/flytimopheev-sketch/pdfly
Source0:        %{name}-%{version}.tar.gz

%global debug_package %{nil}
%{!?_pdf4qt_root:%global _pdf4qt_root /opt/pdf4qt}

# PDF4QT Core is built and installed separately by packaging/build-rpm-redos.sh
# (or manually) into the configured prefix. Qt 6.7+ is required by PDF4QT 1.6.
BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.7
BuildRequires:  cups-devel
BuildRequires:  patchelf
BuildRequires:  python3

Requires:       qt6-qtbase >= 6.7
Requires:       qt6-qtsvg
Requires:       cups-libs

ExclusiveArch:  x86_64

%description
pdfly is a compact, read-only PDF viewer for Linux built with
Qt 6 Widgets and PDF4QT. It renders one page at a time, composites static
XFA forms and annotations, and prints through CUPS. The interface is
deliberately small: open a document, navigate, zoom, and print.

Normal PDFs are sent to CUPS unchanged, preserving their original vectors
and text. Static XFA forms and documents that need a visual fallback are
rendered page by page at a selected DPI and submitted as one multi-document
CUPS job.

The package bundles the PDF4QT Core shared library in a private directory;
it does not replace or conflict with a system-wide PDF4QT installation.

%prep
%setup -q

%build
test -d %{_pdf4qt_root}/include/Pdf4QtLibCore || {
    echo "PDF4QT Core was not found in %{_pdf4qt_root}." >&2
    echo "Build and install it first, see README.md." >&2
    exit 1
}

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPDF4QT_ROOT=%{_pdf4qt_root} \
    -DPDFLY_BUILD_SMOKE_TEST=ON

cmake --build build --parallel

%check
python3 testdata/make_test_pdfs.py testdata-out
QT_QPA_PLATFORM=offscreen ./build/pdfly-smoke testdata-out smoke-out

%install
DESTDIR=%{buildroot} cmake --install build --prefix %{_prefix}

install -d %{buildroot}%{_libdir}/pdfly
for libdir in %{_pdf4qt_root}/lib64 %{_pdf4qt_root}/lib; do
    if [ -e "$libdir/libPdf4QtLibCore.so" ]; then
        cp -a "$libdir"/libPdf4QtLibCore.so* %{buildroot}%{_libdir}/pdfly/
        break
    fi
done
test -e %{buildroot}%{_libdir}/pdfly/libPdf4QtLibCore.so.1.6.0.0

# Use the bundled copy, not the build-time absolute PDF4QT prefix. $ORIGIN is
# the directory containing the executable (/usr/bin), so this points at
# /usr/lib64/pdfly for the x86_64 package.
patchelf --set-rpath '$ORIGIN/../lib64/pdfly' %{buildroot}%{_bindir}/pdfly

%files
%{_bindir}/pdfly
%dir %{_libdir}/pdfly
%{_libdir}/pdfly/libPdf4QtLibCore.so*
%{_datadir}/applications/pdfly.desktop
%{_datadir}/icons/hicolor/scalable/apps/pdfly.svg
%doc README.md README.en.md CHANGELOG.md
%license LICENSE third_party/PDF4QT-LICENSE third_party/blend2d-LICENSE.md third_party/Clipper2-LICENSE

%changelog
* Wed Oct 28 2026 flytimopheev-sketch <flytimopheev-sketch@users.noreply.github.com> - 0.2.0-1
- Rebranded the project and application as pdfly
- Added Russian and English documentation, screenshots, and a refreshed icon
- Improved the toolbar layout and added an About dialog
- Kept static XFA rendering and direct/flatten CUPS printing

* Wed Oct 28 2026 flytimopheev-sketch <flytimopheev-sketch@users.noreply.github.com> - 0.1.0-1
- Initial PDF viewer with static XFA rendering and CUPS printing
