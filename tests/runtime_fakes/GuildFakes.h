#ifndef AUTONOMY_TEST_GUILD_FAKES_H
#define AUTONOMY_TEST_GUILD_FAKES_H
#include <map>

#include "CoreFakes.h"
inline constexpr uint32 GR_OFFICER = 1, GR_RIGHT_INVITE = 16;
inline unsigned FakeGuildCreates = 0;
inline uint32 FakeNextGuild = 23;
inline std::function<void(uint32, uint32, std::string const&)> FakeGuildWrite;
struct Guild
{
    struct Member
    {
        uint32 Rank = 0;
        uint32 GetRankId() const { return Rank; }
    };
    uint32 Id = 0, Leader = 0;
    std::string Name;
    std::map<uint32, Member> Members;
    bool Create(Player* player, std::string const& name)
    {
        ++FakeGuildCreates;
        Id = FakeNextGuild++;
        Leader = player->Guid;
        Name = name;
        Members.emplace(Leader, Member{});
        player->Guild = Id;
        auto& entry = FakeCharacterCache.Entries.at(Leader);
        entry.GuildId = Id;
        if (FakeGuildWrite)
            FakeGuildWrite(Id, Leader, Name);
        return true;
    }
    uint32 GetId() const { return Id; }
    ObjectGuid GetLeaderGUID() const { return {Leader}; }
    std::string const& GetName() const { return Name; }
    uint32 GetMemberCount() const { return Members.size(); }
    Member const* GetMember(ObjectGuid guid) const
    {
        auto it = Members.find(guid.Value);
        return it == Members.end() ? nullptr : &it->second;
    }
    auto const& GetUnorderedMembers() const { return Members; }
    uint32 GetRankRights(uint32) const { return GR_RIGHT_INVITE; }
    bool AddMember(ObjectGuid guid)
    {
        Members.emplace(guid.Value, Member{});
        FakeCharacterCache.Entries.at(guid.Value).GuildId = Id;
        return true;
    }
};
struct GuildMgr
{
    std::map<uint32, std::unique_ptr<Guild>> Guilds;
    Guild* GetGuildById(uint32 id) const
    {
        auto it = Guilds.find(id);
        return it == Guilds.end() ? nullptr : it->second.get();
    }
    Guild* GetGuildByName(std::string const& name) const
    {
        for (auto const& item : Guilds)
            if (item.second->Name == name)
                return item.second.get();
        return nullptr;
    }
    void AddGuild(Guild* guild) { Guilds[guild->Id].reset(guild); }
};
inline GuildMgr FakeGuildMgr;
#define sGuildMgr (&FakeGuildMgr)
struct ObjectMgr
{
    static bool IsValidCharterName(std::string const& name) { return !name.empty(); }
};
#endif
