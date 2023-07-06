#include <QSocketNotifier>
#include "Shared.h"
#include "Bus.h"
#include "Core.h"
#include "Desktop.h"

Bus::Bus(QObject *parent) : QObject{parent} {}

Bus::~Bus()
{
    if (notifier)
        delete notifier;
}

bool Bus::start()
{
    int r;

    r = sd_bus_open_system(&bus);

    if (r < 0)
    {
        fprintf(stderr, "[bus] Failed to connect to the system bus: %s\n", strerror(-r));
        goto finish;
    }

    r = sd_bus_request_name(bus, "com.cuarzo.Desk", 0);

    if (r < 0)
    {
        fprintf(stderr, "[bus] Failed to acquire service name: %s\n", strerror(-r));
        goto finish;
    }

    static const sd_bus_vtable vtable[] =
    {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("ShowDesktop", "s", "", &ShowDesktop, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
    };

    r = sd_bus_add_object_vtable(bus,
                                 &slot,
                                 "/com/cuarzo/Desktop",
                                 "com.cuarzo.Desktop",
                                 vtable,
                                 NULL);
    if (r < 0)
    {
        fprintf(stderr, "[bus] Failed to add object vtable: %s\n", strerror(-r));
        goto finish;
    }


    notifier = new QSocketNotifier(sd_bus_get_fd(bus), QSocketNotifier::Read, this);

    // Handle requests
    connect(notifier, &QSocketNotifier::activated, [this]
    {
        int r;

        while (1)
        {
            r = sd_bus_process(bus, NULL);

            if (r < 0)
            {
                fprintf(stderr, "[bus] Failed to process bus: %s\n", strerror(-r));
                exit(1);
            }

            if (r > 0) /* We processed a request, continue processing */
                continue;

            break;
        }
    });

    return true;

finish:
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);
    return false;
}

int Bus::ShowDesktop(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    Q_UNUSED(ret_error);

    const char *input;
    int r;

    r = sd_bus_message_read(msg, "s", &input);

    if (r < 0)
    {
        fprintf(stderr, "Failed to read input: %s\n", strerror(-r));
        return r;
    }

    printf("Message: %s\n", input);

    for (Desktop *desktop : CORE()->desktops)
        desktop->login();

    return 0;
}
