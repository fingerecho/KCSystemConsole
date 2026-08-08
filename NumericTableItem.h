#pragma once

#include <QTableWidgetItem>

class NumericTableItem : public QTableWidgetItem
{
public:
    explicit NumericTableItem(double sortKey = 0.0)
        : QTableWidgetItem(), m_sortKey(sortKey) {}

    void setSortKey(double key) { m_sortKey = key; }
    double sortKey() const { return m_sortKey; }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const auto *n = dynamic_cast<const NumericTableItem *>(&other);
        if (n)
            return m_sortKey < n->m_sortKey;
        return QTableWidgetItem::operator<(other);
    }

private:
    double m_sortKey;
};
