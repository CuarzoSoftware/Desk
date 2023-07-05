#ifndef DESKTOP_H
#define DESKTOP_H

#include <QWidget>
#include <QList>
#include <QGraphicsBlurEffect>
#include <QLabel>

class Desktop : public QWidget
{
    Q_OBJECT

public:
    Desktop(QScreen *screen, QWidget *parent = nullptr);
    ~Desktop();
    void updateDimensions();
    void login();

    QList<Desktop*>::iterator coreLink;
    QScreen *screen;
    QLabel wallpaperLabel = QLabel(this);
    QGraphicsBlurEffect wallpaperBlur;
    QList<QPixmap>blurAnimationFrames;
};
#endif // DESKTOP_H
