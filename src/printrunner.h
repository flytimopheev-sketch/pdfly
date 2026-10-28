// SPDX-License-Identifier: MIT
#pragma once

#include "printsettings.h"

#include <QString>

class PdfDocumentModel;
class QWidget;

enum class PrintRunResult
{
    Success,
    Cancelled,
    Failed
};

/// Sends a print job synchronously while showing a modal progress dialog.
/// Direct mode streams the original PDF. Flatten mode renders one page at a
/// time, wraps it in a single-page PDF and streams it into the same CUPS job.
PrintRunResult runPrintJob(PdfDocumentModel& model,
                           const PrintSettings& settings,
                           QWidget* parent,
                           QString* errorMessage);
