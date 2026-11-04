// SPDX-License-Identifier: MIT
#pragma once

#include <pdfdocument.h>

namespace pdf
{
class PDFDocument;
}   // namespace pdf

/// Binds XFA datasets into the static XFA template and introduces page breaks
/// for repeated table rows.  PDF4QT can lay out static XFA templates, but its
/// engine deliberately does not evaluate datasets or split tables across
/// pages.  The function returns the original document when the form is not a
/// supported static XFA form, when preprocessing fails, or when nothing has to
/// be changed.
pdf::PDFDocumentPointer preprocessXfaDocument(const pdf::PDFDocumentPointer& document);

/// Applies the same XFA binding and pagination to raw template/datasets XML.
/// Exposed for diagnostics and the offscreen smoke test.  Returns an empty
/// byte array when the XML is malformed or does not contain instance data.
QByteArray preprocessXfaTemplate(const QByteArray& templateData,
                                 const QByteArray& datasetsData);
