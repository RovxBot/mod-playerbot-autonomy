# Recruitment release candidate

This adds automatic, opt-in recruitment to the managed founding candidate. It is
implemented in source and tested through pure controller ports and disposable
SQL, not accepted by a live game server. The operator will initiate the full
build separately. No dungeon-clear, dungeon, raid, PvP or guild economy work is
included.

## Flags and scope

All switches default off for mutation. Keep `AllowEnrollmentCommands=0`; manual
GM/SOAP enrollment is unnecessary. Recruitment requires:

```
PlayerbotAutonomy.Enable = 1
PlayerbotAutonomy.GuildDirector.Enable = 1
PlayerbotAutonomy.GuildDirector.DryRun = 0
PlayerbotAutonomy.Recruitment.Enable = 1
PlayerbotAutonomy.Recruitment.DryRun = 0
```

Use dry runs first. Either dry-run flag suppresses recruitment writes; stopping
recruitment does not stop founding if that remains separately authorized, and
neither activity switch removes persistence protection. `GuildDirector.MaxMembers`
(default 70 including founder), class mask, interval and candidate budget apply
to recruitment. A global single active recruitment job and a 15-second request
cooldown bound work. Reload/restart impose a fresh full cooldown. The default
managed target is eight new guilds, four per faction. Existing guilds are never
automatically adopted to satisfy it.

## Authorization and lifecycle

The world-thread director makes decisions on behalf of an explicitly managed
bot leader. Its world-thread-only internal `Recruiter().Request(guid,guild,actor)` also accepts a
verified bot officer with invite rights, through the same budget and durable
protocol. It never impersonates a player/GM, invokes chat commands or requires a
human session. Map-thread callers are rejected without changing diagnostics;
future bot-AI integrations must enqueue through the existing world-thread processor.
Human-managed guilds, real players, AddClass/selfbots, human masters
and active client-account sessions are excluded. Every mutation revalidates
ownership, complete current member classification, actor rank/rights and identity.

The director checks at most the configured GUID candidate budget per pass, using
M1's ordered candidate index. It audits a bounded managed roster once for planning
and again at action authorization, not for every candidate. Together with the
founding audit, at most three bounded roster validations occur per ordinary
scheduled action pass (up to 200 members each). No per-bot population scans,
professions/inventory/auction scans or map-thread SQL are added. Offline
`Guild::AddMember` itself uses the core's synchronous character query, once per
selected world-thread action; its real latency is a pilot measurement gate.

1. Persist SELECTED intent with candidate/account, explicit managed guild/slot,
   bot actor, initial enrollment revision and exact STAGE UUID. Duplicate GUIDs
   and UUIDs cannot create another job. An old completed/quarantined job is never
   silently reused, including after explicit retirement.
2. Reserve that GUID through M1's login/maintenance veto. Wait for pending-login
   cleanup and normal save/logout, verify no group/activity/guild/invite conflict,
   then call M1 STAGE. Candidate type/faction/class/control remain authoritative.
3. Verify the exact durable RECRUITMENT audit and receipt, including guild, actor,
   account and revision. An unrelated Pending is insufficient. Commit JOINING
   before the guild API. Reacquire reservation/logout after restart when needed.
4. On the world thread, revalidate actual ownership, all member accounts/factions,
   recruiter rank/invite rights, candidate availability, class/faction, real guild
   membership and actual cap immediately before trusted `Guild::AddMember`.
   Existing correct membership is observed idempotently without another AddMember.
5. Call M1 CONFIRM after authoritative membership is present in SQL, then mark
   COMPLETE only when Persistent and membership agree. Release only transient
   reservations. No direct enrollment-table, guild-table or `characters.online`
   updates occur in runtime guild code; the M1 API owns lifecycle mutations.

Membership caps are enforced at planning and immediately at AddMember; reaching
the cap after STAGE quarantines the request while Pending stays protected.
Changed actor/ownership, another guild, failed AddMember, missing identity, stale
receipt or a 180-second timeout cannot return a candidate to destructive filler
maintenance. No automatic CANCEL or RETIRE is issued. Quarantine/review is
intentional for ambiguous or conflicting outcomes, not an automatic disband,
kick, cleanup reset or adoption. The ordinary successful path needs no operator.

## Restart, disable and storage

Apply the M1 migrations, the guild migration, then
`2026_11_18_01_playerbot_autonomy_recruitment.sql`. The latter creates only two
module-owned InnoDB tables: immutable recruitment identity/current phase and an
append-only phase journal. Its enforced ROW_COUNT CHECK makes failed identity/
revision/CAS and audit writes atomic. The guild connection prepares all guild
and recruitment statements, so both new migrations are required before enabling
the guild store, even if recruitment mutations are off. No existing data is
seeded, automatically adopted or enrolled by migrations.

Ownership loads at most 64 slots; recruitment loads indexed pages of 64 rows.
Only a complete snapshot is published. Active-job GUIDs are indexed separately
from completed history. One guild query/transaction is outstanding globally.
Enrollment receipt changes invalidate both snapshots with a generation check, so
an older asynchronous read cannot overwrite current receipt proof. Timed-out DB
work remains outstanding; new actions wait for readback instead of blind retries.

On restart, SELECTED/Pending can resume only with the exact stage receipt;
JOINING with actual membership proceeds to CONFIRM; Persistent with correct
membership proceeds to COMPLETE. Neither repeated requests nor a restart after
guild addition calls AddMember twice. Other-guild membership, disband, unexpected
actor/leader/human membership and orphan identities quarantine rather than
release durable protection. A disabled director starts no guild actions; issued
SQL may finish and is read back. Preserve all history on rollback. Keep M1
protection installed, or keep the realm stopped while restoring a compatible
binary. Downgrading to an unprotected binary is not a safe running rollback.

## Read-only diagnosis

Administrator console/RA/SOAP only; RA admission must remain administrator-only
(`Ra.MinLevel >= 3`):

```
autonomy diagnose 187
autonomy-guild audit
autonomy-guild audit GUILD_ID
autonomy-guild plan GUID SLOT
autonomy-guild recruit-plan GUID GUILD_ID ACTOR_GUID
```

These commands perform no SQL, scheduling, reservation, logout/login, lifecycle
or guild mutation. `recruit-plan` reports missing explicit ownership, duplicate
history, full cap, actor rejection, account/faction/class/group/activity conflict
or eligibility and snapshot readiness. A plan with ready=false does not authorize
mutation. The supplied Trela/Burning Control identity is not automatically
adopted; use live read-only diagnosis after the separately authorized build.
The original STAGE rejection remains unverified without current realm facts.

See [the manual build checklist](RELEASE_CANDIDATE.md) for exact dependencies,
source-manifest capture, evidence and remaining linked-server tests.
