#include <cassert>
#include <iostream>

#include "Guild/GuildNamePool.h"
#include "GuildTestPort.h"
using namespace PlayerbotAutonomy;
int main()
{
    static_assert(GuildNamePool.size() == 46);
    assert(std::string(GuildNamePool[5]) == "CyLx-亚服");
    assert(std::string(GuildNamePool[7]) == "ЦентуриТрайб");
    assert(std::string(GuildNamePool[19]) == "暗流");
    assert(std::string(GuildNamePool[20]) == "一树梨花白");
    assert(std::string(GuildNamePool[38]) == "*The Un*named GDKP");
    auto accept = [](std::string const&) { return std::string{}; };
    auto choice = SelectGuildName(GuildNamePool, accept);
    assert(choice.Name == "Numen" && choice.Rejected.empty());
    std::vector<std::string> duplicate{"  Numen\t", "Numen", "暗流", "ЦентуриТрайб"};
    choice = SelectGuildName(duplicate, accept);
    assert(choice.Name == "Numen" && choice.Rejected.size() == 1);
    assert(choice.Rejected[0].second == "duplicate_pool_candidate");
    auto exhausted = SelectGuildName(GuildNamePool, [](auto const&) { return "existing_core_guild"; });
    assert(exhausted.Name.empty() && exhausted.Rejected.size() == 46);
    assert(TrimGuildName("  uh oh \n") == "uh oh");
    assert(TrimGuildName(" \t").empty());
    Port port;
    port.Ledger[1] = {1, 1, 101, 0, 23, GuildPhase::Managed, "Ashen Vanguard", "original"};
    port.Ledger[2] = {2, 2, 102, 0, 24, GuildPhase::Managed, "Amber Covenant", "original"};
    port.Ledger[3] = {3, 11, 111, 1, 25, GuildPhase::Managed, "Silver Horizon", "original"};
    port.Ledger[4] = {4, 12, 112, 1, 26, GuildPhase::Managed, "Dawn Wardens", "original"};
    port.Ledger[5] = {5, 13, 113, 1, 0, GuildPhase::Planned, "Numen", "persisted"};
    port.Ledger[5].StageRequest = GuildRequestId(13, 5, 0x1001, 0);
    assert(port.ChooseName().Name == "XS");
    auto before = port.Ledger;
    GuildDirector restarted(port);
    GuildSettings s;
    s.Enabled = true;
    s.DryRun = true;
    restarted.Configure(s);
    port.Step(restarted, 20);
    for (auto const& item : before)
        assert(port.Ledger.at(item.first).Name == item.second.Name);
    assert(!port.Writes && !port.Creates);
    Port full;
    for (std::uint32_t i = 0; i < GuildNamePool.size(); ++i)
    {
        full.Ledger[i + 1] = {i + 1, i + 100, i + 200, 0, i + 300, GuildPhase::Managed, GuildNamePool[i], "owned"};
        full.Bots[i + 100] = {
            {i + 100, i + 200, i + 300, 2, EnrollmentState::Persistent, true, true}, 0, 1, true, true, "eligible"};
    }
    GuildDirector stopped(full);
    s.Target = 64;
    s.DryRun = false;
    s.CooldownMs = 1000;
    stopped.Configure(s);
    full.Step(stopped, 10);
    assert(!full.Writes && stopped.LastDecision() == "guild_name_pool_exhausted");
    std::cout << "PASS 46-name pool, exact Unicode/case/punctuation, duplicates, restart stability and exhaustion\n";
}
