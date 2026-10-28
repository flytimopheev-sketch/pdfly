// SPDX-License-Identifier: MIT
#pragma once

#include <QImage>
#include <QObject>
#include <QSharedPointer>
#include <QSizeF>
#include <QStringList>

#include <memory>

#include <pdfdocument.h>

namespace pdf
{
class PDFCMSManager;
class PDFFontCache;
class PDFFormManager;
class PDFICCProfileCache;
class PDFAnnotationManager;
class PDFOptionalContentActivity;
}   // namespace pdf

class QWidget;

/// Owns the PDF4QT document and the supporting caches/managers.
///
/// The class is deliberately read-only: it never changes form values and only
/// creates modified documents when PDF4QT itself repaginates a static XFA form.
class PdfDocumentModel : public QObject
{
    Q_OBJECT

public:
    explicit PdfDocumentModel(QObject* parent = nullptr);
    ~PdfDocumentModel() override;

    bool load(const QString& filePath, QWidget* passwordParent, QString* errorMessage);
    void close();

    bool isOpen() const { return !m_document.isNull(); }
    QString filePath() const { return m_filePath; }
    int pageCount() const;
    bool hasXfaForm() const;
    bool isEncrypted() const;

    /// Rotated media box size in millimetres. This is the size used for
    /// printing (including the XFA paginated size).
    QSizeF pageSizeMm(int pageIndex) const;

    /// Render a page for on-screen display. The page is clipped to its crop
    /// box and annotations/XFA are composited on top.
    QImage renderPage(int pageIndex, const QSize& pixelSize, QStringList* messages = nullptr) const;

    /// Render a full rotated media box page for printing. When \p forPrint is
    /// true, print-only optional content is used, annotations are filtered for
    /// printing and content is *not* clipped to the crop box.
    QImage renderCompositedPage(int pageIndex, int dpi, bool forPrint,
                                QStringList* messages = nullptr) const;

    /// Render one page and wrap the resulting image in an exact-size,
    /// single-page PDF. Used for the CUPS flattening fallback.
    bool createFlattenedPagePdf(int pageIndex, int dpi, QByteArray* pdfData,
                                QString* errorMessage, QStringList* messages = nullptr) const;

signals:
    void documentChanged();
    void errorOccurred(const QString& message);

private slots:
    void onFormDocumentModified(pdf::PDFModifiedDocument document);

private:
    bool installDocument(pdf::PDFDocumentPointer document, QString* errorMessage);
    void resetDocumentInfrastructure();

    pdf::PDFDocumentPointer m_document;
    std::unique_ptr<pdf::PDFFontCache> m_fontCache;
    std::unique_ptr<pdf::PDFICCProfileCache> m_iccProfileCache;
    std::unique_ptr<pdf::PDFCMSManager> m_cmsManager;
    std::unique_ptr<pdf::PDFOptionalContentActivity> m_optionalContent;
    std::unique_ptr<pdf::PDFFormManager> m_formManager;
    std::unique_ptr<pdf::PDFAnnotationManager> m_annotationManager;

    QString m_filePath;
    bool m_installingDocument = false;
    bool m_updatingDocument = false;
};
