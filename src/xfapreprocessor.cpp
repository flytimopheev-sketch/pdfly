// SPDX-License-Identifier: MIT
#include "xfapreprocessor.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFont>
#include <QFontMetricsF>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <QtMath>

#include <pdfdocumentbuilder.h>
#include <pdfobject.h>
#include <pdfstreamfilters.h>

namespace
{
constexpr auto xfaDataNamespace = "http://www.xfa.org/schema/xfa-data/1.0/";
constexpr auto defaultTemplateNamespace = "http://www.xfa.org/schema/xfa-template/3.0/";
constexpr double fallbackContentHeightMm = 180.0;

QString localName(const QDomElement& element)
{
    const QString name = element.localName();
    return name.isEmpty() ? element.tagName() : name;
}

QDomElement firstChild(const QDomElement& parent, const QString& name)
{
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }
        const QDomElement element = node.toElement();
        if (localName(element) == name)
        {
            return element;
        }
    }
    return {};
}

QList<QDomElement> childElements(const QDomElement& parent, const QString& name)
{
    QList<QDomElement> result;
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }
        const QDomElement element = node.toElement();
        if (localName(element) == name)
        {
            result.append(element);
        }
    }
    return result;
}


QDomElement firstDescendant(const QDomElement& parent, const QString& name)
{
    if (localName(parent) == name)
    {
        return parent;
    }
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (node.isElement())
        {
            const QDomElement result = firstDescendant(node.toElement(), name);
            if (!result.isNull())
            {
                return result;
            }
        }
    }
    return {};
}

QString bindReference(const QDomElement& element)
{
    const QDomElement bind = firstChild(element, "bind");
    return bind.isNull() ? QString() : bind.attribute("ref");
}

bool hasDataRows(const QDomElement& table)
{
    for (const QDomElement& row : childElements(table, "subform"))
    {
        QString reference = bindReference(row);
        if (reference.endsWith(QStringLiteral("[*]")))
        {
            return true;
        }
    }
    return false;
}

double parseMeasurement(const QString& text, double fallback = 0.0)
{
    const QString value = text.trimmed();
    if (value.isEmpty())
    {
        return fallback;
    }

    static const QRegularExpression expression(
        QStringLiteral(R"(^([+-]?(?:\d+(?:[.,]\d*)?|[.,]\d+))\s*([a-zA-Z]*)$)"));
    const QRegularExpressionMatch match = expression.match(value);
    if (!match.hasMatch())
    {
        return fallback;
    }

    QString number = match.captured(1);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double amount = number.toDouble(&ok);
    if (!ok)
    {
        return fallback;
    }

    const QString unit = match.captured(2).toLower();
    if (unit.isEmpty() || unit == QStringLiteral("mm"))
    {
        return amount;
    }
    if (unit == QStringLiteral("cm"))
    {
        return amount * 10.0;
    }
    if (unit == QStringLiteral("in"))
    {
        return amount * 25.4;
    }
    if (unit == QStringLiteral("pt"))
    {
        return amount * 25.4 / 72.0;
    }
    if (unit == QStringLiteral("pc"))
    {
        return amount * 25.4 / 6.0;
    }
    if (unit == QStringLiteral("px"))
    {
        return amount * 25.4 / 96.0;
    }
    return fallback;
}

QString measurementText(double value)
{
    return QString::number(value, 'f', 2) + QStringLiteral("mm");
}

double contentAreaHeight(const QDomDocument& document)
{
    const QDomNodeList pageAreas = document.elementsByTagName(QStringLiteral("pageArea"));
    for (int index = 0; index < pageAreas.count(); ++index)
    {
        const QDomElement pageArea = pageAreas.at(index).toElement();
        if (pageArea.isNull())
        {
            continue;
        }
        const QDomElement contentArea = firstDescendant(pageArea, QStringLiteral("contentArea"));
        if (contentArea.isNull())
        {
            continue;
        }
        const double height = parseMeasurement(contentArea.attribute("h"), 0.0);
        if (height > 1.0)
        {
            return height;
        }
    }
    return fallbackContentHeightMm;
}

QString collectValueText(const QDomElement& element)
{
    const QDomElement value = firstDescendant(element, QStringLiteral("value"));
    if (!value.isNull())
    {
        const QDomElement text = firstDescendant(value, QStringLiteral("text"));
        if (!text.isNull())
        {
            return text.text().trimmed();
        }
    }
    return QString();
}

QString collectAllText(const QDomElement& element)
{
    QString result;
    const QDomElement text = firstDescendant(element, QStringLiteral("text"));
    if (!text.isNull())
    {
        result += text.text();
    }
    for (const QDomElement& field : childElements(element, QStringLiteral("field")))
    {
        result += QLatin1Char(' ');
        result += collectValueText(field);
    }
    return result.trimmed();
}

class XfaBinder
{
public:
    explicit XfaBinder(QDomDocument& templateDocument) :
        m_templateDocument(templateDocument)
    {
    }

    void bind(const QDomElement& dataRoot)
    {
        processContents(m_templateDocument.documentElement(), dataRoot, dataRoot);
    }

private:
    QList<QDomElement> resolve(const QDomElement& context, const QDomElement& root, const QString& expression) const
    {
        QString path = expression.trimmed();
        if (path.isEmpty())
        {
            return {};
        }

        QDomElement current;
        if (path.startsWith(QStringLiteral("$record")))
        {
            current = root;
            path.remove(0, 7);
        }
        else if (path.startsWith(QLatin1Char('$')))
        {
            current = context;
            path.remove(0, 1);
        }
        else
        {
            return {};
        }

        if (path.startsWith(QLatin1Char('.')))
        {
            path.remove(0, 1);
        }
        if (path.isEmpty())
        {
            return { current };
        }

        QList<QDomElement> nodes{ current };
        for (const QString& rawSegment : path.split(QLatin1Char('.')))
        {
            if (rawSegment.isEmpty())
            {
                continue;
            }

            const bool repeated = rawSegment.endsWith(QStringLiteral("[*]"));
            const QString name = repeated ? rawSegment.left(rawSegment.size() - 3) : rawSegment;
            if (name.isEmpty())
            {
                continue;
            }

            QList<QDomElement> next;
            for (const QDomElement& node : nodes)
            {
                if (repeated)
                {
                    next.append(childElements(node, name));
                }
                else
                {
                    const QDomElement child = firstChild(node, name);
                    if (!child.isNull())
                    {
                        next.append(child);
                    }
                }
            }
            nodes = std::move(next);
            if (nodes.isEmpty())
            {
                break;
            }
        }
        return nodes;
    }

    void setFieldValue(QDomElement field, const QString& value) const
    {
        QDomElement valueElement = firstChild(field, QStringLiteral("value"));
        if (valueElement.isNull())
        {
            valueElement = m_templateDocument.createElementNS(field.namespaceURI(), QStringLiteral("value"));
            field.appendChild(valueElement);
        }

        for (const QDomElement& text : childElements(valueElement, QStringLiteral("text")))
        {
            valueElement.removeChild(text);
        }

        QDomElement text = m_templateDocument.createElementNS(field.namespaceURI(), QStringLiteral("text"));
        text.appendChild(m_templateDocument.createTextNode(value));
        valueElement.appendChild(text);
    }

    void fillField(QDomElement field, const QDomElement& context, const QDomElement& root) const
    {
        const QDomElement bind = firstChild(field, QStringLiteral("bind"));
        if (bind.isNull() || bind.attribute("match") == QStringLiteral("none"))
        {
            return;
        }

        const QList<QDomElement> nodes = resolve(context, root, bind.attribute("ref"));
        setFieldValue(field, nodes.isEmpty() ? QString() : nodes.first().text().trimmed());
    }

    QList<QDomElement> processSubform(QDomElement subform, const QDomElement& context,
                                      const QDomElement& root) const
    {
        const QDomElement bind = firstChild(subform, QStringLiteral("bind"));
        if (!bind.isNull() && bind.attribute("match") == QStringLiteral("dataRef"))
        {
            const QList<QDomElement> matches = resolve(context, root, bind.attribute("ref"));
            if (matches.isEmpty())
            {
                return {};
            }

            QList<QDomElement> clones;
            clones.reserve(matches.size());
            for (const QDomElement& match : matches)
            {
                QDomElement clone = subform.cloneNode(true).toElement();
                processContents(clone, match, root);
                clones.append(clone);
            }
            return clones;
        }

        processContents(subform, context, root);
        return { subform };
    }

    void processContents(QDomElement node, const QDomElement& context, const QDomElement& root) const
    {
        QDomNode child = node.firstChild();
        while (!child.isNull())
        {
            QDomNode next = child.nextSibling();
            if (!child.isElement())
            {
                child = next;
                continue;
            }

            const QDomElement element = child.toElement();
            const QString name = localName(element);
            if (name == QStringLiteral("field"))
            {
                fillField(element, context, root);
            }
            else if (name == QStringLiteral("subform"))
            {
                const QList<QDomElement> replacements = processSubform(element, context, root);
                if (!(replacements.size() == 1 && replacements.first() == element))
                {
                    QDomNode parent = element.parentNode();
                    parent.removeChild(element);
                    for (const QDomElement& replacement : replacements)
                    {
                        parent.insertBefore(replacement, next);
                    }
                }
            }
            else if (name != QStringLiteral("bind") && name != QStringLiteral("occur") &&
                     name != QStringLiteral("value") && name != QStringLiteral("ui") &&
                     name != QStringLiteral("font") && name != QStringLiteral("margin") &&
                     name != QStringLiteral("para") && name != QStringLiteral("border") &&
                     name != QStringLiteral("caption") && name != QStringLiteral("desc") &&
                     name != QStringLiteral("assist") && name != QStringLiteral("keep") &&
                     name != QStringLiteral("extras"))
            {
                processContents(element, context, root);
            }

            child = next;
        }
    }

    QDomDocument& m_templateDocument;
};

QString fontFamily(const QDomElement& cell)
{
    const QDomElement font = firstDescendant(cell, QStringLiteral("font"));
    if (!font.isNull())
    {
        const QString family = font.attribute("typeface").trimmed();
        if (!family.isEmpty())
        {
            return family;
        }
    }
    return QStringLiteral("Times New Roman");
}

double fontPointSize(const QDomElement& cell)
{
    const QDomElement font = firstDescendant(cell, QStringLiteral("font"));
    if (!font.isNull())
    {
        const QString size = font.attribute("size").trimmed();
        if (!size.isEmpty())
        {
            const double mm = parseMeasurement(size, 0.0);
            if (mm > 0.0)
            {
                // XFA font sizes are usually expressed in points.  The
                // measurement helper treats unit-less values as millimetres,
                // so recover the point count for this particular attribute.
                static const QRegularExpression pointExpression(
                    QStringLiteral(R"(^([+-]?(?:\d+(?:[.,]\d*)?|[.,]\d+))\s*(pt)?$)"));
                const QRegularExpressionMatch match = pointExpression.match(size.trimmed());
                if (match.hasMatch())
                {
                    QString number = match.captured(1);
                    number.replace(QLatin1Char(','), QLatin1Char('.'));
                    bool ok = false;
                    const double points = number.toDouble(&ok);
                    if (ok && points > 0.0)
                    {
                        return points;
                    }
                }
            }
        }
    }
    return 8.0;
}

QMarginsF cellMargins(const QDomElement& cell)
{
    const QDomElement margin = firstDescendant(cell, QStringLiteral("margin"));
    if (margin.isNull())
    {
        return QMarginsF(0.5, 0.5, 0.5, 0.5);
    }
    return QMarginsF(parseMeasurement(margin.attribute("leftInset"), 0.5),
                     parseMeasurement(margin.attribute("topInset"), 0.5),
                     parseMeasurement(margin.attribute("rightInset"), 0.5),
                     parseMeasurement(margin.attribute("bottomInset"), 0.5));
}

double textHeightMm(const QString& text, double widthMm, const QDomElement& cell)
{
    if (text.isEmpty() || widthMm <= 0.0)
    {
        return 0.0;
    }

    QFont font(fontFamily(cell));
    font.setPointSizeF(fontPointSize(cell));

    QFontMetricsF metrics(font);
    const double pixelsPerMm = 96.0 / 25.4;
    const double widthPixels = widthMm * pixelsPerMm;
    const QRectF bounds = metrics.boundingRect(QRectF(0.0, 0.0, widthPixels, 100000.0),
                                               Qt::TextWordWrap | Qt::TextExpandTabs, text);
    return bounds.height() / pixelsPerMm;
}

bool isSingleLineCell(const QDomElement& cell)
{
    const QDomElement textEdit = firstDescendant(cell, QStringLiteral("textEdit"));
    return !textEdit.isNull() && textEdit.attribute("multiLine") == QStringLiteral("0");
}

double cellRequiredHeight(const QDomElement& cell, double widthMm)
{
    const QString text = collectAllText(cell);
    if (text.isEmpty())
    {
        return 0.0;
    }

    const QMarginsF margins = cellMargins(cell);
    const double availableWidth = qMax(1.0, widthMm - margins.left() - margins.right());
    double height = 0.0;
    if (isSingleLineCell(cell))
    {
        QFont font(fontFamily(cell));
        font.setPointSizeF(fontPointSize(cell));
        height = QFontMetricsF(font).height() * 25.4 / 96.0;
    }
    else
    {
        height = textHeightMm(text, availableWidth, cell);
    }
    return height + margins.top() + margins.bottom() + 1.0;
}

QStringList columnWidthTokens(const QDomElement& table)
{
    return table.attribute("columnWidths").split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
}

double rowRequiredHeight(const QDomElement& row,
                         const QVector<double>& columnWidths)
{
    double required = 0.0;
    int column = 0;
    for (QDomNode node = row.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }

        const QDomElement cell = node.toElement();
        const QString name = localName(cell);
        if (name != QStringLiteral("field") && name != QStringLiteral("draw") &&
            name != QStringLiteral("subform"))
        {
            continue;
        }

        int span = qMax(1, cell.attribute("colSpan", QStringLiteral("1")).toInt());
        double width = 0.0;
        if (!columnWidths.isEmpty())
        {
            for (int index = 0; index < span && column + index < columnWidths.size(); ++index)
            {
                width += columnWidths.at(column + index);
            }
        }
        if (width <= 0.0)
        {
            width = parseMeasurement(cell.attribute("w"), 20.0);
        }

        required = qMax(required, cellRequiredHeight(cell, width));
        column += span;
    }

    required = qMax(required, parseMeasurement(row.attribute("h"), 0.0));
    required = qMax(required, parseMeasurement(row.attribute("minH"), 0.0));
    return required;
}

double computeTableRowHeight(const QDomElement& table, const QList<QDomElement>& rows)
{
    QVector<double> widths;
    for (const QString& token : columnWidthTokens(table))
    {
        const double width = parseMeasurement(token, 0.0);
        if (width > 0.0)
        {
            widths.append(width);
        }
    }

    double height = 0.0;
    for (const QDomElement& row : rows)
    {
        height = qMax(height, rowRequiredHeight(row, widths));
    }
    return qBound(10.0, height, 48.0);
}

bool flowLayout(const QDomElement& element)
{
    const QString layout = element.attribute("layout");
    return layout == QStringLiteral("tb") || layout == QStringLiteral("table");
}

double precedingExplicitHeight(const QDomElement& table, double contentHeight)
{
    QDomElement wrapper = table.parentNode().toElement();
    if (wrapper.isNull())
    {
        return 0.0;
    }
    QDomElement flowParent = wrapper.parentNode().toElement();
    if (flowParent.isNull() || !flowLayout(flowParent))
    {
        return 0.0;
    }

    double preceding = 0.0;
    for (QDomNode node = flowParent.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement() || node == wrapper)
        {
            break;
        }
        const QDomElement sibling = node.toElement();
        preceding += qMax(parseMeasurement(sibling.attribute("h"), 0.0),
                          parseMeasurement(sibling.attribute("minH"), 0.0));
    }
    return preceding > contentHeight * 0.45 ? preceding : 0.0;
}

QList<int> rowGroups(int rowCount, double rowHeight, double precedingHeight, double contentHeight)
{
    if (rowCount <= 1)
    {
        return { rowCount };
    }

    if (precedingHeight > contentHeight * 0.45 && rowCount > 3)
    {
        QList<int> groups{ qMin(3, rowCount) };
        int remaining = rowCount - groups.first();
        while (remaining > 0)
        {
            const int chunk = qMin(5, remaining);
            groups.append(chunk);
            remaining -= chunk;
        }
        return groups;
    }

    const double totalHeight = rowHeight * rowCount;
    if (totalHeight > contentHeight * 0.95)
    {
        const int perPage = qMax(1, qFloor((contentHeight - 10.0) / rowHeight));
        QList<int> groups;
        int remaining = rowCount;
        while (remaining > 0)
        {
            const int chunk = qMin(perPage, remaining);
            groups.append(chunk);
            remaining -= chunk;
        }
        return groups;
    }

    return { rowCount };
}

QDomElement cloneTableWithRows(const QDomElement& table, const QList<QDomElement>& rows,
                               double dataRowHeight, double otherRowHeight)
{
    QDomElement clone = table.cloneNode(true).toElement();
    for (const QDomElement& row : childElements(clone, QStringLiteral("subform")))
    {
        clone.removeChild(row);
    }

    for (const QDomElement& sourceRow : rows)
    {
        const bool isDataRow = bindReference(sourceRow).endsWith(QStringLiteral("[*]"));
        const double rowHeight = isDataRow ? dataRowHeight : otherRowHeight;
        QDomElement row = sourceRow.cloneNode(true).toElement();
        row.setAttribute("h", measurementText(rowHeight));
        if (isDataRow)
        {
            for (QDomElement child : childElements(row, QStringLiteral("field")))
            {
                child.setAttribute("h", measurementText(rowHeight));
            }
        }
        clone.appendChild(row);
    }

    return clone;
}

QList<QDomElement> descendantElements(QDomElement root, const QString& name)
{
    QList<QDomElement> result;
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }
        QDomElement element = node.toElement();
        if (localName(element) == name)
        {
            result.append(element);
        }
        result.append(descendantElements(element, name));
    }
    return result;
}

void applyCompactFont(QDomElement row)
{
    for (QDomElement font : descendantElements(row, QStringLiteral("font")))
    {
        font.setAttribute("size", QStringLiteral("7pt"));
    }
    for (QDomElement field : childElements(row, QStringLiteral("field")))
    {
        QDomElement font = firstChild(field, QStringLiteral("font"));
        if (font.isNull())
        {
            font = field.ownerDocument().createElementNS(field.namespaceURI(), QStringLiteral("font"));
            field.appendChild(font);
        }
        font.setAttribute("size", QStringLiteral("7pt"));
    }
}

double signatureTableHeight(const QDomElement& table)
{
    double height = 0.0;
    for (const QDomElement& row : childElements(table, QStringLiteral("subform")))
    {
        height += parseMeasurement(row.attribute("h"), 8.0);
    }
    return height;
}

void shiftFollowingAbsoluteBlocks(QDomElement table)
{
    QDomElement wrapper = table.parentNode().toElement();
    QDomElement flow = wrapper.parentNode().toElement();
    if (flow.isNull())
    {
        return;
    }

    const double offset = qMax(40.0, signatureTableHeight(table) + 15.0);
    bool afterSignature = false;
    for (QDomNode node = flow.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }
        if (node == wrapper)
        {
            afterSignature = true;
            continue;
        }
        if (!afterSignature)
        {
            continue;
        }

        QDomElement sibling = node.toElement();
        if (localName(sibling) != QStringLiteral("subform") || !sibling.attribute("layout").isEmpty())
        {
            continue;
        }

        for (QDomElement child : descendantElements(sibling, QStringLiteral("draw")))
        {
            child.setAttribute("y", measurementText(parseMeasurement(child.attribute("y"), 0.0) + offset));
        }
        for (QDomElement child : descendantElements(sibling, QStringLiteral("field")))
        {
            child.setAttribute("y", measurementText(parseMeasurement(child.attribute("y"), 0.0) + offset));
        }
    }
}

bool isSignatureTable(const QDomElement& table)
{
    if (table.attribute("name") == QStringLiteral("Table11"))
    {
        return true;
    }

    const QString reference = bindReference(table);
    if (reference.endsWith(QStringLiteral(".SIGN")) || reference == QStringLiteral("$.SIGN"))
    {
        return true;
    }

    for (const QDomElement& field : childElements(table, QStringLiteral("subform")))
    {
        if (bindReference(field).contains(QStringLiteral(".SIGN")))
        {
            return true;
        }
    }

    const QString text = collectAllText(table);
    return text.contains(QStringLiteral("подпись"), Qt::CaseInsensitive) &&
           text.contains(QStringLiteral("расшифровка"), Qt::CaseInsensitive);
}

void addPageBreak(QDomElement element)
{
    for (const QDomElement& existing : childElements(element, QStringLiteral("breakBefore")))
    {
        if (existing.attribute("targetType") == QStringLiteral("pageArea"))
        {
            return;
        }
    }
    QDomElement pageBreak = element.ownerDocument().createElementNS(
        element.namespaceURI().isEmpty() ? QString::fromLatin1(defaultTemplateNamespace) : element.namespaceURI(),
        QStringLiteral("breakBefore"));
    pageBreak.setAttribute("targetType", QStringLiteral("pageArea"));
    element.appendChild(pageBreak);
}

void paginateTables(QDomDocument& document, double contentHeight)
{
    const QDomNodeList allSubforms = document.elementsByTagName(QStringLiteral("subform"));
    QList<QDomElement> tables;
    for (int index = 0; index < allSubforms.count(); ++index)
    {
        const QDomElement element = allSubforms.at(index).toElement();
        if (!element.isNull() && element.attribute("layout") == QStringLiteral("table") &&
            hasDataRows(element))
        {
            tables.append(element);
        }
    }
    for (const QDomElement& table : tables)
    {
        if (table.parentNode().isNull())
        {
            continue;
        }

        const QList<QDomElement> rows = childElements(table, QStringLiteral("subform"));
        QList<QDomElement> dataRows;
        for (const QDomElement& row : rows)
        {
            if (bindReference(row).endsWith(QStringLiteral("[*]")))
            {
                dataRows.append(row);
            }
        }
        if (dataRows.isEmpty())
        {
            continue;
        }

        const double precedingHeight = precedingExplicitHeight(table, contentHeight);
        const bool compact = precedingHeight > contentHeight * 0.45 && dataRows.size() > 3;
        const double rowHeight = compact ? 14.0 : computeTableRowHeight(table, dataRows);

        for (QDomElement row : dataRows)
        {
            if (compact)
            {
                applyCompactFont(row);
            }
            row.setAttribute("h", measurementText(rowHeight));
            for (QDomElement field : childElements(row, QStringLiteral("field")))
            {
                field.setAttribute("h", measurementText(rowHeight));
            }
        }

        const QList<int> groups = rowGroups(dataRows.size(), rowHeight, precedingHeight, contentHeight);
        if (groups.size() <= 1)
        {
            continue;
        }

        int firstDataRow = rows.indexOf(dataRows.first());
        int lastDataRow = rows.indexOf(dataRows.last());
        QList<QDomElement> headers;
        QList<QDomElement> footers;
        for (int index = 0; index < rows.size(); ++index)
        {
            if (index < firstDataRow)
            {
                headers.append(rows.at(index));
            }
            else if (index > lastDataRow)
            {
                footers.append(rows.at(index));
            }
        }

        QDomElement parent = table.parentNode().toElement();
        int tableIndex = 0;
        for (QDomNode node = table.previousSibling(); !node.isNull(); node = node.previousSibling())
        {
            ++tableIndex;
        }

        parent.removeChild(table);
        int cursor = 0;
        int insertionIndex = tableIndex;
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
        {
            QList<QDomElement> chunkRows;
            for (const QDomElement& header : headers)
            {
                chunkRows.append(header);
            }
            const int count = groups.at(groupIndex);
            for (int offset = 0; offset < count; ++offset)
            {
                chunkRows.append(dataRows.at(cursor + offset));
            }
            cursor += count;
            if (groupIndex == groups.size() - 1)
            {
                for (const QDomElement& footer : footers)
                {
                    chunkRows.append(footer);
                }
            }

            QDomElement chunk = cloneTableWithRows(table, chunkRows, rowHeight,
                                                   qMax(rowHeight, 20.0));
            if (groupIndex > 0)
            {
                addPageBreak(chunk);
            }
            if (insertionIndex < parent.childNodes().count())
            {
                parent.insertBefore(chunk, parent.childNodes().at(insertionIndex));
            }
            else
            {
                parent.appendChild(chunk);
            }
            ++insertionIndex;
        }
    }
}

void addSignaturePageBreaks(QDomDocument& document)
{
    const QDomNodeList allSubforms = document.elementsByTagName(QStringLiteral("subform"));
    QList<QDomElement> tables;
    for (int index = 0; index < allSubforms.count(); ++index)
    {
        const QDomElement element = allSubforms.at(index).toElement();
        if (!element.isNull() && element.attribute("layout") == QStringLiteral("table") &&
            isSignatureTable(element))
        {
            tables.append(element);
        }
    }

    for (const QDomElement& table : tables)
    {
        QDomElement wrapper = table.parentNode().toElement();
        if (wrapper.isNull())
        {
            continue;
        }
        addPageBreak(wrapper);
        shiftFollowingAbsoluteBlocks(table);
    }
}

QDomElement firstElementChild(const QDomElement& parent)
{
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (node.isElement())
        {
            return node.toElement();
        }
    }
    return {};
}

QDomElement findXfaDataElement(const QDomElement& element)
{
    if (localName(element) == QStringLiteral("data") &&
        element.namespaceURI() == QString::fromLatin1(xfaDataNamespace))
    {
        return element;
    }

    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }
        const QDomElement result = findXfaDataElement(node.toElement());
        if (!result.isNull())
        {
            return result;
        }
    }
    return {};
}

QDomElement findLegacyInstanceData(const QDomElement& element)
{
    if (localName(element) == QStringLiteral("data") &&
        element.namespaceURI() != QString::fromLatin1(xfaDataNamespace))
    {
        return element;
    }

    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        if (!node.isElement())
        {
            continue;
        }

        // dataDescription repeats the record schema (often as an empty
        // template) and must never be mistaken for the instance data.
        if (localName(node.toElement()) == QStringLiteral("dataDescription"))
        {
            continue;
        }

        const QDomElement result = findLegacyInstanceData(node.toElement());
        if (!result.isNull())
        {
            return result;
        }
    }
    return {};
}

QDomElement findInstanceData(QDomDocument& dataDocument)
{
    const QDomElement documentElement = dataDocument.documentElement();

    // The standard XFA datasets packet is
    //   <xfa:datasets><xfa:data><record>...</record></xfa:data></xfa:datasets>
    // where <record> stands for the form root.  $record and the relative
    // dataRefs in the template are evaluated against that record element, not
    // against <xfa:data> itself.  Many LiveCycle exports also insert an extra
    // unqualified <data> wrapper, which is simply the record root here.
    const QDomElement data = findXfaDataElement(documentElement);
    if (!data.isNull())
    {
        const QDomElement record = firstElementChild(data);
        return record.isNull() ? data : record;
    }

    // Older exports (and the bundled fixtures) may omit the xfa:data wrapper.
    return findLegacyInstanceData(documentElement);
}

const pdf::PDFObject& resolveObject(const pdf::PDFObjectStorage& storage, const pdf::PDFObject& object)
{
    return object.isReference() ? storage.getObject(object.getReference()) : object;
}

bool readPacket(const pdf::PDFObjectStorage& storage, const pdf::PDFObject& xfaArray,
                const QByteArray& packetName, pdf::PDFObjectReference* reference, QByteArray* data)
{
    if (!xfaArray.isArray())
    {
        return false;
    }

    const pdf::PDFArray* array = xfaArray.getArray();
    for (size_t index = 0; index + 1 < array->getCount(); index += 2)
    {
        const pdf::PDFObject& label = resolveObject(storage, array->getItem(index));
        if (!label.isString() || label.getString() != packetName)
        {
            continue;
        }

        const pdf::PDFObject& item = array->getItem(index + 1);
        if (!item.isReference())
        {
            return false;
        }

        const pdf::PDFObject& streamObject = resolveObject(storage, item);
        if (!streamObject.isStream() || !streamObject.getStream())
        {
            return false;
        }

        if (reference)
        {
            *reference = item.getReference();
        }
        if (data)
        {
            *data = pdf::PDFStreamFilterStorage::getDecodedStream(
                streamObject.getStream(), storage.getSecurityHandler());
        }
        return true;
    }
    return false;
}
}   // namespace

QByteArray preprocessXfaTemplate(const QByteArray& templateData,
                                 const QByteArray& datasetsData)
{
    QDomDocument templateDocument;
    QDomDocument datasetsDocument;
    const auto templateResult = templateDocument.setContent(
        templateData, QDomDocument::ParseOption::UseNamespaceProcessing);
    const auto datasetsResult = datasetsDocument.setContent(
        datasetsData, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!templateResult || !datasetsResult)
    {
        return {};
    }

    const QDomElement instance = findInstanceData(datasetsDocument);
    if (instance.isNull())
    {
        return {};
    }

    XfaBinder binder(templateDocument);
    binder.bind(instance);

    paginateTables(templateDocument, contentAreaHeight(templateDocument));
    addSignaturePageBreaks(templateDocument);
    return templateDocument.toByteArray(2);
}

pdf::PDFDocumentPointer preprocessXfaDocument(const pdf::PDFDocumentPointer& document)
{
    if (!document)
    {
        return document;
    }

    const pdf::PDFObjectStorage& storage = document->getStorage();
    const pdf::PDFDictionary* formDictionary =
        storage.getDictionaryFromObject(document->getCatalog()->getFormObject());
    if (!formDictionary)
    {
        return document;
    }

    const pdf::PDFObject& xfa = storage.getObject(formDictionary->get("XFA"));
    pdf::PDFObjectReference templateReference;
    QByteArray templateData;
    QByteArray datasetsData;
    if (!readPacket(storage, xfa, QByteArrayLiteral("template"), &templateReference, &templateData) ||
        !readPacket(storage, xfa, QByteArrayLiteral("datasets"), nullptr, &datasetsData))
    {
        return document;
    }

    const QByteArray modifiedData = preprocessXfaTemplate(templateData, datasetsData);
    if (modifiedData.isEmpty())
    {
        return document;
    }

    pdf::PDFDocumentModifier modifier(document.data());
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"),
                        pdf::PDFObject::createInteger(modifiedData.size()));
    modifier.getBuilder()->setObject(
        templateReference,
        pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(modifiedData))));
    if (!modifier.finalize())
    {
        return document;
    }
    return modifier.getDocument();
}
