// SPDX-License-Identifier: MIT
#pragma once

#include <QScrollArea>
#include <QTimer>

class PdfDocumentModel;
class PageCanvas;

/// Scrollable, centred single-page view. Rendering happens synchronously at
/// the displayed resolution; page changes and zoom use the same PDF4QT render
/// path as printing, so XFA/annotations are always composited.
class PageView : public QScrollArea
{
    Q_OBJECT

public:
    enum class FitMode
    {
        None,
        Width,
        Page
    };

    explicit PageView(QWidget* parent = nullptr);
    ~PageView() override;

    void setModel(PdfDocumentModel* model);

    int currentPage() const { return m_pageIndex; }
    qreal zoomFactor() const { return m_zoom; }

public slots:
    void setPage(int pageIndex);
    void setZoomFactor(qreal zoom);
    void zoomIn();
    void zoomOut();
    void fitWidth();
    void fitPage();
    void refresh();

signals:
    void pageChanged(int pageIndex);
    void zoomChanged(qreal zoom);
    void renderFailed(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onDocumentChanged();
    void renderCurrentPage();

private:
    void updateFitZoom();
    void scheduleFitUpdate();

    PdfDocumentModel* m_model = nullptr;
    PageCanvas* m_canvas = nullptr;
    QTimer m_fitTimer;
    int m_pageIndex = 0;
    qreal m_zoom = 1.0;
    FitMode m_fitMode = FitMode::Page;
};
