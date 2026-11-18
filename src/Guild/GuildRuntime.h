#ifndef PLAYERBOT_AUTONOMY_GUILD_RUNTIME_H
#define PLAYERBOT_AUTONOMY_GUILD_RUNTIME_H
#include "GuildStore.h"
#include "Persistence/EnrollmentRuntime.h"
namespace PlayerbotAutonomy
{
class GuildRuntime final : public GuildPort, public RecruitmentPort
{
public:
    GuildRuntime() : _director(*this), _recruiter(*this, *this) {}
    void Configure(GuildSettings settings);
    void ConfigureRecovery(bool allowed) { _allowRecovery = allowed; }
    void ConfigureRecruitment(RecruitmentSettings settings) { _recruiter.Configure(settings, _director.Settings()); }
    RecruitmentDirector& Recruiter() { return _recruiter; }
    std::map<uint32, RecruitmentIntent> const& Recruitments() const override { return _recruitments; }
    std::optional<RecruitmentIntent> ActiveRecruitment() const override;
    bool PersistRecruitment(RecruitmentIntent const& next, std::optional<RecruitmentPhase> before) override;
    bool AuthorizedRecruiter(GuildIntent const& guild, uint32 actor) const override;
    bool IsRecruitMember(uint32 guild, uint32 guid) const override;
    bool StageRecruit(RecruitmentIntent const& intent, EnrollmentRecord const& before) override;
    bool JoinRecruit(RecruitmentIntent const& intent) override;
    bool ConfirmRecruit(RecruitmentIntent const& intent, EnrollmentRecord const& before) override;
    std::string RecruitmentPlan(uint32 guid, uint32 guild, uint32 actor) const;
    void Start(std::string const& info);
    void Update(uint32 diff);
    void Stop();
    std::string Audit(uint32 guild) const;
    std::string Plan(uint32 guid, uint32 slot) const;
    std::string RecoveryDiagnosis(uint32 slot, uint32 guild);
    std::string Recover(uint32 slot, uint32 guild, std::string const& founderName, std::string const& approval);
    GuildDirector const& Director() const { return _director; }
    bool OnWorldThread() const override { return std::this_thread::get_id() == _owner; }
    bool Ready() const override;
    std::map<uint32, GuildIntent> const& Intents() const override { return _intents; }
    bool Persist(GuildIntent const& next, std::optional<GuildPhase> before) override;
    uint32 NextCandidate(uint32 cursor) const override { return Runtime().Cache().NextGuid(cursor); }
    GuildCandidate Candidate(uint32 guid) const override;
    GuildView Inspect(GuildIntent const& intent) const override;
    bool Reserve(uint32 guid) override { return Runtime().RenewReservation(guid) || Runtime().Reserve(guid); }
    void Release(uint32 guid) override { Runtime().Release(guid); }
    bool Offline(uint32 guid) override { return Runtime().PrepareOffline(guid); }
    bool Stage(GuildIntent const& intent, EnrollmentRecord const& before) override;
    bool Login(uint32 guid) override { return Runtime().HandoffLogin(guid); }
    uint32 Create(GuildIntent const& intent) override;
    bool Durable(GuildIntent const& intent) override;
    bool Confirm(GuildIntent const& intent, EnrollmentRecord const& before) override;

private:
    void Load();
    GuildView InspectOwnership(GuildIntent const& intent, bool recovery) const;
    std::string RecoveryRejection(GuildIntent const& intent) const;
    static std::string RecoveryApproval(GuildIntent const& intent);
    void AuditNext();
    void LoadRecruitmentPage();
    void InvalidateRecruitments();
    bool SubmitRecruit(RecruitmentIntent const&, EnrollmentRecord const&, EnrollmentRequest);
    bool Submit(GuildIntent const& intent, EnrollmentRecord const& before, EnrollmentRequest action);
    GuildDirector _director;
    RecruitmentDirector _recruiter;
    std::map<uint32, RecruitmentIntent> _recruitments, _recruitSnapshot;
    std::set<uint32> _activeRecruitments;
    bool _recruitLoaded = false, _needRecruitLoad = true;
    uint32 _recruitCursor = 0;
    GuildStore _store;
    std::map<uint32, GuildIntent> _intents;
    std::map<uint32, uint32> _createdGuilds;
    std::set<uint32> _creationAttempts;
    uint32 _durableGuild = 0;
    uint32 _diagnosedGuild = 0;
    bool _diagnosedDurable = false;
    bool _allowRecovery = false;
    std::optional<QueryCallback> _query;
    std::optional<TransactionCallback> _transaction;
    bool _opened = false, _attempted = false, _loaded = false, _auditDone = false, _needLoad = true;
    uint32 _auditCursor = 0, _existingGuilds = 0, _queryAge = 0, _retry = 0;
    uint64 _enrollmentReceipts = 0;
    uint64 _snapshotEpoch = 0;
    uint64 _failures = 0, _ticks = 0, _maxTickUs = 0;
    std::thread::id _owner;
    std::string _info;
};
GuildRuntime& Guilds();
}  // namespace PlayerbotAutonomy
#endif
