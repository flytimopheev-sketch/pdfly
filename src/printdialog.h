// SPDX-License-Identifier: MIT
#pragma once

#include "printsettings.h"

#include <QDialog>

class PdfDocumentModel;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

class PrintDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PrintDialog(PdfDocumentModel* model, QWidget* parent = nullptr);

    PrintSettings settings() const;

protected:
    void accept() override;

private slots:
    void reloadPrinters();
    void updateModeUi();
    void updateRangeUi();

private:
    QVector<int> selectedPageIndices() const;
    void updateOutputHint();

    PdfDocumentModel* m_model = nullptr;

    QComboBox* m_printerCombo = nullptr;
    QPushButton* m_refreshButton = nullptr;

    QGroupBox* m_modeGroup = nullptr;
    QRadioButton* m_directRadio = nullptr;
    QRadioButton* m_flattenRadio = nullptr;
    QLabel* m_modeHint = nullptr;

    QGroupBox* m_rangeGroup = nullptr;
    QRadioButton* m_allPagesRadio = nullptr;
    QRadioButton* m_rangeRadio = nullptr;
    QSpinBox* m_firstPageSpin = nullptr;
    QSpinBox* m_lastPageSpin = nullptr;
    QLabel* m_rangeHint = nullptr;

    QSpinBox* m_copiesSpin = nullptr;
    QComboBox* m_dpiCombo = nullptr;
    QCheckBox* m_fitCheck = nullptr;
    QLabel* m_outputHint = nullptr;

    QLabel* m_printerMessage = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QPushButton* m_printButton = nullptr;

    QString m_printerError;
};
