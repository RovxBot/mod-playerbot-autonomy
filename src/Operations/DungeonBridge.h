#ifndef PLAYERBOT_AUTONOMY_DUNGEON_BRIDGE_H
#define PLAYERBOT_AUTONOMY_DUNGEON_BRIDGE_H

// Optional companion capability. A source set without dungeon-clear still
// compiles and supports real group assembly, with launch unavailable.
#if __has_include("Integration/DcAutonomyApi.h") && __has_include("Integration/DcAutonomyPilot.h")
#include "Integration/DcAutonomyApi.h"
static_assert(DcAutonomy::Version == 2, "Unsupported trusted dungeon-clear contract version");
#define AUTONOMY_DC_BRIDGE 1
#else
#define AUTONOMY_DC_BRIDGE 0
#endif
#endif
