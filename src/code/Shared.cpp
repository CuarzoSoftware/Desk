#include "Shared.h"
#include "Core.h"

static Core *core;
static Shared *data;

Core *CORE()
{
    return core;
}

Bus *BUS()
{
    return &core->bus;
}

bool SETUP(Core *c)
{
    core = c;
    data = new Shared();
    return true;
}

Shared *DATA()
{
    return data;
}
