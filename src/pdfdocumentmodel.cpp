// SPDX-License-Identifier: MIT
#include "pdfdocumentmodel.h"

#include <QBuffer>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QPainter>
#include <QWidget>
#include <QtMath>

#include <pdfannotation.h>
#include <pdfcms.h>
#include <pdfcolorspaces.h>
#include <pdfdocumentmanipulator.h>
#include <pdfdocumentreader.h>
#include <pdfdocumentwriter.h>
#include <pdffont.h>
#include <pdfform.h>
#include <pdfoptionalcontent.h>
#include <pdfpage.h>
#include <pdfrenderer.h>
#include <pdfsecurityhandler.h>
#include <pdftextlayout.h>

namespace
{
constexpr qint64 MaximumRenderedPixels = 180'000'000;

pdf::PDFRenderer::Features displayFeatures()
{
    return pdf::PDFRenderer::Features(pdf::PDFRenderer::Antialiasing)
         | pdf::PDFRenderer::TextAntialiasing
         | pdf::PDFRenderer::SmoothImages
         | pdf::PDFRenderer::ClipToCropBox
         | pdf::PDFRenderer::DisplayAnnotations
         | pdf::PDFRenderer::ColorAdjust_Annotations;
}

pdf::PDFRenderer::Features printFeatures()
{
    return pdf::PDFRenderer::Features(pdf::PDFRenderer::Antialiasing)
         | pdf::PDFRenderer::TextAntialiasing
         | pdf::PDFRenderer::SmoothImages
         | pdf::PDFRenderer::DisplayAnnotations
         | pdf::PDFRenderer::ColorAdjust_Annotations;
}

QSize renderSizeForDpi(const QSizeF& sizeMm, int dpi)
{
    if (sizeMm.isEmpty() || dpi <= 0)
    {
        return QSize();
    }

    const double inchesPerMillimeter = 1.0 / 25.4;
    const int width = qMax(1, qRound(sizeMm.width() * inchesPerMillimeter * dpi));
    const int height = qMax(1, qRound(sizeMm.height() * inchesPerMillimeter * dpi));
    return QSize(width, height);
}
}   // namespace

PdfDocumentModel::PdfDocumentModel(QObject* parent) :
    QObject(parent)
{
}

PdfDocumentModel::~PdfDocumentModel()
{
    resetDocumentInfrastructure();
}

bool PdfDocumentModel::load(const QString& filePath, QWidget* passwordParent, QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile() || !fileInfo.isReadable())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Файл «%1» не существует или недоступен для чтения.").arg(filePath);
        }
        return false;
    }

    auto passwordCallback = [this, passwordParent](bool* ok) -> QString
    {
        if (!ok)
        {
            return QString();
        }

        QInputDialog dialog(passwordParent);
        dialog.setWindowTitle(tr("Требуется пароль"));
        dialog.setLabelText(tr("PDF-документ зашифрован. Введите пароль:"));
        dialog.setTextEchoMode(QLineEdit::Password);
        dialog.setOkButtonText(tr("Открыть"));
        dialog.setCancelButtonText(tr("Отмена"));

        if (dialog.exec() != QDialog::Accepted)
        {
            *ok = false;
            return QString();
        }

        *ok = true;
        return dialog.textValue();
    };

    pdf::PDFDocumentReader reader(nullptr, passwordCallback, true, false);
    pdf::PDFDocument document = reader.readFromFile(fileInfo.absoluteFilePath());

    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        if (errorMessage)
        {
            *errorMessage = reader.getErrorMessage();
            if (errorMessage->isEmpty())
            {
                *errorMessage = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled
                                    ? tr("Открытие документа отменено.")
                                    : tr("Не удалось прочитать PDF-документ.");
            }
        }
        return false;
    }

    if (document.getCatalog()->getPageCount() == 0)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Документ не содержит страниц.");
        }
        return false;
    }

    // The new document was read successfully; only now discard the previous
    // one, so a cancelled password prompt or a broken file leaves the current
    // document open.
    resetDocumentInfrastructure();
    m_filePath.clear();

    // PDF4QT's reader returns the document by value. The shared pointer keeps
    // the document alive while the raw pointers held by the managers are valid.
    pdf::PDFDocumentPointer documentPointer(new pdf::PDFDocument(std::move(document)));
    if (!installDocument(std::move(documentPointer), errorMessage))
    {
        resetDocumentInfrastructure();
        return false;
    }

    m_filePath = fileInfo.absoluteFilePath();
    Q_EMIT documentChanged();
    return true;
}

bool PdfDocumentModel::installDocument(pdf::PDFDocumentPointer document, QString* errorMessage)
{
    if (!document)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Некорректный PDF-документ.");
        }
        return false;
    }

    m_document = std::move(document);

    m_fontCache = std::make_unique<pdf::PDFFontCache>(32, 128);
    m_iccProfileCache = std::make_unique<pdf::PDFICCProfileCache>();
    m_cmsManager = std::make_unique<pdf::PDFCMSManager>(nullptr);
    m_cmsManager->setDocument(m_document.data());
    m_optionalContent = std::make_unique<pdf::PDFOptionalContentActivity>(
        m_document.data(), pdf::OCUsage::View, nullptr);

    pdf::PDFModifiedDocument modifiedDocument(m_document, m_optionalContent.get(),
                                             pdf::PDFModifiedDocument::Reset);

    m_fontCache->setDocument(modifiedDocument);
    m_iccProfileCache->setDocument(m_document.data());

    m_formManager = std::make_unique<pdf::PDFFormManager>(nullptr);
    m_formManager->setAppearanceFlags(pdf::PDFFormManager::FormAppearanceFlags());
    m_formManager->setDocument(modifiedDocument);
    connect(m_formManager.get(), &pdf::PDFFormManager::documentModified,
            this, &PdfDocumentModel::onFormDocumentModified, Qt::DirectConnection);

    m_annotationManager = std::make_unique<pdf::PDFAnnotationManager>(
        m_fontCache.get(),
        m_iccProfileCache.get(),
        m_cmsManager.get(),
        m_optionalContent.get(),
        pdf::PDFMeshQualitySettings(),
        displayFeatures(),
        pdf::PDFAnnotationManager::Target::View,
        nullptr);
    m_annotationManager->setFormManager(m_formManager.get());
    m_annotationManager->setDocument(modifiedDocument);

    // Static XFA forms can change the page count and page sizes. PDF4QT emits
    // documentModified synchronously while performPaging() runs; the slot below
    // swaps all document-aware objects before the function returns.
    m_installingDocument = true;
    m_formManager->performPaging();
    m_installingDocument = false;

    return true;
}

void PdfDocumentModel::onFormDocumentModified(pdf::PDFModifiedDocument document)
{
    if (m_updatingDocument || !document.getDocument())
    {
        return;
    }

    pdf::PDFDocumentPointer newDocument = document;
    if (!newDocument || newDocument == m_document)
    {
        return;
    }

    m_updatingDocument = true;

    // Keep the old document and optional-content activity alive until every
    // manager has been switched to the new document.
    pdf::PDFDocumentPointer oldDocument = std::move(m_document);
    std::unique_ptr<pdf::PDFOptionalContentActivity> oldActivity = std::move(m_optionalContent);

    m_document = std::move(newDocument);
    m_optionalContent = std::make_unique<pdf::PDFOptionalContentActivity>(
        m_document.data(), pdf::OCUsage::View, nullptr);
    document.setOptionalContentActivity(m_optionalContent.get());

    m_fontCache->setDocument(document);
    m_iccProfileCache->setDocument(m_document.data());
    m_cmsManager->setDocument(m_document.data());
    m_annotationManager->setDocument(document);
    m_formManager->setDocument(document);

    m_updatingDocument = false;

    if (!m_installingDocument)
    {
        Q_EMIT documentChanged();
    }
}

void PdfDocumentModel::close()
{
    const bool wasOpen = isOpen();
    resetDocumentInfrastructure();
    m_filePath.clear();

    if (wasOpen)
    {
        Q_EMIT documentChanged();
    }
}

void PdfDocumentModel::resetDocumentInfrastructure()
{
    if (m_formManager)
    {
        m_formManager->disconnect(this);
    }

    // Managers must go away before their caches and the document.
    m_annotationManager.reset();
    m_formManager.reset();
    m_optionalContent.reset();
    m_cmsManager.reset();
    m_iccProfileCache.reset();
    m_fontCache.reset();
    m_document.reset();

    m_installingDocument = false;
    m_updatingDocument = false;
}

int PdfDocumentModel::pageCount() const
{
    if (!m_document)
    {
        return 0;
    }

    return static_cast<int>(m_document->getCatalog()->getPageCount());
}

bool PdfDocumentModel::hasXfaForm() const
{
    return m_formManager && m_formManager->hasXFAForm();
}

bool PdfDocumentModel::isEncrypted() const
{
    if (!m_document)
    {
        return false;
    }

    const pdf::PDFSecurityHandler* handler = m_document->getStorage().getSecurityHandler();
    return handler && handler->getMode() != pdf::EncryptionMode::None;
}

QSizeF PdfDocumentModel::pageSizeMm(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= pageCount())
    {
        return QSizeF();
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    return page ? page->getRotatedMediaBoxMM().size() : QSizeF();
}

QImage PdfDocumentModel::renderPage(int pageIndex, const QSize& pixelSize, QStringList* messages) const
{
    if (!m_document || !m_annotationManager || pageIndex < 0 || pageIndex >= pageCount())
    {
        return QImage();
    }

    if (!pixelSize.isValid() || pixelSize.isEmpty())
    {
        return QImage();
    }

    const qint64 pixelCount = qint64(pixelSize.width()) * qint64(pixelSize.height());
    if (pixelCount > MaximumRenderedPixels)
    {
        if (messages)
        {
            *messages << tr("Запрошенное изображение страницы слишком велико (%1 × %2 пикселей).")
                             .arg(pixelSize.width())
                             .arg(pixelSize.height());
        }
        return QImage();
    }

    QImage image(pixelSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        if (messages)
        {
            *messages << tr("Недостаточно памяти для отрисовки страницы.");
        }
        return QImage();
    }
    image.fill(Qt::white);

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    if (!page)
    {
        return QImage();
    }

    const pdf::PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
    const pdf::PDFRenderer::Features features = displayFeatures();

    QPainter painter(&image);
    if (!painter.isActive())
    {
        return QImage();
    }

    const QRectF targetRect(0.0, 0.0, pixelSize.width(), pixelSize.height());
    const QTransform matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, targetRect);

    pdf::PDFRenderer renderer(m_document.data(),
                              m_fontCache.get(),
                              m_iccProfileCache.get(),
                              cms.data(),
                              m_optionalContent.get(),
                              features,
                              pdf::PDFMeshQualitySettings());

    QList<pdf::PDFRenderError> errors = renderer.render(&painter, matrix, static_cast<size_t>(pageIndex));

    pdf::PDFColorConvertor convertor = cms->getColorConvertor();
    pdf::PDFRenderer::applyFeaturesToColorConvertor(features, convertor);
    pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
    m_annotationManager->drawPage(&painter, pageIndex, nullptr, textLayoutGetter,
                                  matrix, convertor, errors);
    painter.end();

    if (messages)
    {
        for (const pdf::PDFRenderError& error : errors)
        {
            *messages << tr("Страница %1: %2").arg(pageIndex + 1).arg(error.message);
        }
    }

    return image;
}

QImage PdfDocumentModel::renderCompositedPage(int pageIndex, int dpi, bool forPrint,
                                              QStringList* messages) const
{
    if (!m_document || !m_annotationManager || pageIndex < 0 || pageIndex >= pageCount())
    {
        return QImage();
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    if (!page)
    {
        return QImage();
    }

    const QSize imageSize = renderSizeForDpi(page->getRotatedMediaBoxMM().size(), dpi);
    if (!imageSize.isValid() || imageSize.isEmpty())
    {
        if (messages)
        {
            *messages << tr("Страница %1 имеет недопустимый размер.").arg(pageIndex + 1);
        }
        return QImage();
    }

    const qint64 pixelCount = qint64(imageSize.width()) * qint64(imageSize.height());
    if (pixelCount > MaximumRenderedPixels)
    {
        if (messages)
        {
            *messages << tr("Для страницы %1 при разрешении %2 DPI требуется %3 × %4 пикселей — "
                            "это превышает безопасный лимит. Выберите меньшее разрешение.")
                             .arg(pageIndex + 1)
                             .arg(dpi)
                             .arg(imageSize.width())
                             .arg(imageSize.height());
        }
        return QImage();
    }

    QImage image(imageSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        if (messages)
        {
            *messages << tr("Недостаточно памяти для отрисовки страницы %1 при разрешении %2 DPI.")
                             .arg(pageIndex + 1)
                             .arg(dpi);
        }
        return QImage();
    }
    image.fill(Qt::white);

    const pdf::PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
    const pdf::PDFRenderer::Features features = forPrint ? printFeatures() : displayFeatures();

    // The flattening path must show what a printer would show. Use print-only
    // optional content and print annotation filtering, but never clip to the
    // crop box: printer marks and fields close to the media-box edge stay in
    // the output. The scope guard restores the on-screen state afterwards.
    std::unique_ptr<pdf::PDFOptionalContentActivity> printActivity;
    struct AnnotationStateGuard
    {
        pdf::PDFAnnotationManager* manager = nullptr;
        pdf::PDFAnnotationManager::Target target = pdf::PDFAnnotationManager::Target::View;
        const pdf::PDFOptionalContentActivity* activity = nullptr;

        ~AnnotationStateGuard()
        {
            if (manager)
            {
                manager->setTarget(target);
                manager->setOptionalActivity(activity);
            }
        }
    } stateGuard;

    const pdf::PDFOptionalContentActivity* activity = m_optionalContent.get();
    if (forPrint)
    {
        printActivity = std::make_unique<pdf::PDFOptionalContentActivity>(
            m_document.data(), pdf::OCUsage::Print, nullptr);
        activity = printActivity.get();

        stateGuard.manager = m_annotationManager.get();
        stateGuard.target = m_annotationManager->getTarget();
        stateGuard.activity = m_annotationManager->getOptionalActivity();
        m_annotationManager->setTarget(pdf::PDFAnnotationManager::Target::Print);
        m_annotationManager->setOptionalActivity(activity);
    }

    QPainter painter(&image);
    if (!painter.isActive())
    {
        return QImage();
    }

    const QRectF targetRect(0.0, 0.0, imageSize.width(), imageSize.height());
    const QTransform matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, targetRect);

    pdf::PDFRenderer renderer(m_document.data(),
                              m_fontCache.get(),
                              m_iccProfileCache.get(),
                              cms.data(),
                              activity,
                              features,
                              pdf::PDFMeshQualitySettings());

    QList<pdf::PDFRenderError> errors = renderer.render(&painter, matrix, static_cast<size_t>(pageIndex));

    pdf::PDFColorConvertor convertor = cms->getColorConvertor();
    pdf::PDFRenderer::applyFeaturesToColorConvertor(features, convertor);
    pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
    m_annotationManager->drawPage(&painter, pageIndex, nullptr, textLayoutGetter,
                                  matrix, convertor, errors);
    painter.end();

    if (messages)
    {
        for (const pdf::PDFRenderError& error : errors)
        {
            *messages << tr("Страница %1: %2").arg(pageIndex + 1).arg(error.message);
        }
    }

    return image;
}

bool PdfDocumentModel::createFlattenedPagePdf(int pageIndex, int dpi, QByteArray* pdfData,
                                               QString* errorMessage, QStringList* messages) const
{
    if (pdfData)
    {
        pdfData->clear();
    }
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!pdfData)
    {
        return false;
    }

    if (!m_document || pageIndex < 0 || pageIndex >= pageCount())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Недопустимый индекс страницы.");
        }
        return false;
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    if (!page)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Страницы %1 не существует.").arg(pageIndex + 1);
        }
        return false;
    }

    const QSizeF pageSizeMM = page->getRotatedMediaBoxMM().size();
    QImage image = renderCompositedPage(pageIndex, dpi, true, messages);
    if (image.isNull())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Не удалось отрисовать страницу %1 при разрешении %2 DPI.").arg(pageIndex + 1).arg(dpi);
        }
        return false;
    }

    pdf::PDFDocumentManipulator manipulator;
    manipulator.addImage(0, std::move(image));

    pdf::PDFDocumentManipulator::AssembledPages pages;
    pages.push_back(pdf::PDFDocumentManipulator::createImagePage(
        0, pageSizeMM, pdf::PageRotation::None));

    pdf::PDFOperationResult result = manipulator.assemble(pages);
    if (!result)
    {
        if (errorMessage)
        {
            *errorMessage = result.getErrorMessage();
        }
        return false;
    }

    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = tr("Не удалось создать PDF-документ в памяти.");
        }
        return false;
    }

    pdf::PDFDocumentWriter writer(nullptr);
    result = writer.write(&buffer, &manipulator.getAssembledDocument());
    buffer.close();

    if (!result)
    {
        if (errorMessage)
        {
            *errorMessage = result.getErrorMessage();
        }
        return false;
    }

    *pdfData = buffer.data();
    if (pdfData->isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Созданный PDF-документ пуст.");
        }
        return false;
    }

    return true;
}
