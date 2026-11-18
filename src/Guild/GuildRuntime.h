#ifndef PLAYERBOT_AUTONOMY_GUILD_RUNTIME_H
#define PLAYERBOT_AUTONOMY_GUILD_RUNTIME_H
#include "GuildStore.h"
#include "Persistence/EnrollmentRuntime.h"
namespace PlayerbotAutonomy
{
class GuildRuntime final : public GuildPort
{
public:
    GuildRuntime() : _director(*this) {}
    void Configure(GuildSettings settings);
    void Start(std::string const& info);
    void Update(uint32 diff);
    void Stop();
    std::string Audit(uint32 guild) const;
    std::string Plan(uint32 guid, uint32 slot) const;
    GuildDirector const& Director() const { return _director; }
    bool Ready() const override;
    std::map<uint32, GuildIntent> const& Intents() const override { return _intents; }
    bool Persist(GuildIntent const& next, std::optional<GuildPhase> before) override;
    uint32 NextCandidate(uint32 cursor) const override { return Runtime().Cache().NextGuid(cursor); }
    GuildCandidate Candidate(uint32 guid) const override;
    GuildView Inspect(GuildIntent const& intent) const override;
    bool Reserve(uint32 guid) override { return Runtime().Cache().Reserved() == guid || Runtime().Reserve(guid); }
    void Release(uint32 guid) override { Runtime().Release(guid); }
    bool Offline(uint32 guid) override { return Runtime().PrepareOffline(guid); }
    bool Stage(GuildIntent const& intent, EnrollmentRecord const& before) override;
    bool Login(uint32 guid) override { return Runtime().HandoffLogin(guid); }
    uint32 Create(GuildIntent const& intent) override;
    bool Confirm(GuildIntent const& intent, EnrollmentRecord const& before) override;

private:
    void Load();
    void AuditNext();
    bool Submit(GuildIntent const& intent, EnrollmentRecord const& before, EnrollmentRequest action);
    GuildDirector _director;
    GuildStore _store;
    std::map<uint32, GuildIntent> _intents;
    std::optional<QueryCallback> _query;
    std::optional<TransactionCallback> _transaction;
    bool _opened = false, _attempted = false, _loaded = false, _auditDone = false, _needLoad = true;
    uint32 _auditCursor = 0, _existingGuilds = 0, _queryAge = 0, _retry = 0;
    uint64 _enrollmentReceipts = 0;
    uint64 _failures = 0, _ticks = 0, _maxTickUs = 0;
    std::thread::id _owner;
    std::string _info;
};
GuildRuntime& Guilds();
}  // namespace PlayerbotAutonomy
#endif
