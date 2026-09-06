# Cycle D plan — CDR "Watchdog Logic / Fault Manager", shadow-mode slice (index)

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`, branch `feat/persisted-record` @ 78a5d24.
Read `$R/CLAUDE.md` first (quote `$R`; no `make`; python = `fprime-venv/bin/python3`; never Read a >300-line file whole).
`P` = `PROVESFlightControllerReference`, `UT` = `P/test/unit-tests`, `Top` = `P/ReferenceDeployment/Top`. Paths relative to `$R`.

**Working tree is dirty**: 78a5d24 carries uncommitted Cycle B edits (`P/Components/ThermalManager/ThermalManager.{fpp,cpp,hpp}`,
`P/Components/RunInterval/`, ADCS/ImuManager/PowerMonitor sdds, `docs-site/requirements/cdh.md`). Apply D on top of the working tree; do not
revert B. If B has been committed by the time you start, nothing changes except line numbers in ThermalManager (grep, do not trust `:NN`).

Governing rules (non-negotiable):
1. Must not hurt the current system: **shadow mode by default** — `AUTHORITY_ENABLED` param default false; the FaultManager observes, debounces,
   counts, telemeters, emits events, and calls **no** action port. Every existing trigger path keeps executing verbatim (harm table, 03).
2. Stage 0 is host only: gtest against recorder stubs; target compile from `~/scalar-build/proves-core-reference`; board tests written, deferred.
3. No `lib/` edits. Topology diff = one instance, one rate-group slot, the `faultOut -> faultIn[i]` fan-in, two action outputs.
4. Requirement tables only via `scripts/req.py`; add IDs before any test claims them (RTM warnings fail `verify.sh`).

| File | Holds | Read when |
|---|---|---|
| `01-scope.md` | Per-requirement decision: what D proves at Unit level in shadow, what needs authority (Board, deferred), what is out of scope and the hook left | Before starting; when writing `verifies` strings |
| `02-design.md` | `FaultTypes.fpp` types, the F´-free `FaultTable` module, the passive `FaultManager` component (ports, params, events, tlm, commands), threading and the disposition contract | Before writing any new file |
| `03-harm-table.md` | Proof that defaults are identical to today: per-trigger table with the exact lines that stay, timing, memory, dictionary, event traffic | Before touching ModeManager/Router/Watchdog/Thermal; again at review |
| `04-files.md` | File-by-file steps in build order: new component dir, four one-line producers, packets, CMake, instances/topology, host stubs, tests, docs, `req.py` commands | While coding (one subsection at a time) |
| `05-verification.md` | Expected `verify.sh` deltas, RTM rows, target generate/build + dictionary checks, what stays deferred | After coding |
| `06-followups.md` | Non-goals, open risks, and the per-trigger authority-enable procedure on flatsat | At hand-off |
| `07-findings.md` | Verified facts (file:line) not yet in `docs-site/dev-loop-findings.md` — merge into the ledger at cycle end | Ledger merge |

Order of work: 04 §A (types + pure module + tests) → 04 §B (component + stub + tests) → 04 §C (producers, one at a time, run existing tests after
each) → 04 §D (packets, instances, topology, CMake) → target generate/build in the copy → 04 §E (docs, req.py, int test) → 05.
