#pragma once
// Isolate the core decoder from the server logging subsystem.
#include "CoreFakes.h"
#define LOG_WARN(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_FATAL(...) ((void)0)
