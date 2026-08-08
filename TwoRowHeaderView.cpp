#include "TwoRowHeaderView.h"

#include <QPainter>
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

    // Draw the entire section as ONE header cell (borders drawn only once)
    QStyleOptionHeader opt;
    opt.initFrom(this);
    opt.section = logicalIndex;
    opt.orientation = orientation();
    opt.rect = rect;
    opt.text = QString(); // no built-in text – we overlay our own
    style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    // Overlay two rows of text
    const QRect topRect(rect.x(), rect.y(), rect.width(), m_rowHeight);
    const QRect bottomRect(rect.x(), rect.y() + m_rowHeight,
                           rect.width(), rect.height() - m_rowHeight);

    const QColor textColor = palette().color(QPalette::ButtonText);

    // ---- Top row: summary values (smaller font) ----
    QFont smallFont = painter->font();
    smallFont.setPointSize(qMax(smallFont.pointSize() - 1, 7));
    painter->setFont(smallFont);
    painter->setPen(textColor);
    {
        const QString s = (logicalIndex < m_topLabels.size())
                              ? m_topLabels.at(logicalIndex) : QString();
        painter->drawText(topRect, Qt::AlignCenter, s);
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
