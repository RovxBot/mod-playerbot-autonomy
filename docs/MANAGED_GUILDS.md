# Managed founding release candidate

This source adds opt-in guild founding, not dungeon/PvP/economy operations. The
operator reports M1 deployed; this revision has only standalone, disposable-SQL
and pinned-header evidence. No server build, gameplay or deployment was run.

The world-thread GuildDirector reserves up to eight **new managed guilds** by
default, four Alliance and four Horde, with at most 70 members including each
founder. This is a managed-guild target, not a limit on existing realm guilds.
Existing guilds are audited in indexed one-guild pages before founding starts. Unavailable account-type snapshots halt guild actions
without changing ownership; they are not treated as human or filler evidence.
They are never deleted, disbanded or adopted. Git Commit and Bangerzoids remain
unmanaged. Ownership comes from durable director intent and confirmed creation,
not a leader account or name. External membership, leader or name changes can
quarantine governance; character protection remains intact.

## Activation and dependency order

Use core test-staging `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` and playerbots
master `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59` (merged enrollment handoff).
Keep dungeon-clear unchanged. Autonomy registers M1 protection before the guild
script. Guild work waits for complete M1 and ownership snapshots and the startup
existing-guild audit. Actual combined script/include/link order remains a manual
build/startup gate.

Apply both 15-November M1 migrations first, then the 18-November guild and
recruitment migrations and `2026_11_19_00_playerbot_autonomy_guild_journal.sql`.
The journal upgrade preserves history and permits repeated quarantine after
explicit recovery. The new module-owned InnoDB tables
store one immutable founding identity per slot and an append-only phase journal.
Unique founder, name, stage request and managed guild keys exclude duplicate
intents. The journal's enforced ROW_COUNT CHECK makes CAS/audit failures atomic.
No existing guild/character data is seeded or edited by the migration. The guild
pool opens only when the guild feature is enabled; failures stop guild work and
leave M1 protection active. Only one asynchronous guild query/transaction exists
at a time; an uncertain commit is read back before further action.

Start with `PlayerbotAutonomy.Enable=1`,
`PlayerbotAutonomy.GuildDirector.Enable=1`, and `GuildDirector.DryRun=1` (full
prefix `PlayerbotAutonomy.`). Automatic dry run permits reads and planning only:
no intent, reservation, logout/login, enrollment or guild write. Explicit console
recovery has a separate approval/write gate and can run while automatic work is
in dry run; follow [the recovery runbook](GUILD_FOUNDING_RECOVERY.md).
After controlled review,
`DryRun=0` authorizes internal founding without manual enrollment or GM/SOAP
mutation commands. `AllowEnrollmentCommands` may remain 0. Config includes
count, faction percentage, membership cap, class mask, candidate budget, interval
and founding cooldown; invalid ranges disable guild work. Existing immutable
intents count toward faction quotas, so increasing a balanced two-guild canary
to eight still produces four guilds per faction. Existing guilds are never
converted or deleted to rebalance a changed percentage. Reload/restart impose
a fresh full cooldown. Targets below existing ownership never delete guilds.

## Founding and recovery

1. Choose a known, dedicated type-1 guildless candidate with eligible class,
   faction and no group/invite/combat/instance/travel/human control conflict.
   A GUID cursor checks at most 16 candidates per pass; maintenance lookup remains
   O(1), with a separate ordered GUID index for world-thread candidate traversal.
2. Commit a unique PLANNED intent including initial enrollment revision and exact
   STAGE UUID. Reserve this GUID, wait for normal logout/save and login cleanup,
   and call the M1 STAGE API. No character progression is changed.
3. Read the exact durable RECRUITMENT actor/STAGE audit and receipt. A preexisting
   unrelated Pending is insufficient. Once Pending is reconciled, hand off to
   normal protected bot login. Revalidate all live facts before creation.
4. Commit CREATING before calling trusted `Guild::Create`, then register the
   returned guild with GuildMgr. No chat commands, fake GM, tabard/equipment/gold
   grants, petitions spoofing, direct guild SQL or character-online writes occur.
5. Persist MANAGED only after actual guild/leader/name/founder membership agrees
   in SQL; verify ownership and all current member accounts/factions/control.
   Call M1 CONFIRM after authoritative membership is durable. Recruitment actor
   identity is audited separately from administrator requests.

An in-process create result is never repeated. Restart in CREATING is ambiguous:
quarantine the intent, keep the founder Pending and the guild unmanaged, and
require separate review. A durable MANAGED/Pending pair can automatically
CONFIRM after revalidation; Persistent remains protected after any guild loss.
PLANNED restarts resume only with the exact stage receipt. Preparation and
creation visibility have separate bounded progress windows and a 600-second
total job cap. Owned reservations renew until the cap and release after durable
terminal readback; abandoned M1 leases still expire after 120 seconds. See
[founding race fixes and explicit recovery](GUILD_FOUNDING_RECOVERY.md).
DB timeouts wait for the outstanding operation;
there is no blind write retry or automatic CANCEL/RETIRE. Quarantined intents
continue occupying their slot and block subsequent founding, including queued
intents that have not created a guild. A returned in-process Create may finish
registration. Explicit recovery requires separate operator approval and exact
founding proof; it never adopts an unrelated guild.

The complete managed roster is checked at action authorization and at most one
managed roster per scheduled pass, bounded by the configured cap (hard maximum
200). Over-cap rosters stop governance without trimming. Map threads perform no
guild SQL or scheduling. Diagnostics expose failures, largest observed guild
scheduler duration, decisions and rejection counters; realm pmon/DB/tick budgets
still need the manual pilot.

## Read-only diagnostics and rollback

Administrator console/RA/SOAP only, with `Ra.MinLevel >= 3`:

```
autonomy-guild audit
autonomy-guild audit GUILD_ID
autonomy-guild plan GUID SLOT
```

Audit reports persisted ownership versus actual core facts. A missing ownership
record always means unmanaged. Plan explains candidate/slot rejection and queues
nothing. These commands never grant mutation authority. No outgoing player chat
or invitations are generated.

Disable new guild work with GuildDirector.Enable=0 or the activity master switch.
Already issued SQL may finish and is reconciled; no new guild action begins.
Pending/Persistent protection remains. Preserve all guild/enrollment/audit tables.
Never roll back to an unprotected binary on a running realm. Uncertain created
guilds remain for review, not destructive cleanup. The manual release pilot must
verify no inventory/XP/money changes, human boundaries, normal filler/profession
behavior, in-flight login cancellation, every restart window and actual load.
