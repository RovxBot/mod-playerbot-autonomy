# Founding races and explicit recovery

This revision is source and disposable-test evidence only. No live SQL, server
build, deployment or recovery was performed. The production report is supplied
by the operator: slot 1, Alliance Junglatu (GUID/account 1/1), Ashen Vanguard, core guild
23, QUARANTINED with NULL recorded guild, Pending revision 1 and an exact STAGE
receipt 0 -> 1; slot 2, Horde Crudi (14/2), Amber Covenant, guild 24, Managed/Persistent
revision 2. The reported 20:32:19 and 20:32:36 CHECK errors and 20:32:18 ->
20:32:25 creation/timeout sequence motivate these tests; they are not new live
observations.

## Ordering, lifetime and protection

On core test-staging `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`,
`Guild::Create` queues the guild transaction, then `AddMember` independently
queues membership. A different pool can observe neither or only one. The
world-thread adapter retains the returned ID before invalidating enrollment,
then asynchronously checks both rows through its own pool. Only a positive
observation and current world ownership proof permit the guarded MANAGED CAS.
A failed scheduling attempt, transaction, acknowledgement or reload never calls
Create again. The controller and adapter both retain successful identities.
Quarantine preserves a known returned ID. A cold CREATING intent is ambiguous
and quarantines without invoking Create, including when guild rows already exist.

Preparation has 180 seconds per observed forward milestone (exact STAGE receipt,
protected login, then CREATING). Core creation/visibility has a separate 90-second
window, refreshed when Create returns, with a 600-second total enabled-job cap.
Readiness stalls count against these finite bounds, but do not consume the next
phase's entire allowance. An owner renews its M1 reservation each world update
until that total cap, independently of SQL readiness. Release follows durable
MANAGED/QUARANTINED readback, not scheduling success. Disabled/dry-run work
releases its transient lease. M1 still expires an abandoned lease after 120
seconds; renewal stops at the job cap, so a DB outage cannot retain it forever.
Pending/Unknown protection survives expiry, disable and restart. No automatic
CANCEL or RETIRE is introduced.

Any unresolved quarantine stops new founding, including already queued intents
that have not called Create. An already returned in-process Create may finish
registration; existing Managed guild auditing/confirmation continues. Resolving
one quarantine does not bypass another. This deliberately requires operator
review rather than cascading through unused slots.

Login/guild invalidations now dirty only their GUID, retaining fail-closed
maintenance protection until a current, versioned SQL/world snapshot agrees.
They never rewind the initial or periodic 128-row scan. After startup, targeted
refreshes are capped at one per 200 ms and alternate with periodic pages. An
invalid reserved founder gets alternating priority. The sweep clock advances
while reads/refreshes run. Read traffic no longer makes the whole director
unready: at most one enrollment request can wait behind an existing read, then
revalidate before its receipt/transaction. Writes, unknown DB health, timeouts,
schema failures and incomplete startup still block activity appropriately.
`autonomy status` exposes pages, point_refreshes and dirty_guids. No map-thread
SQL or per-bot realm scan is added.

## Operator procedure for Junglatu

This procedure is documentation for a separately approved rollout. Do not execute
it as part of reviewing or testing this PR.

1. Install the reviewed binary/source set and apply the existing four migrations
   plus `2026_11_19_00_playerbot_autonomy_guild_journal.sql` through the normal
   updater. This last replay-safe module-only migration retains every phase
   receipt, gives it an auto-increment audit ID and indexes (slot,phase). The
   enforced mutation CHECK remains. Repeated quarantine after recovery can now
   append a new event; a phase CAS miss still rolls back. Guild loading fails
   closed if this upgrade is missing. Do not edit core guild/character tables.
2. Keep M1 protection enabled. Freeze automatic guild writes using
   `PlayerbotAutonomy.GuildDirector.DryRun=1`, with the activity master and guild
   director enabled so read-only snapshots load. Keep
   `PlayerbotAutonomy.AllowGuildRecoveryCommands=0` and recruitment disabled.
   Console/administrator RA admission requires `Ra.MinLevel >= 3`; in-game
   sessions cannot use this capability.
3. Inspect, then repeat after the asynchronous read completes:

   ```text
   autonomy status
   autonomy-guild audit 23
   autonomy-guild audit 24
   autonomy-guild recovery-diagnose 1 23
   ```

   Diagnostics read only. They show immutable name/founder/account/faction,
   actual name/leader/member count/founder membership, current identity and
   dedicated type-1 classification, phase/recorded guild, enrollment state and
   revision, and exact STAGE request/expected/resulting revisions. The durable
   membership field is a previous SQL observation; every write uses a fresh
   probe and guarded transaction. No diagnostic token grants write authority.
4. Require all proof: slot 1's intended name is exactly **Ashen Vanguard**,
   founder **Junglatu**, GUID/account **1/1**, guild **23**, leader **1**, founder
   membership present, and immutable faction matches both current core team
   and SQL character race. Do not infer faction from a name. Require the exact
   director STAGE actor/reason and receipt 0 -> 1, and current Pending(1).
   This narrow capability requires a singleton founder roster; additional
   members, human/AddClass accounts, human account sessions, selfbots, human
   masters, name/account/leader/faction changes, missing or unrelated receipts,
   other recorded guild IDs and changed lifecycle revisions all reject. A
   prior committed CREATING journal entry is required in SQL. Do not repair
   failed proof by altering data to make it fit.
5. Obtain explicit operator approval of these observations. Only after approval,
   explicitly enable `PlayerbotAutonomy.AllowGuildRecoveryCommands=1` and invoke:

   ```text
   autonomy-guild recover 1 23 Junglatu APPROVE:1:23:1:1:<exact-stage-request-from-diagnostics>
   ```

   The literal command must contain the observed UUID in place of the
   placeholder. The config gate, exact approval tuple and current founder name
   are independent requirements. The async callback rechecks current world
   proof and whether approval/activity was revoked. SQL independently checks
   immutable identity/name, current account/race/membership, phase, original
   CREATING audit, exact STAGE audit/receipt and Pending revision. CAS and a new
   MANAGED audit commit together. Failures retain quarantine/Pending; uncertain
   acknowledgements trigger durable readback. The command never calls a guild
   API or writes character progression. Replay of a verified Managed result
   returns already_managed without another write/audit.
6. Inspect `audit 23` and slot/lifecycle status again. The first write should
   produce Managed/guild 23 while enrollment stays Pending(1). Revoke the
   recovery flag immediately. Only after reviewing that result, set the normal
   guild director out of dry run to permit its existing CONFIRM path. It must
   produce Persistent(2). Verify guild 24 and Crudi remain Managed/Persistent(2),
   with unchanged identity and audit history. Check actual character progression
   through the owner's usual read-only pilot procedure. Never delete/recreate
   guild 23 or manually set either enrollment revision.

## Failure and rollback

Failed proof leaves the existing quarantine. A SQL CAS or audit failure rolls
back the complete recovery transaction. Retrying after uncertain commit reads
back ownership first; a committed recovery is idempotent. A later ownership
change appends another quarantine and retains Persistent protection. Recovery
cannot authorize a guild with no explicit founding intent, a human guild, or
one previously managed and subsequently quarantined: its lifecycle/audit history
will not satisfy the Pending founding proof.

Stop new writes with the recovery flag off and director dry run/disabled (or the
activity master off). Already issued transactions may finish; inspect their
receipts. Keep M1 protection, module tables and all journal history, including
the new audit_id/index. Do not reverse a committed recovery by clearing guild_id,
rewriting audits, retiring the founder, deleting a guild, removing protection or
restoring an unprotected binary. No schema downgrade is necessary for stopping
activities. Review ambiguous outcomes while the guild stays unmanaged and the
character stays protected.

## Reproduction and evidence

The prepared harness uses actual GuildStore/EnrollmentStore/GuildRuntime code
and the unmodified core Field/QueryResult decoder at the core pin above, with
playerbots master `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`. Engine objects and
queue scheduling are deterministic fakes. Dungeon-clear is unchanged; this
slice introduces no cross-repository API patch. The existing compatibility
pins describe the tested source set, not a claim about the deployed SHA.
The playerbots pin is the verified current master merge commit. Its Git tree
`e8be0e336ef81f4425380af0647fd2b02197511e` is identical to the prior
`422db5a50b429db01ab481ba597b928c87b0eb98` pin: no source or interface changes
intervene. The recovery fixture now preserves the reported two-faction incident:
Alliance Junglatu (0), Horde Crudi (1), with synthetic representative races.
It also rejects a Horde race substituted for Junglatu and verifies Crudi
remains unchanged, including faction.

```sh
AUTONOMY_MYSQL_SOCKET=/path/to/disposable/mysql.sock \
AUTONOMY_MYSQL_DATABASE=autonomy_test \
python3 tools/check_prepared_mysql.py --core /path/to/pinned/core \
  --playerbots /path/to/pinned/playerbots
```

Local results on disposable **MySQL 8.0.44**:

- Binary prepared original decoder negative control and all existing store tests.
- Independent uncommitted guild and member transactions, followed by each
  commit; exactly one real adapter Create call and no visibility-induced CAS
  errors. Injected MANAGED audit failure retains the identity and reservation,
  then succeeds after fault removal without repeating Create.
- Fresh adapter between Create/registration, explicit quarantine, exact recovery
  identity/name/phase/STAGE rejections, audit failure rollback, lost-acknowledgement
  readback, console/config/
  human control gates, revocation while proof is pending, idempotent replay,
  normal Confirm(1 -> 2), repeated quarantine, unchanged successful guild 24.
  Journal upgrade/replay preserves all prior receipts and their timestamps.
- 8,250 synthetic characters, concurrent login invalidations every scheduler
  tick: **65 initial pages, 370 activity-ready windows in 500 ticks, 92 point
  refreshes**. The periodic sweep also completes under continuing churn.
- All standalone policy/controller/cache/runtime suites pass C++17 and C++20,
  including bounded readiness stalls, reservation renewal/abandoned expiry,
  scheduling failure, known-ID timeout, no-repeat-create and cascade blocking.
- 52 Python tests pass with disposable MySQL enabled; scaffold verification
  passes. Pinned real-header syntax checks cover all nine runtime sources.

These checks do not establish combined C++17 core compatibility, linking,
startup, live recovery, or production CPU/DB/map-tick budgets. No worldserver
build, deployment, live connection or production SQL write was performed.
