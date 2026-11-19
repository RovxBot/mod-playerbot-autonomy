#ifndef PLAYERBOT_AUTONOMY_GUILD_DIRECTOR_H
#define PLAYERBOT_AUTONOMY_GUILD_DIRECTOR_H
#include "GuildNamePool.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "Persistence/EnrollmentCache.h"
namespace PlayerbotAutonomy
{
enum class GuildPhase
{
    Planned,
    Creating,
    Managed,
    Quarantined
};
struct GuildIntent
{
    std::uint32_t Slot = 0, Founder = 0, Account = 0, Faction = 0, Guild = 0;
    GuildPhase Phase = GuildPhase::Planned;
    std::string Name, Reason;
    std::uint64_t StageRevision = 0;
    std::string StageRequest{};
    bool StageVerified = false;
};
struct GuildSettings
{
    bool Enabled = false, DryRun = true;
    std::uint32_t Target = 8, AlliancePercent = 50, MaxMembers = 70;
    std::uint32_t CandidateBudget = 16, IntervalMs = 1000, CooldownMs = 60000;
    std::uint32_t ClassMask = 0x5ff;  // WotLK playable classes 1..9 and 11.
    bool Valid() const;
};
struct GuildCandidate
{
    EnrollmentRecord Enrollment;
    std::uint32_t Faction = 2, Class = 0;
    bool Available = false, Online = false;
    std::string Reason;
};
struct GuildView
{
    std::uint32_t Guild = 0, Leader = 0, Members = 0, Faction = 2;
    bool Safe = false;
    std::string Reason;
};
// The port is implemented by the world-thread engine/async SQL adapter. It
// exposes values only; the controller retains no Player/Guild/session pointers.
class GuildPort
{
public:
    virtual ~GuildPort() = default;
    virtual bool OnWorldThread() const = 0;
    virtual bool Ready() const = 0;
    virtual GuildNameChoice ChooseName() const = 0;
    virtual std::map<std::uint32_t, GuildIntent> const& Intents() const = 0;
    virtual bool Persist(GuildIntent const& next, std::optional<GuildPhase> before) = 0;
    virtual std::uint32_t NextCandidate(std::uint32_t cursor) const = 0;
    virtual GuildCandidate Candidate(std::uint32_t guid) const = 0;
    virtual GuildView Inspect(GuildIntent const& intent) const = 0;
    virtual bool Reserve(std::uint32_t guid) = 0;
    virtual void Release(std::uint32_t guid) = 0;
    virtual bool Offline(std::uint32_t guid) = 0;
    virtual bool Stage(GuildIntent const& intent, EnrollmentRecord const& before) = 0;
    virtual bool Login(std::uint32_t guid) = 0;
    virtual std::uint32_t Create(GuildIntent const& intent) = 0;
    virtual bool Durable(GuildIntent const& intent) = 0;
    virtual bool Confirm(GuildIntent const& intent, EnrollmentRecord const& before) = 0;
};
std::uint32_t NextFoundingFaction(GuildSettings const& settings, std::map<std::uint32_t, GuildIntent> const& intents);
char const* PhaseName(GuildPhase phase);
std::string GuildRequestId(std::uint32_t guid, std::uint32_t scope, std::uint32_t event, std::uint64_t revision);
std::string FoundingRejection(GuildCandidate const& candidate, GuildSettings const& settings, std::uint32_t faction);
class GuildDirector
{
public:
    explicit GuildDirector(GuildPort& port) : _port(port) {}
    void Configure(GuildSettings settings);
    void Update(std::uint32_t diff);
    GuildSettings const& Settings() const { return _settings; }
    std::string const& LastDecision() const { return _last; }
    std::uint64_t Considered() const { return _considered; }
    std::map<std::string, std::uint64_t> const& Rejections() const { return _rejections; }

private:
    void Progress(GuildIntent const& intent);
    void Reject(std::string reason);
    void Quarantine(GuildIntent intent, std::string reason);
    GuildPort& _port;
    GuildSettings _settings;
    std::uint32_t _untilStep = 0, _cooldown = 0, _cursor = 0, _reserved = 0, _armed = 0, _created = 0;
    std::uint32_t _jobAge = 0, _jobSlot = 0, _managedCursor = 0;
    std::uint32_t _phaseAge = 0;
    unsigned _progress = 0;
    bool _loginRequested = false;
    bool _createAttempted = false;
    std::uint64_t _considered = 0;
    std::string _last = "disabled";
    std::map<std::string, std::uint64_t> _rejections;
};
}  // namespace PlayerbotAutonomy
#endif
