#include "CoreFakes.h"
struct AiFactory
{
    static std::map<uint8, uint32> GetPlayerSpecTabs(Player* p) { return p->Talents; }
    static uint8 GetPlayerSpecTab(Player* p) { return p->Spec; }
};
