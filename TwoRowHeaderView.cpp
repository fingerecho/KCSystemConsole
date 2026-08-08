#include "TwoRowHeaderView.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionHeader>
#include <QStyle>

TwoRowHeaderView::TwoRowHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    m_rowHeight = fontMetrics().height() + 10;
    setSectionsClickable(true);
}

void TwoRowHeaderView::setTopLabels(const QStringList &labels)
{
    m_topLabels = labels;
    viewport()->update();
}

QSize TwoRowHeaderView::sizeHint() const
{
    QSize base = QHeaderView::sizeHint();
    base.setHeight(m_rowHeight * 2);
    return base;
}

void TwoRowHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!rect.isValid())
        return;

    painter->save();

    // Draw the entire section as ONE header cell (borders drawn only once).
    // Suppress the default sort indicator – we draw our own in the top row.
    QStyleOptionHeader opt;
    opt.initFrom(this);
    opt.section = logicalIndex;
    opt.orientation = orientation();
    opt.rect = rect;
    opt.text = QString();
    opt.sortIndicator = QStyleOptionHeader::None;
    style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    const QRect topRect(rect.x(), rect.y(), rect.width(), m_rowHeight);
    const QRect bottomRect(rect.x(), rect.y() + m_rowHeight,
                           rect.width(), rect.height() - m_rowHeight);

    const QColor textColor = palette().color(QPalette::ButtonText);

    // ---- Top row: summary values (smaller font) + sort indicator ----
    const bool isSorted = (sortIndicatorSection() == logicalIndex);
    const bool ascending = (sortIndicatorOrder() == Qt::AscendingOrder);

    QRect topTextRect = topRect;

    if (isSorted) {
        // Draw ▲/▼ triangle on the left
        const int indicatorW = 14;
        const QRect indRect(topRect.x() + 5, topRect.y(), indicatorW, topRect.height());
        const int cx = indRect.center().x();
        const int cy = indRect.center().y();
        const int sz = 4;

        QPainterPath path;
        if (ascending) {
            path.moveTo(cx, cy - sz);
            path.lineTo(cx + sz, cy + sz);
            path.lineTo(cx - sz, cy + sz);
        } else {
            path.moveTo(cx, cy + sz);
            path.lineTo(cx + sz, cy - sz);
            path.lineTo(cx - sz, cy - sz);
        }
        path.closeSubpath();
        painter->setBrush(textColor);
        painter->setPen(Qt::NoPen);
        painter->drawPath(path);
        painter->setPen(textColor);

        topTextRect.setLeft(indRect.right() + 2);
    }

    QFont smallFont = painter->font();
    smallFont.setPointSize(qMax(smallFont.pointSize() - 1, 7));
    painter->setFont(smallFont);
    painter->setPen(textColor);
    {
        const QString s = (logicalIndex < m_topLabels.size())
                              ? m_topLabels.at(logicalIndex) : QString();
        painter->drawText(topTextRect, Qt::AlignCenter, s);
    }

    // ---- Bottom row: column labels (normal font) ----
    painter->setFont(font());
    painter->setPen(textColor);
    {
        const QString s = model()
                              ? model()->headerData(logicalIndex, orientation()).toString()
                              : QString();
        painter->drawText(bottomRect, Qt::AlignCenter, s);
    }

    painter->restore();
}
