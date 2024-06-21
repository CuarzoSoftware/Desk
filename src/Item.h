#ifndef ITEM_H
#define ITEM_H

#include <QLabel>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>

class Desktop;

class Item : public QFrame
{
public:
    Item(Desktop &desktop, const QFileInfo &info);

    Desktop &m_desktop;
    QLabel icon;
    QVBoxLayout layout;
    QFileInfo m_info;

    QLabel label;
    QFrame labelContainer;
    QVBoxLayout labelLayout;

    QGraphicsDropShadowEffect iconShadow {this};
    QGraphicsDropShadowEffect labelShadow {this};

    /* Drag & drop */
    QPoint hotspot;
    bool pressed { false };


    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
};

#endif // ITEM_H
