#ifndef DESKTOP_H
#define DESKTOP_H

#include <QWidget>
#include <QDrag>
#include <QMimeData>

class Item;

class Desktop : public QWidget
{
public:

    Desktop();
    ~Desktop();
    void updateItemsPos();

private:
    void resizeEvent(QResizeEvent *) override;
    QVector<Item*> m_items;
};

#endif // DESKTOP_H
