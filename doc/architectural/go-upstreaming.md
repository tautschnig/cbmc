# Go front-end branch: changes outside the front-end, and their upstreaming

`go-frontend` is the only branch of the Go work. This file lists its
commits that change code outside `src/go`, `regression/go` and the Go
documents, so that upstreaming starts here rather than from branch names.
Keep it up to date when such a commit is added, or a PR is opened or
merged.

"Alone" says whether the commit applies on its own to `develop`
(`826c1c7390`, checked with `git cherry-pick`); "no" means it textually
depends on earlier commits of this branch. Applying is not building or
passing the tests.

PR branches are prepared in a separate clone (`~/cbmc-upstream`, remotes
`origin` = diffblue/cbmc and `fork` = tautschnig/cbmc).

## In upstream PRs

| Commit | Subject | PR |
|---|---|---|
| `627d2df578` | Fatal assertions: also propagate to callers and other threads | #9400 |
| `97f789ce71` | Shadow memory: character pointers may access objects of pointer type | #9401 |
| `7cdcc55c4e` | Shadow memory: arrays of characters may access objects of pointer type | #9401 (adapted to `develop`) |
| `5a16a5847d` | lower_byte_update: the last element of an update of symbolic size | not ours: #9380 fixes the same bug |

## Prepared, not pushed

Branch `kiro/symex-guard-perf` in `~/cbmc-upstream`:

| Commit | Subject | Alone |
|---|---|---|
| `31e9ceed9a` | irept::compare: ireps that share their data are equal | yes |
| `03863d00dc` | symex_atomic_end: simplify the guards of shared accesses only if merged | yes |
| `e26207a1d5` | merge_ireps: reuse the merged guard of the previous step | yes |
| `5ee67eff58` | symex: merge the state's guard into the equation's shared expressions | yes |
| `c15a94ab47` | symbol_tablet: remember the next free suffix of each prefix | yes |

## Candidates, independent of Go

Memory model:

| Commit | Subject | Alone |
|---|---|---|
| `12fde03b52` | Memory model: no events for shared writes that nothing reads | yes |
| `28f24c5d4c` | Memory model: visit only the read-from choices of the writes considered | yes |
| `4e06933303` | Memory model: add from-read constraints without simplifying them | yes |
| `7467c30137` | Add --refine-concurrency: from-read constraints added lazily | no |

Nested arrays and byte operators:

| Commit | Subject | Alone |
|---|---|---|
| `c6fc82d8ba` | expr_initializer: bound flattening by the elements of nested arrays | yes |
| `85b1145528` | field_sensitivity: bound the number of fields that an array divides into | no (earlier field-sensitivity changes of this branch) |
| `63c2313a5c` | value_sett::assign: skip array elements that equal their predecessor | yes |
| `357c121d83` | Lower byte operators between arrays of the same element type by element | yes |
| `ec006be75e` | Lower byte operators by element also for one array of constant size | no (`357c121d83`) |
| `8c5154f7ef` | simplify_byte_update: no update of empty arrays, no long with chains | no |
| `a9744160b9` | Array theory: add Ackermann constraints only where they are not implied | yes |

Symex, sequential:

| Commit | Subject | Alone |
|---|---|---|
| `eec55e6369` | goto-symex: propagate values merged from constants | yes |
| `00c972e115` | goto-symex: assignments of values of size zero have no effect | yes |
| `d20035e5e0` | goto-symex: check for zero-size assignments only of aggregates | no (`00c972e115`) |
| `06dd1826f3` | goto-symex: do not divide arrays of const-qualified elements | yes |
| `1350f0f7a0` | symex: keep static const structs whole, decided by the symbol table | no (`06dd1826f3`) |
| `b404832c15` | symex: propagate constant updates of constant arrays | no |
| `064c7f50f5` | goto-symex: typed dereferences for front-ends | no |
| `3e3f028645` | Linking: skip renaming passes when there is nothing to rename | yes |

Symex, threads (a series: the shared-value summary of `f01c0fd6c4` and
what builds on it):

| Commit | Subject | Alone |
|---|---|---|
| `3efd7c3fd8` | Symex: reads within an atomic section see the value at its start | no |
| `f01c0fd6c4` | Symex: sound value sets for pointers shared between threads | yes |
| `fb2f76151d` | Symex: copy only the current instance of locals to a new thread | yes |
| `48d421466f` | Symex: unreachable states need not agree on the atomic section | yes |
| `7074a5843c` | Symex: more precise value sets of shared objects | no |
| `2543b34ddc` | Symex: repeat for shared value sets only when a lookup lacked values | no |
| `b628c6259b` | Symex: byte updates assign only the fields they overlap | yes |
| `208dee0cff` | goto-symex: a new thread starts outside any atomic section | yes |
| `13bd168c77` | goto-symex: objects of a previous run keep names of other types | no |
| `6b95e60032` | Symex: a thread starts with the state of its locals at the spawn | no |
| `bf65092c0b` | symex: shadow memory is not shared for the shared-value summary | no (`f01c0fd6c4`) |

Shadow memory (`bb98255232` and `731c8613f7` need the incompleteness
marker of `6f0e9ce28c`):

| Commit | Subject | Alone |
|---|---|---|
| `8d3b69c931` | Symex: keep types of renamed byte updates and conditionals consistent | yes |
| `6f0e9ce28c` | Shadow memory: record unresolved objects in __CPROVER_shadow_memory_incomplete | yes |
| `99d23955dd` | Shadow memory: resolve objects that other threads registered | no |
| `ac23ea0997` | Shadow memory: unbounded shadow arrays for arrays of symbolic size | no |
| `bb98255232` | Shadow memory: a failed symbol is incomplete only if accessed | no |
| `731c8613f7` | symex_assign: an array of copies takes the size of the left-hand side | no |

Profiling scripts:

| Commit | Subject | Alone |
|---|---|---|
| `37db5a0e35` | Profiling: benchmarks of concurrent programs | no |
| `54fbc11cc2` | Profiling: run CBMC at verbosity 9, not 10 | no |
| `d018e2015d` | Profiling: count the post-processing of the equation as pre-solver time | no |
| `c21aff1231` | profiling: stop the profiled CBMC when a benchmark times out | no |

## Part of the Go front-end

These change shared files only to add the Go front-end, and go upstream
with it: `f494777325`, `7b2bde110c`, `217a4e700b`, `6b2334f1cc`,
`b960893f6f`, `c495ea32a9`, `0a53607c47`, `318dd1fd73`, `cac03bce50`,
`4bdf49f50f`, `4e9af095d2`, `036b30283a`.

## Not for merging

Tag `exp/go-race-large-values` (`3ee4f040c2`): the environment switches
of the large-values investigation (`go-data-races.md`, section 14).
