QT += core gui widgets
CONFIG += c++17
TARGET = Desk
target.path = /usr/bin
INSTALLS += target

LIBS += -lsystemd

INCLUDEPATH += /usr/include/systemd

SOURCES += \
    code/Bus.cpp \
    code/Core.cpp \
    code/Shared.cpp \
    code/main.cpp \
    code/Desktop.cpp

HEADERS += \
    code/Bus.h \
    code/Core.h \
    code/Desktop.h \
    code/Shared.h
