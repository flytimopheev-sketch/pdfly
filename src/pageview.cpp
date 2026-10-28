// SPDX-License-Identifier: MIT
#include "pageview.h"

#include "pdfdocumentmodel.h"

#include <QApplication>
#include <QCursor>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QWheelEvent>
#include <QtMath>

namespace
{
constexpr int PageMargin = 16;
constexpr qreal PointsPerMillimeter = 1.0 / 25.4;
constexpr qreal ScreenDpi = 96.0;
constexpr qreal MinimumZoom = 0.10;
constexpr qreal MaximumZoom = 4.00;
constexpr qint64 MaximumPreviewPixels = 64'000'000;
}   // namespace

// Defined at global scope so the forward declaration in pageview.h names the
// same type. Keeping it in an anonymous namespace would give the definition
// internal linkage and make the member pointer a different type.
class PageCanvas : public QWidget
{
public:
    explicit PageCanvas(QWidget* parent = nullptr) :
        QWidget(parent)
    {
        setAutoFillBackground(false);
    }

    void setContent(const QImage& image, const QSize& pageSize, const QString& message)
    {
        m_image = image;
        m_pageSize = pageSize;
        m_message = message;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(55, 58, 62));

        if (m_pageSize.isEmpty())
        {
            if (!m_message.isEmpty())
            {
                painter.setPen(QColor(220, 220, 220));
                painter.drawText(rect().adjusted(20, 20, -20, -20),
                                 Qt::AlignCenter | Qt::TextWordWrap, m_message);
            }
            return;
        }

        const QRect pageRect(PageMargin, PageMargin,
                             m_pageSize.width(), m_pageSize.height());

        // Subtle shadow and border make the paper edge visible on dark and
        // light themes alike.
        painter.fillRect(pageRect.translated(4, 4), QColor(0, 0, 0, 80));
        painter.fillRect(pageRect, Qt::white);

        if (!m_image.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(pageRect, m_image);
        }

        painter.setPen(QColor(0, 0, 0, 120));
        painter.drawRect(pageRect.adjusted(0, 0, -1, -1));

        if (!m_image.isNull() || m_message.isEmpty())
        {
            return;
        }

        painter.setPen(QColor(170, 40, 40));
        painter.drawText(pageRect.adjusted(20, 20, -20, -20),
                         Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }

private:
    QImage m_image;
    QSize m_pageSize;
    QString m_message;
};

PageView::PageView(QWidget* parent) :
    QScrollArea(parent)
{
    setWidgetResizable(false);
    setAlignment(Qt::AlignCenter);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QPalette viewportPalette = viewport()->palette();
    viewportPalette.setColor(QPalette::Window, QColor(55, 58, 62));
    viewport()->setPalette(viewportPalette);
    viewport()->setAutoFillBackground(true);
    viewport()->installEventFilter(this);

    m_canvas = new PageCanvas;
    m_canvas->setFixedSize(1, 1);
    m_canvas->installEventFilter(this);
    setWidget(m_canvas);

    m_fitTimer.setSingleShot(true);
    m_fitTimer.setInterval(120);
    connect(&m_fitTimer, &QTimer::timeout, this, &PageView::renderCurrentPage);
}

PageView::~PageView() = default;

void PageView::setModel(PdfDocumentModel* model)
{
    if (m_model)
    {
        disconnect(m_model, nullptr, this, nullptr);
    }

    m_model = model;
    if (m_model)
    {
        connect(m_model, &PdfDocumentModel::documentChanged,
                this, &PageView::onDocumentChanged);
    }

    onDocumentChanged();
}

void PageView::onDocumentChanged()
{
    m_pageIndex = 0;
    m_fitMode = FitMode::Page;
    m_zoom = 1.0;

    updateFitZoom();
    renderCurrentPage();
    Q_EMIT pageChanged(m_pageIndex);
}

void PageView::setPage(int pageIndex)
{
    if (!m_model || !m_model->isOpen())
    {
        return;
    }

    const int pageCount = m_model->pageCount();
    if (pageCount <= 0)
    {
        return;
    }

    const int newPage = qBound(0, pageIndex, pageCount - 1);
    const bool pageWasChanged = newPage != m_pageIndex;
    m_pageIndex = newPage;

    renderCurrentPage();

    if (pageWasChanged)
    {
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
        Q_EMIT pageChanged(m_pageIndex);
    }
}

void PageView::setZoomFactor(qreal zoom)
{
    m_fitMode = FitMode::None;

    const qreal newZoom = qBound(MinimumZoom, zoom, MaximumZoom);
    const bool zoomWasChanged = qAbs(newZoom - m_zoom) > 0.0001;
    m_zoom = newZoom;

    if (zoomWasChanged)
    {
        Q_EMIT zoomChanged(m_zoom);
    }

    renderCurrentPage();
}

void PageView::zoomIn()
{
    setZoomFactor(m_zoom * 1.25);
}

void PageView::zoomOut()
{
    setZoomFactor(m_zoom / 1.25);
}

void PageView::fitWidth()
{
    m_fitMode = FitMode::Width;
    updateFitZoom();
    renderCurrentPage();
}

void PageView::fitPage()
{
    m_fitMode = FitMode::Page;
    updateFitZoom();
    renderCurrentPage();
}

void PageView::refresh()
{
    renderCurrentPage();
}

void PageView::updateFitZoom()
{
    if (!m_model || !m_model->isOpen() || m_fitMode == FitMode::None)
    {
        return;
    }

    const QSizeF pageSizeMM = m_model->pageSizeMm(m_pageIndex);
    if (pageSizeMM.isEmpty())
    {
        return;
    }

    const qreal pageWidth = pageSizeMM.width() * PointsPerMillimeter * ScreenDpi;
    const qreal pageHeight = pageSizeMM.height() * PointsPerMillimeter * ScreenDpi;
    if (pageWidth <= 0.0 || pageHeight <= 0.0)
    {
        return;
    }

    const int scrollBarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const qreal availableWidth = viewport()->width() - 2.0 * PageMargin - scrollBarExtent - 4.0;
    const qreal availableHeight = viewport()->height() - 2.0 * PageMargin - scrollBarExtent - 4.0;
    if (availableWidth <= 1.0 || availableHeight <= 1.0)
    {
        return;
    }

    const qreal widthZoom = availableWidth / pageWidth;
    const qreal heightZoom = availableHeight / pageHeight;
    qreal zoom = m_fitMode == FitMode::Width ? widthZoom : qMin(widthZoom, heightZoom);
    zoom = qBound(MinimumZoom, zoom, MaximumZoom);

    if (qAbs(zoom - m_zoom) > 0.0001)
    {
        m_zoom = zoom;
        Q_EMIT zoomChanged(m_zoom);
    }
}

void PageView::scheduleFitUpdate()
{
    if (m_fitMode != FitMode::None)
    {
        m_fitTimer.start();
    }
}

bool PageView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport() || watched == m_canvas)
    {
        if (event->type() == QEvent::Resize)
        {
            scheduleFitUpdate();
        }
        else if (event->type() == QEvent::Wheel)
        {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->modifiers().testFlag(Qt::ControlModifier))
            {
                const int delta = wheelEvent->angleDelta().y();
                if (delta != 0)
                {
                    const qreal factor = delta > 0 ? 1.15 : 1.0 / 1.15;
                    setZoomFactor(m_zoom * factor);
                    wheelEvent->accept();
                    return true;
                }
            }
        }
    }

    return QScrollArea::eventFilter(watched, event);
}

void PageView::renderCurrentPage()
{
    if (!m_model || !m_model->isOpen())
    {
        m_canvas->setContent(QImage(), QSize(), QString());
        m_canvas->setFixedSize(1, 1);
        return;
    }

    if (m_fitMode != FitMode::None)
    {
        updateFitZoom();
    }

    const int pageCount = m_model->pageCount();
    if (pageCount <= 0)
    {
        m_canvas->setContent(QImage(), QSize(), tr("This document has no pages."));
        m_canvas->setFixedSize(1, 1);
        return;
    }

    m_pageIndex = qBound(0, m_pageIndex, pageCount - 1);

    const QSizeF pageSizeMM = m_model->pageSizeMm(m_pageIndex);
    if (pageSizeMM.isEmpty())
    {
        m_canvas->setContent(QImage(), QSize(), tr("The page has an invalid size."));
        m_canvas->setFixedSize(1, 1);
        return;
    }

    const qreal logicalWidth = pageSizeMM.width() * PointsPerMillimeter * ScreenDpi * m_zoom;
    const qreal logicalHeight = pageSizeMM.height() * PointsPerMillimeter * ScreenDpi * m_zoom;
    const int pageWidth = qMax(1, qRound(logicalWidth));
    const int pageHeight = qMax(1, qRound(logicalHeight));

    const qreal devicePixelRatio = qMax<qreal>(1.0, this->devicePixelRatioF());
    qreal pixelScale = devicePixelRatio;

    qint64 pixelCount = qint64(qRound(pageWidth * pixelScale)) *
                        qint64(qRound(pageHeight * pixelScale));
    if (pixelCount > MaximumPreviewPixels)
    {
        pixelScale *= qSqrt(qreal(MaximumPreviewPixels) / qreal(pixelCount));
    }

    const QSize pixelSize(qMax(1, qRound(pageWidth * pixelScale)),
                          qMax(1, qRound(pageHeight * pixelScale)));

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QStringList messages;
    const QImage image = m_model->renderPage(m_pageIndex, pixelSize, &messages);
    QApplication::restoreOverrideCursor();

    QString errorMessage;
    if (image.isNull())
    {
        if (messages.isEmpty())
        {
            errorMessage = tr("The page could not be rendered.");
        }
        else
        {
            const int shownMessages = qMin(4, messages.size());
            errorMessage = messages.mid(0, shownMessages).join(QLatin1Char('\n'));
        }
    }

    m_canvas->setContent(image, QSize(pageWidth, pageHeight), errorMessage);
    m_canvas->setFixedSize(pageWidth + 2 * PageMargin,
                           pageHeight + 2 * PageMargin);

    if (!errorMessage.isEmpty())
    {
        Q_EMIT renderFailed(errorMessage);
    }
}
