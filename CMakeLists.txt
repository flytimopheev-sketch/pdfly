cmake_minimum_required(VERSION 3.21)

project(PdfQtViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

# PDF4QT 1.6 uses qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES ...),
# which requires Qt 6.7 or newer.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets)

# ---------------------------------------------------------------------------
# CUPS
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CUPS QUIET IMPORTED_TARGET cups)
endif()

if(TARGET PkgConfig::CUPS)
    set(CUPS_LINK_TARGET PkgConfig::CUPS)
else()
    find_path(CUPS_INCLUDE_DIR NAMES cups/cups.h)
    find_library(CUPS_LIBRARY NAMES cups)
    if(NOT CUPS_INCLUDE_DIR OR NOT CUPS_LIBRARY)
        message(FATAL_ERROR
            "CUPS development files were not found. Install libcups2-dev (Debian/Ubuntu), "
            "cups-devel (Fedora), or cups (Arch), then re-run CMake.")
    endif()

    add_library(cups_imported UNKNOWN IMPORTED)
    set_target_properties(cups_imported PROPERTIES
        IMPORTED_LOCATION "${CUPS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CUPS_INCLUDE_DIR}")
    set(CUPS_LINK_TARGET cups_imported)
endif()

# ---------------------------------------------------------------------------
# PDF4QT Core
#
# PDF4QT does not install a CMake package configuration, so locate the public
# headers (installed under include/Pdf4QtLibCore) and the shared library
# manually. PDF4QT_ROOT may point at the installation prefix, for example
# -DPDF4QT_ROOT=$HOME/.local/pdf4qt.
# ---------------------------------------------------------------------------
set(PDF4QT_ROOT "" CACHE PATH "PDF4QT installation prefix")

find_path(PDF4QT_INCLUDE_DIR
    NAMES pdfdocument.h pdf4qtlibcore_export.h
    HINTS
        "${PDF4QT_ROOT}/include"
        "$ENV{PDF4QT_ROOT}/include"
    PATH_SUFFIXES Pdf4QtLibCore)

find_library(PDF4QT_CORE_LIBRARY
    NAMES Pdf4QtLibCore
    HINTS
        "${PDF4QT_ROOT}/lib"
        "${PDF4QT_ROOT}/lib64"
        "$ENV{PDF4QT_ROOT}/lib"
        "$ENV{PDF4QT_ROOT}/lib64"
    PATH_SUFFIXES lib lib64)

if(NOT PDF4QT_INCLUDE_DIR OR NOT PDF4QT_CORE_LIBRARY)
    message(FATAL_ERROR
        "PDF4QT Core was not found. Build and install PDF4QT with "
        "-DPDF4QT_BUILD_ONLY_CORE_LIBRARY=ON and -DPDF4QT_INSTALL_TO_USR=OFF, then "
        "configure this project with -DPDF4QT_ROOT=<pdf4qt-install-prefix> (or set the "
        "PDF4QT_ROOT environment variable). Expected headers in "
        "<prefix>/include/Pdf4QtLibCore and the library in <prefix>/lib.")
endif()

message(STATUS "PDF4QT headers: ${PDF4QT_INCLUDE_DIR}")
message(STATUS "PDF4QT library: ${PDF4QT_CORE_LIBRARY}")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(pdfqtviewer
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/pageview.cpp
    src/pageview.h
    src/pdfdocumentmodel.cpp
    src/pdfdocumentmodel.h
    src/cupsprint.cpp
    src/cupsprint.h
    src/printdialog.cpp
    src/printdialog.h
    src/printrunner.cpp
    src/printrunner.h
    src/printsettings.h
)

target_include_directories(pdfqtviewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PDF4QT_INCLUDE_DIR}")

target_link_libraries(pdfqtviewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${CUPS_LINK_TARGET}
    "${PDF4QT_CORE_LIBRARY}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pdfqtviewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Make the built and installed executable find a PDF4QT installed outside the
# system library paths. LD_LIBRARY_PATH still works if PDF4QT lives elsewhere.
get_filename_component(PDF4QT_LIBRARY_DIR "${PDF4QT_CORE_LIBRARY}" DIRECTORY)
set_target_properties(pdfqtviewer PROPERTIES
    BUILD_RPATH "${PDF4QT_LIBRARY_DIR}"
    INSTALL_RPATH "${PDF4QT_LIBRARY_DIR};$ORIGIN/../lib")

install(TARGETS pdfqtviewer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/pdfqtviewer.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/pdfqtviewer.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

# ---------------------------------------------------------------------------
# Optional offscreen smoke test
#
# Generates its input with testdata/make_test_pdfs.py and validates normal
# rendering, static XFA compositing, XFA repagination and PDF flattening.
# ---------------------------------------------------------------------------
option(PDFQTVIEWER_BUILD_SMOKE_TEST "Build the offscreen PdfDocumentModel smoke test" OFF)
if(PDFQTVIEWER_BUILD_SMOKE_TEST)
    add_executable(pdfqtviewer-smoke
        testdata/smoke.cpp
        src/pdfdocumentmodel.cpp
        src/pdfdocumentmodel.h
    )

    target_include_directories(pdfqtviewer-smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${PDF4QT_INCLUDE_DIR}")

    target_link_libraries(pdfqtviewer-smoke PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        "${PDF4QT_CORE_LIBRARY}")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pdfqtviewer-smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(pdfqtviewer-smoke PROPERTIES
        BUILD_RPATH "${PDF4QT_LIBRARY_DIR}")

    add_executable(pdfqtviewer-print-smoke
        testdata/print_smoke.cpp
        src/pdfdocumentmodel.cpp
        src/pdfdocumentmodel.h
        src/cupsprint.cpp
        src/cupsprint.h
        src/printrunner.cpp
        src/printrunner.h
        src/printsettings.h
    )

    target_include_directories(pdfqtviewer-print-smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${PDF4QT_INCLUDE_DIR}")

    target_link_libraries(pdfqtviewer-print-smoke PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        ${CUPS_LINK_TARGET}
        "${PDF4QT_CORE_LIBRARY}")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pdfqtviewer-print-smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(pdfqtviewer-print-smoke PROPERTIES
        BUILD_RPATH "${PDF4QT_LIBRARY_DIR}")
endif()
