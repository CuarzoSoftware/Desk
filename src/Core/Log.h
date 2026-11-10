#ifndef CZ_DESKLOG_H
#define CZ_DESKLOG_H

#include <CZ/Core/CZLogger.h>

#define Log DeskLogger()

const CZ::CZLogger &DeskLogger() noexcept;

#endif // CZ_DESKLOG_H
