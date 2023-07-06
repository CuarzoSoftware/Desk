#ifndef CORE_H
#define CORE_H

#include <QApplication>
#include <QList>

#include "Bus.h"

class Desktop;

enum USER_STATE
{
    USER_STATE_UNLOGGED,
    USER_STATE_LOGGING,
    USER_STATE_UNLOGGING,
    USER_STATE_LOGGED
};

class Core : public QApplication
{
    Q_OBJECT
public:
    Core(int argc, char *argv[]);

    Bus bus = Bus(this);
    QList<Desktop*>desktops;
    USER_STATE state = USER_STATE_UNLOGGED;
};

#endif // CORE_H
