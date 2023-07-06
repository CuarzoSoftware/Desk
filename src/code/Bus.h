#ifndef BUS_H
#define BUS_H

#include <sd-bus.h>
#include <QObject>

class QSocketNotifier;

class Bus : public QObject
{
    Q_OBJECT
public:
    explicit Bus(QObject *parent = nullptr);
    ~Bus();
    bool start();

    // Interface
    static int ShowDesktop(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error);
private:
    QSocketNotifier *notifier = nullptr;
    sd_bus_slot *slot = NULL;
    sd_bus *bus = NULL;
};

#endif // BUS_H
