// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QVector>

/// Keep the peak memory of a single flattened page bounded. The model enforces
/// the same limit as a second line of defence.
inline constexpr qint64 MaximumFlattenPixels = 180'000'000;

struct PrintSettings
{
    QString printer;
    QString documentTitle;
    int copies = 1;

    /// When false the original PDF bytes are sent to CUPS. This is the
    /// lossless, vector-preserving path and always prints every page.
    bool flatten = false;

    /// Page indices (0-based) for flatten mode. Empty means all pages.
    QVector<int> pageIndices;

    int dpi = 300;
    bool fitToPrintableArea = true;
};
