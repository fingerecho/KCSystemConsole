#include "TwoRowHeaderView.h"

#include <QPainter>
#include <QStyleOptionHeader>

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
    base.setHeight(m_rowHeight * 2 + 1); // two rows + 1px separator
    return base;
}

void TwoRowHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!rect.isValid())
        return;

    painter->save();

    // Build a base style option from this widget
    QStyleOptionHeader opt;
    opt.initFrom(this);
    opt.section = logicalIndex;
    opt.orientation = orientation();

    const QRect topRect(rect.x(), rect.y(), rect.width(), m_rowHeight);
    const QRect sepRect(rect.x(), rect.y() + m_rowHeight, rect.width(), 1);
    const QRect bottomRect(rect.x(), rect.y() + m_rowHeight + 1,
                           rect.width(), rect.height() - m_rowHeight - 1);

    // ---- Top row: summary values ----
    opt.rect = topRect;
    opt.text = (logicalIndex < m_topLabels.size()) ? m_topLabels.at(logicalIndex) : QString();
    opt.textAlignment = Qt::AlignCenter;

    // Slightly lighter background to distinguish from bottom row
    const QColor baseColor = opt.palette.color(QPalette::Button);
    opt.palette.setColor(QPalette::Button, baseColor.lighter(110));

    // Smaller font for summary values
    QFont smallFont = painter->font();
    smallFont.setPointSize(smallFont.pointSize() - 1);
    painter->setFont(smallFont);

    style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    // ---- Separator line ----
    painter->fillRect(sepRect, opt.palette.color(QPalette::Mid));

    // ---- Bottom row: column labels (from model) ----
    painter->setFont(font()); // restore normal font
    opt.rect = bottomRect;
    opt.text = model() ? model()->headerData(logicalIndex, orientation()).toString() : QString();
    opt.textAlignment = Qt::AlignCenter;
    opt.palette.setColor(QPalette::Button, baseColor);

    style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    painter->restore();
}
