#include <Core/Log.h>

using namespace CZ;

const CZ::CZLogger &DeskLogger() noexcept
{
    static CZLogger logger { "Desk", "CZ_DESK_LOG_LEVEL" };
    return logger;
}
