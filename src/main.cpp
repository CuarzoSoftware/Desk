#include <Core/Desk.h>

int main()
{
    Desk desk;

    if (!desk.marco)
        return 1;

    return desk.marco->run();
}
