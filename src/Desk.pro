QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

LIBS += -lsystemd

INCLUDEPATH += /usr/include/systemd

SOURCES += \
    Bus.cpp \
    Core.cpp \
    Shared.cpp \
    main.cpp \
    Desktop.cpp

HEADERS += \
    Bus.h \
    Core.h \
    Desktop.h \
    Shared.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
