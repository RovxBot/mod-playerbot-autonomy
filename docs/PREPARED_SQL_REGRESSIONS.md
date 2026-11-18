# Enrollment decoding and guild binding regression evidence

Prepared 18 November 2026 for the two reported production defects. Base autonomy
commit: `a5f3e589042916621b80d93d51e64dfb03a912df`. Related tracking: #5, #6, #17.
No worldserver build, deployment, live DB access or live migration is part of this
change. Operator-provided production observations are separate from the disposable
reproduction below; the exact deployed dependency SHAs were not independently verified.

## Revision root cause

The reported GUID 187 had no enrollment row, SQL state `ABSENT`/revision `0`,
but the runtime reported revision `576461298018222128` and allowed world checks.
The call path is `EnrollmentStore::LoadPage` (five correctly bound cursor/limit
parameters) -> `Sql::Page` -> core `PreparedResultSet` ->
`EnrollmentRuntime::LoadPage` -> `Field::Get<uint64>()` -> cache/diagnosis.

On disposable **MySQL 8.0.44**, the exact old page statement's
`COALESCE(e.revision, 0)` has metadata **MYSQL_TYPE_NEWDECIMAL (246)**. The stored
column is `BIGINT UNSIGNED`; its fallback literal is signed. Their combined
numeric range requires DECIMAL. An absent row returns the one-byte decimal text
`"0"`, not eight bytes of integer zero. The core's `QueryResult.cpp::SizeForType`
allocates 64 bytes for DECIMAL, and `PreparedResultSet` marks the fetched value as
raw bytes. `Field.cpp::GetData<uint64>()` dereferences those bytes as a uint64;
it does not parse decimal text. Optional strict type checks only log the mismatch.

The isolated test links those two **unmodified core source files** and reproduces
`metadata=NEWDECIMAL text=0 core uint64=48`. The low byte is ASCII `0` (`0x30`);
the other bytes depend on buffer contents. The reported production value also
ends in byte `0x30`. Its exact garbage value is not a stable regression oracle.
PyMySQL's text protocol parses the decimal as zero, so the previous SQL tests
could not reproduce this fault.

The fix is `COALESCE(e.revision, CAST(0 AS UNSIGNED))`: both arguments are unsigned
BIGINT, so prepared metadata is **MYSQL_TYPE_LONGLONG (8)** and `Get<uint64>()`
reads the correct integer. No floating-point conversion or diagnostic masking is
used. Tests retain real revisions above both 2^53 and the signed 64-bit range.

The runtime also rejects `ABSENT` with nonzero revision, any durable state with
zero revision, unknown state, NULL revision, or disagreement between state and
enrollment-row presence. It invalidates the affected GUID and invokes the existing
failure/retry path before publishing cache readiness. Maintenance remains blocked
and new transitions remain unavailable, including with activities disabled. No
enrollment record is retired, repaired or deleted by this validation.

## Numeric result audit

| Result | MySQL binary type / getter after correction |
| --- | --- |
| Page revision with unsigned fallback | unsigned LONGLONG / `uint64` |
| Page current/enrollment guild, receipt guild/actor, managed guild ID | native nullable unsigned LONG / `uint32`; core NULL -> 0 |
| Page character/account IDs; guild/recruitment identities, slots, audit cursor | native unsigned LONG / `uint32` |
| Guild faction | unsigned TINY / `uint8` (previously over-read as `uint32`) |
| Guild/recruitment stage revision and enrollment receipt revisions | unsigned LONGLONG / `uint64` |
| Guild/recruitment `EXISTS` stage proof | LONGLONG / `uint64` (previously `uint32`) |

Unnecessary numeric `COALESCE(...,0)` expressions in result columns were removed:
they widened INT results to BIGINT. Predicate-only expressions do not feed a
`Field` getter and do not require a decoding change. Empty sentinel rows remain
distinguished by a NULL first identity column. String state fallback is retained.
Binary tests verify false/true founding and recruitment stage proofs, both NULL
and non-NULL receipt IDs, faction 1 and guild IDs before/after creation.

## Guild binding root cause

`GuildStore::Save` incorrectly copied insert-only stage revision/request into
update placeholders 4/5, then bound founder/account into 6/7 and overwrote them
with phase values. The resulting identity predicate could not match the intended
row. Existing Python tests supplied a separate correct tuple, bypassing this code.

The update now binds exactly once per placeholder:

| Index | Value |
| --- | --- |
| 0 | target phase |
| 1 | guild ID |
| 2 | reason |
| 3 | slot |
| 4 | founder GUID |
| 5 | account ID |
| 6 | previous phase |
| 7 | target phase |
| 8 | guild ID |

`tests/PreparedMySQLTests.cpp` calls the actual production `GuildStore::Save`
and checks all nine typed values for CREATING and MANAGED, using stage identities
that deliberately differ from founder/account. It executes the registered
production `GuildSql::Change` and following `GuildSql::Audit` on the same real
MySQL transaction connection. Founder/account mismatches produce the enforced
`chk_autonomy_guild_mutation` failure, roll back and add no audit row. A successful
mutation followed by duplicate audit failure also rolls back. PLANNED -> CREATING
-> reconnect/load -> MANAGED succeeds; stale retries and duplicate intents fail.
The director's interrupted-create quarantine and no-repeat-create tests remain in
the C++17/C++20 suites. No recovery, create/adoption or human ownership rule changed.

## Reproduction and verification

Use a disposable database named `autonomy_test` or `autonomy_test_<lowercase suffix>`.
The runner recreates minimal synthetic fixtures and applies module migrations
twice **only there**. It requires PyMySQL and MySQL client development files.

```sh
AUTONOMY_MYSQL_SOCKET=/path/to/disposable/mysql.sock \
AUTONOMY_MYSQL_DATABASE=autonomy_test \
python3 tools/check_prepared_mysql.py \
  --core /path/to/pinned-core --playerbots /path/to/pinned-playerbots
```

For unpacked client libraries, add `--mysql-dir /path/to/mysql`; optional
`--boost-include /path/to/boost/include` supports local dependency bundles.
Dependencies are checked against core
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` and playerbots
`422db5a50b429db01ab481ba597b928c87b0eb98`. No dependency source is patched or
copied into this repository. The core decoder requires C++20; module-only tests
continue under both C++17 and C++20.

The test transport replaces pool/queue scheduling and game objects, executes the
production stores' parameter calls via `mysql_stmt_prepare`/`mysql_stmt_execute`,
and delegates result buffering/getters to the pinned core implementation. It is
not the core async pool, a combined server link, SOAP execution, or a real Guild
API acceptance test. No production maintenance, guild creation or recruitment is
executed. Fixtures assert unchanged character level/money.

Cache coverage includes GUID 187 ABSENT -> revision 0, actual STAGE/CONFIRM
revisions 1/2, nonzero Pending/Persistent rows, full-width revisions, 300 characters
over multiple 128-row pages, four periodic sweeps, stop/reconnect, a fresh empty
runtime cache with activities off, and invalid zero-revision protection/recovery.
The deterministic runtime tests additionally inject the reported ABSENT/nonzero
pair and zero revisions for Pending/Persistent/Retired.

Local results on this change:

- All eight standalone policy/lifecycle/cache/receipt/director/runtime variants
  passed under each of C++17 and C++20, including provider present and absent.
- `python3 -m unittest discover -s tests -p 'test_*.py' -v`: **50 passed** on
  disposable MySQL 8.0.44 and **50 passed** on a separate disposable MySQL 8.4.10.
- The binary prepared harness passed on both servers,
  including the legacy `NEWDECIMAL`/48 negative control. Fixtures are synthetic.
- Pinned real-core syntax checks passed for all nine autonomy translation units.
  These are syntax checks only, not worldserver compilation/linking/startup.

CI retains the C++17/C++20 matrix and runs the new binary harness in each MySQL
8.0/8.4.10 service job. CI results must be checked on the PR's actual head.

## Rollout and rollback

No schema or configuration change is required; all automation retains its current
default-off configuration. A later owner-authorized release must build/restart the
module with the matching dependencies, then verify GUID 187's cached revision and
stage diagnosis before any guild pilot. This work does not authorize that release.
Protection must remain installed during any rollback. Disabling activities is safe;
removing maintenance protection or returning to the defective decoder is not a
repair. An inconsistent durable row stays protected for operator investigation;
this patch does not automatically alter it or adopt a guild.
