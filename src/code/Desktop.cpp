#include <QScreen>
#include <QPainter>
#include <QTimer>
#include <QPropertyAnimation>
#include "Desktop.h"
#include "Shared.h"
#include "Core.h"

static int initBlurRadius = 128;
static int blurFrames = 10;

Desktop::Desktop(QScreen *screen, QWidget *parent) : QWidget(parent)
{
    setWindowTitle("DESK");
    setWindowFlags(Qt::FramelessWindowHint);

    this->screen = screen;

    connect(screen, &QScreen::destroyed, [this]
    {
        delete this;
    });

    connect(screen, &QScreen::geometryChanged, [this]
    {
        updateDimensions();
    });

    wallpaperLabel.setGraphicsEffect(&wallpaperBlur);
    wallpaperLabel.setScaledContents(true);
    wallpaperBlur.setEnabled(true);
    wallpaperBlur.setBlurRadius(initBlurRadius);
    wallpaperBlur.setBlurHints(QGraphicsBlurEffect::QualityHint);
    setProperty("currentBlurFrame", 0);

    updateDimensions();
    setScreen(screen);
    showFullScreen();
}

Desktop::~Desktop()
{
    CORE()->desktops.erase(coreLink);
}

QT_BEGIN_NAMESPACE
  extern Q_WIDGETS_EXPORT void qt_blurImage( QPainter *p, QImage &blurImage, qreal radius, bool quality, bool alphaOnly, int transposed = 0 );
QT_END_NAMESPACE

void Desktop::updateDimensions()
{
    wallpaperLabel.move(-initBlurRadius, -initBlurRadius);
    wallpaperLabel.setFixedSize(screen->size() + 2*QSize(initBlurRadius, initBlurRadius));

    QPixmap wallpaper;
    if (wallpaper.load("/System/Resources/Wallpapers/Default.jpg"))
    {
        QSize screenSize = screen->size()*screen->devicePixelRatio();
        qreal hScaled = (qreal)(screenSize.width()*wallpaper.height())/(qreal)wallpaper.width();

        if (hScaled > screenSize.height())
        {
            wallpaper = wallpaper.scaledToWidth(screenSize.width(), Qt::SmoothTransformation);
            int hClip = (hScaled - screenSize.height())/2;
            wallpaper = wallpaper.copy(0, hClip, screenSize.width(), screenSize.height());
        }
        else
        {
            wallpaper = wallpaper.scaledToHeight(screenSize.height(), Qt::SmoothTransformation);
            int wClip = (wallpaper.width() - screenSize.width())/2;
            wallpaper = wallpaper.copy(wClip, 0, screenSize.width(), screenSize.height());
        }

        wallpaper.setDevicePixelRatio(screen->devicePixelRatio());

        wallpaperLabel.setPixmap(wallpaper);
        resize(screen->size());

        int prevBlur = -1;
        for (int i = 0; i < blurFrames; i++)
        {
            qreal easing = (((qreal)blurFrames-(qreal)i)/(qreal)blurFrames);
            easing *= easing;
            qreal blur = initBlurRadius * easing;

            if (prevBlur == (int)blur)
                continue;

            prevBlur = blur;
            blurAnimationFrames.push_back(QPixmap(screenSize));
            QPixmap &pix = blurAnimationFrames.last();
            pix.setDevicePixelRatio(screen->devicePixelRatio());

            wallpaperLabel.move(-blur, -blur);
            wallpaperLabel.setFixedSize(screen->size() + 2*QSize(blur, blur));
            wallpaperBlur.setBlurRadius(blur);

            qDebug() << blur;
            QPainter p(&pix);
            render(&p);
            p.end();
        }

        wallpaperLabel.move(0, 0);
        wallpaperLabel.setFixedSize(screen->size());
        wallpaperLabel.setPixmap(blurAnimationFrames.front());
        wallpaperBlur.setEnabled(false);
    }
}

void Desktop::login()
{
    QPropertyAnimation *animation = new QPropertyAnimation(this, "currentBlurFrame");
    animation->setStartValue(1);
    animation->setEndValue(blurAnimationFrames.length()-1);
    animation->setDuration(200);
    animation->setEasingCurve(QEasingCurve::Linear);
    animation->start(QPropertyAnimation::DeleteWhenStopped);

    int *frame = new int;
    *frame = 0;
    connect(animation, &QPropertyAnimation::valueChanged, [this, animation, frame]
    {
        int currentFrame = animation->currentValue().toUInt();

        if (currentFrame != *frame && !blurAnimationFrames.empty())
        {
            wallpaperLabel.setPixmap(blurAnimationFrames[currentFrame]);
            repaint();
            *frame = currentFrame;
        }

    });

    connect(animation, &QPropertyAnimation::destroyed, [this, frame]
    {
        delete frame;
        blurAnimationFrames.clear();
    });
}

