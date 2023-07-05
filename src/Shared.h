#ifndef SHARED_H
#define SHARED_H

#include <QPixmap>

class Core;
class Bus;

struct Shared
{
};

Core *CORE();
Bus *BUS();
Shared *DATA();
bool SETUP(Core *core);

#endif // SHARED_H
