// SPDX-License-Identifier: MIT
#include "printdialog.h"

#include "cupsprint.h"
#include "pdfdocumentmodel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

PrintDialog::PrintDialog(PdfDocumentModel* model, QWidget* parent) :
    QDialog(parent),
    m_model(model)
{
    setWindowTitle(tr("Print"));
    setModal(true);

    auto* printerLayout = new QHBoxLayout;
    m_printerCombo = new QComboBox(this);
    m_printerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    printerLayout->addWidget(m_printerCombo, 1);
    printerLayout->addWidget(m_refreshButton);

    auto* printerForm = new QFormLayout;
    printerForm->addRow(tr("Printer:"), printerLayout);

    m_printerMessage = new QLabel(this);
    m_printerMessage->setWordWrap(true);
    m_printerMessage->setStyleSheet(QStringLiteral("color: #b00020;"));

    // ---- Output mode -----------------------------------------------------
    m_modeGroup = new QGroupBox(tr("Output"), this);
    m_directRadio = new QRadioButton(tr("Original PDF (vector, all pages)"), m_modeGroup);
    m_flattenRadio = new QRadioButton(tr("Flatten pages in the viewer"), m_modeGroup);
    m_modeHint = new QLabel(m_modeGroup);
    m_modeHint->setWordWrap(true);
    m_modeHint->setTextFormat(Qt::PlainText);

    const bool hasXfa = m_model && m_model->hasXfaForm();
    const bool isEncrypted = m_model && m_model->isEncrypted();
    const bool forceFlatten = hasXfa || isEncrypted;
    m_directRadio->setChecked(!forceFlatten);
    m_flattenRadio->setChecked(forceFlatten);
    m_directRadio->setEnabled(!forceFlatten);

    if (hasXfa)
    {
        m_modeHint->setText(tr("Static XFA form detected. This viewer supports read-only XFA "
                               "rendering only, so the form is flattened before printing. "
                               "This guarantees that all XFA fields and edges are included."));
    }
    else if (isEncrypted)
    {
        m_modeHint->setText(tr("This PDF is encrypted. Direct mode would send the encrypted "
                               "original to CUPS, which cannot supply the password, so the "
                               "document is flattened before printing."));
    }
    else
    {
        m_modeHint->setText(tr("Original PDF mode sends the unchanged PDF to CUPS and preserves "
                               "vector text and graphics. Use flatten mode if form fields, "
                               "annotations, or unusual page boxes do not print correctly."));
    }

    auto* modeLayout = new QVBoxLayout(m_modeGroup);
    modeLayout->addWidget(m_directRadio);
    modeLayout->addWidget(m_flattenRadio);
    modeLayout->addWidget(m_modeHint);

    // ---- Pages -----------------------------------------------------------
    m_rangeGroup = new QGroupBox(tr("Pages"), this);
    m_allPagesRadio = new QRadioButton(tr("All pages"), m_rangeGroup);
    m_rangeRadio = new QRadioButton(tr("Page range:"), m_rangeGroup);
    m_allPagesRadio->setChecked(true);

    m_firstPageSpin = new QSpinBox(m_rangeGroup);
    m_lastPageSpin = new QSpinBox(m_rangeGroup);
    m_firstPageSpin->setMinimum(1);
    m_lastPageSpin->setMinimum(1);
    m_firstPageSpin->setMaximum(qMax(1, m_model ? m_model->pageCount() : 1));
    m_lastPageSpin->setMaximum(qMax(1, m_model ? m_model->pageCount() : 1));
    m_lastPageSpin->setValue(qMax(1, m_model ? m_model->pageCount() : 1));

    auto* rangeRow = new QHBoxLayout;
    rangeRow->addWidget(m_rangeRadio);
    rangeRow->addWidget(m_firstPageSpin);
    rangeRow->addWidget(new QLabel(tr("to"), m_rangeGroup));
    rangeRow->addWidget(m_lastPageSpin);
    rangeRow->addStretch(1);

    m_rangeHint = new QLabel(m_rangeGroup);
    m_rangeHint->setWordWrap(true);
    m_rangeHint->setTextFormat(Qt::PlainText);

    auto* rangeLayout = new QVBoxLayout(m_rangeGroup);
    rangeLayout->addWidget(m_allPagesRadio);
    rangeLayout->addLayout(rangeRow);
    rangeLayout->addWidget(m_rangeHint);

    // ---- Options ---------------------------------------------------------
    m_copiesSpin = new QSpinBox(this);
    m_copiesSpin->setRange(1, 999);
    m_copiesSpin->setValue(1);

    m_dpiCombo = new QComboBox(this);
    m_dpiCombo->addItem(tr("150 DPI"), 150);
    m_dpiCombo->addItem(tr("300 DPI (recommended)"), 300);
    m_dpiCombo->addItem(tr("600 DPI (large files)"), 600);
    m_dpiCombo->setCurrentIndex(1);

    m_fitCheck = new QCheckBox(tr("Scale to printable area (prevents clipping)"), this);
    m_fitCheck->setChecked(true);
    m_fitCheck->setToolTip(tr("Adds print-scaling=fit to the CUPS job. The whole page, including "
                              "fields close to the paper edge, is fitted into the printable area "
                              "instead of being cropped."));

    m_outputHint = new QLabel(this);
    m_outputHint->setWordWrap(true);
    m_outputHint->setTextFormat(Qt::PlainText);

    auto* optionsForm = new QFormLayout;
    optionsForm->addRow(tr("Copies:"), m_copiesSpin);
    optionsForm->addRow(tr("Raster resolution:"), m_dpiCombo);
    optionsForm->addRow(QString(), m_fitCheck);
    optionsForm->addRow(QString(), m_outputHint);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_printButton = m_buttonBox->addButton(tr("Print"), QDialogButtonBox::AcceptRole);
    m_printButton->setDefault(true);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(printerForm);
    mainLayout->addWidget(m_printerMessage);
    mainLayout->addWidget(m_modeGroup);
    mainLayout->addWidget(m_rangeGroup);
    mainLayout->addLayout(optionsForm);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &PrintDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &PrintDialog::reject);
    connect(m_refreshButton, &QPushButton::clicked, this, &PrintDialog::reloadPrinters);
    connect(m_directRadio, &QRadioButton::toggled, this, &PrintDialog::updateModeUi);
    connect(m_flattenRadio, &QRadioButton::toggled, this, &PrintDialog::updateModeUi);
    connect(m_allPagesRadio, &QRadioButton::toggled, this, &PrintDialog::updateRangeUi);
    connect(m_rangeRadio, &QRadioButton::toggled, this, &PrintDialog::updateRangeUi);
    connect(m_dpiCombo, &QComboBox::currentIndexChanged, this, &PrintDialog::updateOutputHint);
    connect(m_firstPageSpin, &QSpinBox::valueChanged, this, [this](int value)
    {
        if (value > m_lastPageSpin->value())
        {
            const QSignalBlocker blocker(m_lastPageSpin);
            m_lastPageSpin->setValue(value);
        }
        updateOutputHint();
    });
    connect(m_lastPageSpin, &QSpinBox::valueChanged, this, [this](int value)
    {
        if (value < m_firstPageSpin->value())
        {
            const QSignalBlocker blocker(m_firstPageSpin);
            m_firstPageSpin->setValue(value);
        }
        updateOutputHint();
    });

    reloadPrinters();
    updateModeUi();
    updateRangeUi();
    updateOutputHint();
    resize(540, sizeHint().height());
}

void PrintDialog::reloadPrinters()
{
    const QString previousPrinter = m_printerCombo->currentData().toString();
    m_printerCombo->clear();

    m_printerError.clear();
    const QVector<CupsPrinter> printerList = CupsBackend::printers(&m_printerError);

    for (const CupsPrinter& printer : printerList)
    {
        QString label = printer.name;
        if (printer.isDefault)
        {
            label += tr(" (default)");
        }
        m_printerCombo->addItem(label, printer.name);
        if (!printer.info.isEmpty())
        {
            m_printerCombo->setItemData(m_printerCombo->count() - 1, printer.info, Qt::ToolTipRole);
        }
    }

    if (printerList.isEmpty())
    {
        m_printerCombo->addItem(tr("No printers available"));
        m_printerCombo->setEnabled(false);
    }
    else
    {
        m_printerCombo->setEnabled(true);

        int selectedIndex = 0;
        if (!previousPrinter.isEmpty())
        {
            const int previousIndex = m_printerCombo->findData(previousPrinter);
            if (previousIndex >= 0)
            {
                selectedIndex = previousIndex;
            }
        }
        m_printerCombo->setCurrentIndex(selectedIndex);
    }

    m_printerMessage->setText(m_printerError);

    if (m_printButton)
    {
        m_printButton->setEnabled(!printerList.isEmpty() && m_model && m_model->isOpen());
    }
}

void PrintDialog::updateModeUi()
{
    const bool flatten = m_flattenRadio->isChecked();
    m_rangeGroup->setEnabled(flatten);
    m_dpiCombo->setEnabled(flatten);
    m_outputHint->setVisible(flatten);

    if (flatten)
    {
        m_rangeHint->setText(tr("Page selection applies to flattened output. Selected pages are "
                                "rendered at the chosen resolution and sent as one CUPS job."));
    }
    else
    {
        m_rangeHint->setText(tr("Original PDF mode always sends all pages. Switch to flatten mode "
                                "to print a page range."));
    }
}

void PrintDialog::updateRangeUi()
{
    const bool useRange = m_flattenRadio->isChecked() && m_rangeRadio->isChecked();
    m_firstPageSpin->setEnabled(useRange);
    m_lastPageSpin->setEnabled(useRange);
    updateOutputHint();
}

void PrintDialog::updateOutputHint()
{
    if (!m_model || !m_model->isOpen())
    {
        m_outputHint->clear();
        return;
    }

    if (!m_flattenRadio->isChecked())
    {
        m_outputHint->clear();
        return;
    }

    const QVector<int> pages = selectedPageIndices();
    const int dpi = m_dpiCombo->currentData().toInt();

    qint64 largestPagePixels = 0;
    for (int pageIndex : pages)
    {
        const QSizeF pageSizeMM = m_model->pageSizeMm(pageIndex);
        if (pageSizeMM.isEmpty())
        {
            continue;
        }
        const qint64 width = qRound(pageSizeMM.width() / 25.4 * dpi);
        const qint64 height = qRound(pageSizeMM.height() / 25.4 * dpi);
        largestPagePixels = qMax(largestPagePixels, width * height);
    }

    m_outputHint->setText(tr("%1 page(s) will be rasterized at %2 DPI. Largest page image: %3 MP.")
                              .arg(pages.size())
                              .arg(dpi)
                              .arg(QString::number(largestPagePixels / 1'000'000.0, 'f', 1)));
}

QVector<int> PrintDialog::selectedPageIndices() const
{
    if (!m_model || !m_model->isOpen())
    {
        return {};
    }

    const int pageCount = m_model->pageCount();
    if (m_allPagesRadio->isChecked())
    {
        QVector<int> pages;
        pages.reserve(pageCount);
        for (int i = 0; i < pageCount; ++i)
        {
            pages.push_back(i);
        }
        return pages;
    }

    const int firstPage = qBound(1, m_firstPageSpin->value(), pageCount) - 1;
    const int lastPage = qBound(1, m_lastPageSpin->value(), pageCount) - 1;
    const int from = qMin(firstPage, lastPage);
    const int to = qMax(firstPage, lastPage);

    QVector<int> pages;
    pages.reserve(to - from + 1);
    for (int i = from; i <= to; ++i)
    {
        pages.push_back(i);
    }
    return pages;
}

PrintSettings PrintDialog::settings() const
{
    PrintSettings printSettings;
    printSettings.printer = m_printerCombo->currentData().toString();
    printSettings.copies = m_copiesSpin->value();
    printSettings.flatten = m_flattenRadio->isChecked();
    printSettings.dpi = m_dpiCombo->currentData().toInt();
    printSettings.fitToPrintableArea = m_fitCheck->isChecked();

    if (m_model)
    {
        printSettings.documentTitle = QFileInfo(m_model->filePath()).fileName();
    }
    if (printSettings.documentTitle.isEmpty())
    {
        printSettings.documentTitle = tr("PDF document");
    }

    if (printSettings.flatten)
    {
        printSettings.pageIndices = selectedPageIndices();
    }

    return printSettings;
}

void PrintDialog::accept()
{
    if (!m_model || !m_model->isOpen())
    {
        QMessageBox::warning(this, tr("Print"), tr("No PDF document is open."));
        return;
    }

    if (m_printerCombo->currentData().toString().isEmpty())
    {
        QMessageBox::warning(this, tr("Print"),
                             tr("No CUPS printer is available. Start CUPS, configure a printer, "
                                "and make sure the current user can access it."));
        return;
    }

    if (m_flattenRadio->isChecked())
    {
        const QVector<int> pages = selectedPageIndices();
        if (pages.isEmpty())
        {
            QMessageBox::warning(this, tr("Print"), tr("No pages are selected."));
            return;
        }

        const int dpi = m_dpiCombo->currentData().toInt();
        for (int pageIndex : pages)
        {
            const QSizeF pageSizeMM = m_model->pageSizeMm(pageIndex);
            const qint64 width = qRound(pageSizeMM.width() / 25.4 * dpi);
            const qint64 height = qRound(pageSizeMM.height() / 25.4 * dpi);
            if (width * height > MaximumFlattenPixels)
            {
                QMessageBox::warning(this, tr("Print"),
                                     tr("Page %1 would require a %2 x %3 pixel image at %4 DPI, "
                                        "which exceeds the safe memory limit. Choose a lower DPI.")
                                         .arg(pageIndex + 1)
                                         .arg(width)
                                         .arg(height)
                                         .arg(dpi));
                return;
            }
        }
    }

    QDialog::accept();
}
