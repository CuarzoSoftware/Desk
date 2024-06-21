#include <QFileIconProvider>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QStyle>
#include "Item.h"
#include "Desktop.h"

Item::Item(Desktop &desktop, const QFileInfo &info) :
    m_desktop(desktop),
    m_info(info)
{
    setFixedSize(112, 112);
    setParent(&desktop);
    setLayout(&layout);

    QFileIconProvider provider;
    QIcon icoFallack { provider.icon(m_info) };

    QIcon ico = QIcon::fromTheme(QMimeDatabase().mimeTypeForFile(m_info).iconName(), icoFallack);
    qDebug() << QMimeDatabase().mimeTypeForFile(m_info).iconName();

    icon.setPixmap(ico.pixmap(128, 128));
    icon.setFixedSize(68, 68);
    icon.setScaledContents(true);

    QFontMetrics metrics(label.font());
    QString elidedText = metrics.elidedText(m_info.fileName(), Qt::ElideMiddle, 80);

    labelLayout.setContentsMargins(4, 2, 4, 2);
    labelContainer.setLayout(&labelLayout);
    labelLayout.addWidget(&label, 0, Qt::AlignCenter);
    label.setText(elidedText);
    label.setProperty("class", "item-label");

    layout.addWidget(&icon);
    layout.addWidget(&labelContainer);
    layout.setAlignment(&icon, Qt::AlignCenter);
    layout.setAlignment(&labelContainer, Qt::AlignCenter);

    icon.setGraphicsEffect(&iconShadow);
    label.setGraphicsEffect(&labelShadow);

    iconShadow.setBlurRadius(20);
    iconShadow.setOffset(0, 3);
    iconShadow.setColor(QColor(0, 0, 0, 40));

    labelShadow.setBlurRadius(16);
    labelShadow.setOffset(0, 0);
    labelShadow.setColor(Qt::black);
    update();
}

void Item::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        pressed = true;
        hotspot = event->pos();
        labelContainer.setProperty("class", "selected");
        labelContainer.style()->unpolish(&labelContainer);
        labelContainer.style()->polish(&labelContainer);
    }
}

void Item::mouseReleaseEvent(QMouseEvent *event)
{
    pressed = false;
}

void Item::mouseMoveEvent(QMouseEvent *event)
{
    if (pressed)
    {
        QDrag *drag = new QDrag(this);
        QMimeData *mimeData = new QMimeData;
        mimeData->setText("Hola");
        drag->setMimeData(mimeData);

        setPalette(Qt::transparent);
        setAutoFillBackground(true);
        drag->setPixmap(grab(QRect(0, 0, 112, 112)));
        drag->setHotSpot(event->pos());
        Qt::DropAction dropAction = drag->exec();
    }

}

void Item::leaveEvent(QEvent *event)
{
    pressed = false;
}
