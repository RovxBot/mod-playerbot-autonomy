#include "CoreFakes.h"
namespace lfg
{
inline constexpr uint32 LFG_STATE_NONE = 0;
}
struct FakeLFGMgr
{
    std::unordered_set<uint32> Queued;
    uint32 GetState(ObjectGuid guid) const { return Queued.count(guid.Value); }
};
inline FakeLFGMgr FakeLfg;
#define sLFGMgr (&FakeLfg)
