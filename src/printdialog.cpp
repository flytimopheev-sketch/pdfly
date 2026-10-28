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
    setWindowTitle(tr("Печать"));
    setModal(true);

    auto* printerLayout = new QHBoxLayout;
    m_printerCombo = new QComboBox(this);
    m_printerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshButton = new QPushButton(tr("Обновить"), this);
    printerLayout->addWidget(m_printerCombo, 1);
    printerLayout->addWidget(m_refreshButton);

    auto* printerForm = new QFormLayout;
    printerForm->addRow(tr("Принтер:"), printerLayout);

    m_printerMessage = new QLabel(this);
    m_printerMessage->setWordWrap(true);
    m_printerMessage->setStyleSheet(QStringLiteral("color: #b00020;"));

    // ---- Output mode -----------------------------------------------------
    m_modeGroup = new QGroupBox(tr("Способ вывода"), this);
    m_directRadio = new QRadioButton(tr("Исходный PDF (векторный, все страницы)"), m_modeGroup);
    m_flattenRadio = new QRadioButton(tr("Растровые страницы"), m_modeGroup);
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
        m_modeHint->setText(tr("Обнаружена статическая XFA-форма. Просмотрщик поддерживает "
                               "XFA только в режиме чтения, поэтому перед печатью форма будет "
                               "растрирована. Это гарантирует вывод всех полей XFA и их границ."));
    }
    else if (isEncrypted)
    {
        m_modeHint->setText(tr("PDF зашифрован. В прямом режиме зашифрованный файл ушёл бы "
                               "в CUPS, который не может запросить пароль, поэтому документ "
                               "будет растрирован перед печатью."));
    }
    else
    {
        m_modeHint->setText(tr("Прямой режим отправляет исходный PDF в CUPS без изменений и "
                               "сохраняет векторный текст и графику. Если поля форм, аннотации "
                               "или необычные границы страницы печатаются неправильно, выберите "
                               "растровый режим."));
    }

    auto* modeLayout = new QVBoxLayout(m_modeGroup);
    modeLayout->addWidget(m_directRadio);
    modeLayout->addWidget(m_flattenRadio);
    modeLayout->addWidget(m_modeHint);

    // ---- Pages -----------------------------------------------------------
    m_rangeGroup = new QGroupBox(tr("Страницы"), this);
    m_allPagesRadio = new QRadioButton(tr("Все страницы"), m_rangeGroup);
    m_rangeRadio = new QRadioButton(tr("Диапазон страниц:"), m_rangeGroup);
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
    rangeRow->addWidget(new QLabel(tr("по"), m_rangeGroup));
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
    m_dpiCombo->addItem(tr("300 DPI (рекомендуется)"), 300);
    m_dpiCombo->addItem(tr("600 DPI (большие файлы)"), 600);
    m_dpiCombo->setCurrentIndex(1);

    m_fitCheck = new QCheckBox(tr("Вписать в область печати (без обрезки)"), this);
    m_fitCheck->setChecked(true);
    m_fitCheck->setToolTip(tr("Добавляет в задание CUPS параметр print-scaling=fit. "
                              "Страница целиком, включая поля у края бумаги, вписывается "
                              "в область печати вместо обрезки."));

    m_outputHint = new QLabel(this);
    m_outputHint->setWordWrap(true);
    m_outputHint->setTextFormat(Qt::PlainText);

    auto* optionsForm = new QFormLayout;
    optionsForm->addRow(tr("Копии:"), m_copiesSpin);
    optionsForm->addRow(tr("Разрешение растра:"), m_dpiCombo);
    optionsForm->addRow(QString(), m_fitCheck);
    optionsForm->addRow(QString(), m_outputHint);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    m_printButton = m_buttonBox->addButton(tr("Печать"), QDialogButtonBox::AcceptRole);
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
            label += tr(" (по умолчанию)");
        }
        m_printerCombo->addItem(label, printer.name);
        if (!printer.info.isEmpty())
        {
            m_printerCombo->setItemData(m_printerCombo->count() - 1, printer.info, Qt::ToolTipRole);
        }
    }

    if (printerList.isEmpty())
    {
        m_printerCombo->addItem(tr("Принтеры не найдены"));
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
        m_rangeHint->setText(tr("Диапазон действует для растрового вывода. Выбранные страницы "
                                "будут отрисованы с указанным разрешением и отправлены одним "
                                "заданием CUPS."));
    }
    else
    {
        m_rangeHint->setText(tr("В прямом режиме всегда отправляются все страницы. Чтобы "
                                "напечатать диапазон, выберите растровый режим."));
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

    m_outputHint->setText(tr("Будет растрировано страниц: %1, разрешение: %2 DPI. "
                             "Самое большое изображение: %3 Мп.")
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
        printSettings.documentTitle = tr("PDF-документ");
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
        QMessageBox::warning(this, tr("Печать"), tr("PDF-документ не открыт."));
        return;
    }

    if (m_printerCombo->currentData().toString().isEmpty())
    {
        QMessageBox::warning(this, tr("Печать"),
                             tr("Нет доступного принтера CUPS. Запустите CUPS, настройте "
                                "принтер и убедитесь, что текущий пользователь имеет к нему "
                                "доступ."));
        return;
    }

    if (m_flattenRadio->isChecked())
    {
        const QVector<int> pages = selectedPageIndices();
        if (pages.isEmpty())
        {
            QMessageBox::warning(this, tr("Печать"), tr("Страницы не выбраны."));
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
                QMessageBox::warning(this, tr("Печать"),
                                     tr("Для страницы %1 при разрешении %2 DPI потребуется "
                                        "изображение %3 × %4 пикселей, что превышает безопасный "
                                        "лимит памяти. Выберите меньшее разрешение.")
                                         .arg(pageIndex + 1)
                                         .arg(dpi)
                                         .arg(width)
                                         .arg(height));
                return;
            }
        }
    }

    QDialog::accept();
}
