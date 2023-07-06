#include <QScreen>
#include "Shared.h"
#include "Core.h"
#include "Desktop.h"

Core::Core(int argc, char *argv[]) : QApplication(argc, argv)
{
    if (!SETUP(this))
    {
        qDebug() << "[core] Init setup failed.";
        exit(1);
    }

    if (!bus.start())
    {
        qDebug() << "[core] Failed to start bus service.";
        exit(1);
    }

    for (QScreen *screen : screens())
    {
        desktops.push_back(new Desktop(screen));
    }
}
