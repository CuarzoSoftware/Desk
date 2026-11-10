#include <cstdlib>
#include <Core/Desk.h>

int main()
{
    // Apps launched from here (Qt ones) use the Cuarzo platform theme plugin.
    setenv("QT_QPA_PLATFORMTHEME", "cuarzo", 1);

    Desk desk;

    if (!desk.marco)
        return 1;

    return desk.marco->run();
}
