// SPDX-License-Identifier: MIT
#pragma once

#include <QMainWindow>

class PdfDocumentModel;
class PageView;
class QAction;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QLabel;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openFile(const QString& filePath);

protected:
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseFile();
    void printDocument();
    void showDocumentProperties();
    void goToFirstPage();
    void goToPreviousPage();
    void goToNextPage();
    void goToLastPage();
    void updateDocumentUi();
    void updatePageUi(int pageIndex);
    void updateZoomUi(qreal zoom);
    void updateRotationUi(int rotation);
    void showRenderFailure(const QString& message);

private:
    void createActions();
    void createMenusAndToolBar();
    void updateNavigationActions();

    PdfDocumentModel* m_model = nullptr;
    PageView* m_pageView = nullptr;

    QAction* m_openAction = nullptr;
    QAction* m_printAction = nullptr;
    QAction* m_documentInfoAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_firstPageAction = nullptr;
    QAction* m_previousPageAction = nullptr;
    QAction* m_nextPageAction = nullptr;
    QAction* m_lastPageAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_fitWidthAction = nullptr;
    QAction* m_fitPageAction = nullptr;
    QAction* m_rotateLeftAction = nullptr;
    QAction* m_rotateRightAction = nullptr;
    QAction* m_resetRotationAction = nullptr;
    QAction* m_fullScreenAction = nullptr;
    QAction* m_exitFullScreenAction = nullptr;

    QSpinBox* m_pageSpinBox = nullptr;
    QLabel* m_pageCountLabel = nullptr;
    QLabel* m_pageStatusLabel = nullptr;
    QLabel* m_xfaLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
};
