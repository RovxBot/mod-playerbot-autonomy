#ifndef PLAYERBOT_AUTONOMY_RECRUITMENT_DIRECTOR_H
#define PLAYERBOT_AUTONOMY_RECRUITMENT_DIRECTOR_H
#include "GuildDirector.h"
namespace PlayerbotAutonomy
{
enum class RecruitmentPhase
{
    Selected,
    Joining,
    Complete,
    Quarantined
};
struct RecruitmentIntent
{
    std::uint32_t Guid = 0, Account = 0, Guild = 0, Slot = 0, Actor = 0;
    std::uint64_t StageRevision = 0;
    RecruitmentPhase Phase = RecruitmentPhase::Selected;
    std::string StageRequest{}, Reason{};
    bool StageVerified = false;
};
struct RecruitmentSettings
{
    bool Enabled = false, DryRun = true;
    std::uint32_t CooldownMs = 15000, IntervalMs = 1000, CandidateBudget = 16;
    bool Valid() const
    {
        return CooldownMs >= 1000 && CooldownMs <= 86400000 && IntervalMs >= 100 && IntervalMs <= 60000 &&
               CandidateBudget >= 1 && CandidateBudget <= 64;
    }
};
class RecruitmentPort
{
public:
    virtual ~RecruitmentPort() = default;
    virtual std::map<std::uint32_t, RecruitmentIntent> const& Recruitments() const = 0;
    virtual std::optional<RecruitmentIntent> ActiveRecruitment() const = 0;
    virtual bool PersistRecruitment(RecruitmentIntent const& next, std::optional<RecruitmentPhase> before) = 0;
    virtual bool AuthorizedRecruiter(GuildIntent const& guild, std::uint32_t actor) const = 0;
    virtual bool IsRecruitMember(std::uint32_t guild, std::uint32_t guid) const = 0;
    virtual bool StageRecruit(RecruitmentIntent const& intent, EnrollmentRecord const& before) = 0;
    virtual bool JoinRecruit(RecruitmentIntent const& intent) = 0;
    virtual bool ConfirmRecruit(RecruitmentIntent const& intent, EnrollmentRecord const& before) = 0;
};
char const* RecruitmentPhaseName(RecruitmentPhase phase);
class RecruitmentDirector
{
public:
    RecruitmentDirector(GuildPort& guilds, RecruitmentPort& recruits) : _guilds(guilds), _recruits(recruits) {}
    void Configure(RecruitmentSettings settings, GuildSettings guildSettings);
    void Update(std::uint32_t diff);
    bool Request(std::uint32_t guid, std::uint32_t guild, std::uint32_t actor);
    std::string Plan(std::uint32_t guid, std::uint32_t guild, std::uint32_t actor) const;
    std::string const& LastDecision() const { return _last; }
    RecruitmentSettings const& Settings() const { return _settings; }
    std::uint64_t Considered() const { return _considered; }

private:
    GuildIntent const* Managed(std::uint32_t guild) const;
    void Progress(RecruitmentIntent const& intent);
    void Quarantine(RecruitmentIntent intent, std::string reason);
    GuildPort& _guilds;
    RecruitmentPort& _recruits;
    RecruitmentSettings _settings;
    GuildSettings _guildSettings;
    std::uint32_t _cooldown = 0, _untilStep = 0, _cursor = 0, _guildCursor = 0, _active = 0, _age = 0, _reserved = 0;
    std::uint64_t _considered = 0;
    std::string _last = "disabled";
};
}  // namespace PlayerbotAutonomy
#endif
