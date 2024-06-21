#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#include <QApplication>

#include "Desktop.h"

int main(int argc, char **argv)
{
    qputenv("QT_ASSUME_STDERR_HAS_CONSOLE", "1");
    LayerShellQt::Shell::useLayerShell();
    QApplication app(argc, argv);

    QIcon::setThemeName("WhiteSur");
    QIcon::setFallbackThemeName("Hicolor");

    app.setStyleSheet(".item-label{text-align:center; font-weight:600; color:white;} .selected{border-radius: 5px; background-color:rgba(3, 111, 252, 0.8);}");

    Desktop desktop;
    app.exec();
	return 0;
}
