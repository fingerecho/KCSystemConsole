#pragma once

#include <QHeaderView>
#include <QStringList>

class TwoRowHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit TwoRowHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setTopLabels(const QStringList &labels);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    QSize sizeHint() const override;

private:
    QStringList m_topLabels;
    int m_rowHeight;
};
