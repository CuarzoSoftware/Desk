#include <LayerShellQt/Shell>
#include <QStandardPaths>
#include <QFileIconProvider>
#include <QDir>
#include <QDebug>
#include "Desktop.h"
#include "Item.h"

using namespace LayerShellQt;

Desktop::Desktop() : QWidget()
{
    setPalette(Qt::transparent);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_TranslucentBackground);
    show(); hide();

    auto *win = LayerShellQt::Window::get(windowHandle());
    win->setExclusiveZone(-1);
    win->setLayer(Window::LayerBackground);
    win->setAnchors(QFlag(Window::AnchorLeft | Window::AnchorTop | Window::AnchorRight | Window::AnchorBottom));

    QStringList desktopPath { QStandardPaths::standardLocations(QStandardPaths::DesktopLocation) };

    if (desktopPath.empty())
    {
        qFatal() << "Could not get desktop path.";
        exit(1);
    }

    QDir dir(desktopPath[0]);

    for (const QFileInfo &file : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot))
        m_items.emplaceBack(new Item(*this, file));

    show();
}

Desktop::~Desktop()
{

}

void Desktop::updateItemsPos()
{
    int marginTop { 40 };
    int padding { 16 };
    int x { 1 };
    int y { 0 };

    for (auto *item : m_items)
    {
        item->move(width() - (item->width() + padding) * x, y * (item->height() + padding) + marginTop);

        if ((y + 1) * (item->height() + padding * 2) + marginTop > height())
        {
            y = 0;
            x++;
        }
        else
            y++;
    }
}

void Desktop::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateItemsPos();
}
