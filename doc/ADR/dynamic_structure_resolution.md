\page dynamic-structure-resolution Resolving program structure that depends on string values

**Date**: 5 Nov 2026
**Updated**: 5 Nov 2026
**Author**: Michael Tautschnig, with Kiro
**Domain**: Architecture, front-ends, analyses
**Description**: How front-ends model code whose structure depends on the
    run-time value of a string: the command name of a shell command, `eval`,
    Java reflection, Python's `getattr`, JavaScript's `eval` and computed
    property names. A string analysis over the GOTO program proposes
    candidates, the front-end converts code for each candidate, and residual
    assertions keep the model sound however imprecise the analysis is.

## Problem

In several languages, a string's value decides what code runs or what shape
it has. Examples include:

| Language | Construct | What the value decides |
|---|---|---|
| shell | `$cmd args`, `"$@"` | function, builtin or utility |
| shell | `eval "$text"`, `. "$file"`, `trap "$action" EXIT` | the commands |
| shell | unquoted `$x` | number of fields, pathname expansion |
| shell | `$(( $expr ))`, `[ $op ... ]`, `case $x in $pattern)` | expression, test or pattern syntax |
| shell | `eval "value=\$prefix_$i"` | which variable is read or written |
| Java | `Class.forName(s)`, `getMethod(s)` | class loaded, method called |
| Python | `getattr(o, s)`, `setattr`, `exec`, `importlib` | attribute, code, module |
| JavaScript | `eval(s)`, `Function(s)`, `o[s]` | code, property |
| C | `dlsym(h, s)`, `printf(fmt, ...)` | function called, argument use |

Each front-end currently handles these sites in one of three ways:

- with a value known during conversion: the shell front-end tracks
  `known_literals`, a constant propagation inside the converter;
- with an assertion that the value has no characters that would change the
  structure, as the shell front-end does for pattern characters;
- by reporting the construct as unsupported.

A degenerate candidate set already exists: a dynamic shell command name
dispatches over every function and builtin of the script, with a check that
the name is none of the unsupported ones. The Python, TypeScript and Java
front-ends have no support at all for these constructs.

In the shell census (1,652 scripts installed on Ubuntu 24.04), 136 scripts use
`eval`. The scripts that convert have 377 source lines where `eval`'s text is
not known during conversion. Classified by a rough regular expression:

| Kind | Lines | Example |
|---|---|---|
| dynamic command name that may be `eval` | 122 | `$tool $@` |
| command words with expansions | 100 | `eval rm -f /lib/modules/$version/$f` |
| command held in a variable | 64 | `eval $BYOBU_TEST gsed` |
| output of a utility with a known format | 40 | `eval $(apt-config shell V Dir::Etc)` |
| indirect variable | 37 | `eval "value=\$GIT_PUSH_OPTION_$i"` |
| other command substitution | 14 | `eval "$(_read_fstab_entry "$1")"` |

## Prior work

The analyses of shell scripts are in the reading list of
`doc/architectural/shell-frontend-plan.md`, except SaSh (Lazarek, Lamprou,
Kapetanakis et al., *Ahead-of-Time Analysis of Shell Program Effects*,
SOSP 2026).

- Analyses of shell scripts (Morbig and CoLiS, ABASH, SaSh) exclude `eval`
  or handle it only for constant text. Smoosh and PaSh-JIT execute the
  text, which a static analysis cannot.
- Jensen, Jonsson and Møller, *Remedying the Eval That Men Do* (ISSTA 2012),
  rewrite JavaScript `eval` calls into ordinary code within the fixpoint of
  a dataflow analysis (TAJS):
  - The rewrite runs whenever new values reach the call. It parses the text
    with placeholders for its non-constant parts, and specialises the call
    per constant where the analysis finds a finite set.
  - The rewrite must be monotone; nesting of `eval` is bounded (1 sufficed).
  - The string domain has finite height: constants, JSON text, identifiers,
    identifier characters. Where it cannot rewrite a call, the tool gives up.
  - It removed 33 of 44 hard call sites. Of the 11 failures, 6 are loops
    that lose precision, and 4 are text concatenated away from the call.
- Christensen, Møller and Schwartzbach, *Precise Analysis of String
  Expressions* (SAS 2003), over-approximate each Java string expression by a
  context-free grammar, widened to a regular language (Mohri–Nederhof). Its
  applications check generated SQL against a regular approximation of SQL,
  and resolve `Class.forName`.
- Minamide, *Static Approximation of Dynamically Generated Web Pages* (WWW
  2005), keeps a context-free approximation of PHP output. It models string
  functions as transducers, under whose image context-free languages stay
  context-free. Checks are inclusion in, or disjointness from, regular
  languages.
- Chugh, Meister, Jhala and Lerner, *Staged Information Flow for
  JavaScript* (PLDI 2009), cited by Jensen et al., compute residual checks
  for code that is only known at run time.

## Decision

A **fixpoint of conversion and analysis**, combining the three approaches:
Jensen et al.'s structure, a string domain that widens from finite sets to
regular languages (Christensen et al.), and transducers for string operations
(Minamide). Model checking adds **residual assertions**: whatever the
analysis cannot establish is asserted, not given up on.

### 1. Dynamic sites in the GOTO program

The front-end emits each dynamic site as a call of an intrinsic:

    __CPROVER_dynamic_site(site_id, value)

`site_id` identifies the site and its kind in a table that the front-end
keeps. `value` is the string whose value decides the structure. The front-end
converts the site from a **resolution**, its candidate set for the site:

- With no resolution, it converts the site as today. A shell command name
  becomes the dispatch over all names; `eval` of unknown text is an
  unsupported property.
- With a resolution, it converts the site for those candidates only, as for
  `remove_function_pointers`:

      if(value == c1) code(c1)
      else if(value == c2) code(c2)
      ...
      else assert(false, "value is one of c1, ..., cn")

  A candidate may also be a regular language within a token class, such as
  "a word without shell metacharacters" or "an identifier". Code for such a
  candidate comes from a skeleton parsed once with placeholders. The
  assertion becomes `value ∈ L`.

The intrinsic never reaches symbolic execution. A resolved site is replaced by
its conversion, and an unresolved one by today's conversion, so the program is
symex-ready (see \ref symex-ready-goto).

### 2. The fixpoint

    resolutions := {}
    repeat
      program := front_end.convert(resolutions)        // full conversion
      facts := string_analysis(program)                // abstract values at sites
      new := front_end.resolve(sites, facts, bound)    // candidates per site
      if new == resolutions: break
      resolutions := resolutions ⊔ new                 // monotone
    final := front_end.convert(resolutions)

The front-end re-converts the whole program each round, rather than patching
code into the GOTO program, for three reasons:

- **Conversion context.** Code converted for a site needs the converter's
  state there, which a GOTO-level patch does not have. For the shell, that
  is the enclosing loops (for `break` inside `eval`), function scope,
  `local` variables and known values.
- **Global facts.** Code for a candidate may change facts the converter
  decided upfront. For the shell, that includes which options `set` may
  enable (`collect_options`), which functions exist, and which variables are
  arrays. Re-converting with the larger facts keeps every site consistent.
- **Shared driver.** The loop is the same for every front-end. `link_to_library`
  is a precedent: it is a conversion fixpoint over the GOTO model.

Conversion is cheap compared with solving. For a random sample of 100
converting census scripts, conversion through `--show-goto-functions`
(including library linking) took 0.03 s at the median and 0.26 s at the 90th
percentile, with a maximum of 13.5 s. Each round of the fixpoint adds one
conversion and one analysis.

**Monotonicity:** resolutions only grow (union of candidates, and join of
languages), and the front-end must convert more candidates to more behaviours.

**Termination:**
- The domain widens: a finite set larger than a bound (as in
  `value_set_abstract_objectt::max_value_set_size`) becomes a regular
  language, and regular languages widen to `C*` for their set of
  characters C.
- Code for a candidate can contain new dynamic sites. Their nesting is bounded;
  beyond the bound, a site stays unresolved.

### 3. Soundness does not depend on the analysis

Each resolved site asserts that its value is among its candidates. If the
analysis is unsound or imprecise, the assertion fails in the model and is
reported, the same as an unsupported construct today. The analysis therefore
affects only precision and the number of supported scripts, not soundness. This
is the main difference from Jensen et al., whose rewrite is sound only if
their dataflow analysis is.

This also allows **syntactic candidate sets** as a first step: for example,
the literals that the script assigns to the variable at the site.

### 4. The string analysis

The analysis is an abstract interpretation over GOTO, using the
`variable-sensitivity` framework. It adds a string value object with these
levels:

1. a constant;
2. a set of at most N constants;
3. a regular language, as an automaton;
4. ⊤.

Concatenation is exact on automata. Operations such as prefix and suffix
removal, replacement and case conversion are transducers, applied to
automata. Utilities with a specification of their output contribute that
output's language: for example, `apt-config shell` prints lines of the
form `NAME='value'`.

The analysis runs on the **native** string representation, where SMT-LIB
strings are values and string operations are expressions. That holds whatever
representation the final model uses. The refined representation hides strings
behind pointers to character arrays and `cprover_string_*` function
applications, which a domain would have to see through. For the analysis
rounds, the front-end converts with native strings.

### 5. Front-end interface

Each front-end provides:

- `convert(resolutions)`, which emits `__CPROVER_dynamic_site` for every site
  it cannot resolve;
- `site_kind(site_id)`, which gives the token class the value must belong to:
  a command name, an identifier, a word without metacharacters, or program
  text;
- `resolve(site_id, abstract value)`, which returns either candidates (a set
  of constants, or a language within the site's token class) or nothing.

The shell front-end's sites, in order of the census:

| Site | Candidate use |
|---|---|
| command name | dispatch over the candidates; a utility if the language excludes all functions and builtins |
| `eval`, `.`, `trap` text | parse each constant; skeleton for a language of words without metacharacters |
| indirect variable | if-chain over the candidate variables |
| unquoted expansion | known field count where the language excludes IFS and pattern characters |
| arithmetic text, test operands, patterns | parse each constant |

## Alternatives considered

- **Resolve on the shell AST, before conversion.** This is simpler and keeps
  shell structure, but no other front-end could use it, and it would need a
  second, shell-specific, abstract interpreter.
- **Patch GOTO code into the program at each site (Jensen et al.'s
  rewrite).** This loses the converter's context and its global facts (see
  section 2).
- **Assertions only, without analysis.** We already do this for pattern
  characters. Without candidates it cannot model what the value selects;
  the assertion would amount to "this site is not reached".

## Plan

1. The intrinsic, the driver loop and the resolution table, with the shell
   front-end's dynamic command names as the first sites. With empty
   resolutions, conversion is unchanged.
2. Syntactic candidate sets (literals assigned to the variable), to exercise
   the residual assertions and the census numbers before any analysis exists.
3. The string value-set object in `variable-sensitivity`, on native strings;
   shell sites for command names, `eval`, `.` and `trap` text, and indirect
   variables.
4. Regular languages, transducers for string operations, skeleton parsing,
   and output languages for specified utilities.
5. Other front-ends: Python (`getattr`, `setattr`, `exec`), TypeScript
   (`eval`, computed properties), JBMC (`Class.forName`, `getMethod`).

## Status

- Step 1 is implemented for the shell front-end's dynamic command names.
  - Each one is a site `shell-command-name:FILE:LINE:COLUMN`.
  - `cbmc --show-dynamic-sites` lists the sites, and
    `cbmc --dynamic-resolutions FILE` reads a JSON object of candidates.
  - A resolved name dispatches over its candidates only. A value outside
    them fails the property class `shell-dynamic-site`.
- The analysis of step 3 is implemented for these sites.
  - `cbmc --resolve-dynamic-sites` runs the fixpoint, at most 5 rounds,
    with sets of at most 16 strings; `analyse_dynamic_sites` is in
    `src/analyses`. It reports, for example, `Dynamic sites: 1 resolved,
    0 unreachable, 0 unresolved (2 rounds)`.
  - Front-ends convert for the analysis when the option
    `dynamic-sites-analysis` is set; shell scripts then use native
    strings.
  - On the 244 census scripts with dynamic command names: 92 sites
    resolved, 255 unreachable, 457 unresolved; 1 round for 136 scripts
    and 2 for 108.
  - Cost: 2.1 times a plain conversion at the median, 2.9 at the 90th
    percentile, 4.0 at most; at most 0.85 GB.
  - Verification of the 54 scripts with resolved sites (cvc5,
    `--unwind 2`, 180 s each):
    - 15 finish either way, with the same verdicts, 243 properties
      rather than 540, and 55 s rather than 53 s;
    - 5 that timed out end with a solver error, and 3 that ended with
      one time out;
    - all 21 residual assertions that were decided hold.
- Precision work (29 Oct 2026), measured on the same 244 scripts (804 site
  occurrences):

  | Version | Resolved | Unreachable | Unresolved | Cost: median, 90th percentile, maximum |
  |---|---|---|---|---|
  | first analysis | 92 | 255 | 457 | 2.1, 2.9, 4.0 |
  | summaries, saved copies, arrays | 131 | 255 | 405 | 2.3, 6.7, 49 (2 time outs at 120 s) |
  | and constants, worklist | 131 | 275 | 398 | 2.0, 3.4, 7.9 |

  - Must-assigned symbols are computed per function from summaries: a
    function starts with what holds at all its reachable calls, and a call
    adds what the callee assigns where it returns. Most of the gain comes
    from this.
  - A local `t` whose only assignments are `t := x` is a saved copy of
    `x` (as `local` makes); the restore gives `x` the assigned status it
    had where it was saved.
  - Arrays of shell values are tracked with their elements joined.
  - Constant scalars, such as `shell::fn::NAME` (whether a function is
    defined), `shell::exited` and `shell::exit_trap`, are tracked per
    instruction, and prune the branches they decide. Compared with the
    version before, 4 distinct sites become resolved and 12 unreachable,
    and the 2 scripts that timed out finish. A summary keeps only the
    constants that the function (or its callees) may write.
  - The interprocedural iteration is a worklist in post-order of the call
    graph, in which entries, call facts and summaries only lose facts once
    they exist. Recomputing entries in each round, as the first version
    did, did not terminate on one script once constants were tracked.
  - Constants are in a `sharing_mapt`, as each instruction has its own
    copy; with `std::map`, copying them took 40% of the time.
  - `ait` makes the state of an instruction that no trace reached on
    demand, as the only owner of it. Keeping a reference to it after
    dropping the pointer read freed memory, and made about one run in 400
    find a resolved site unresolved. Precision measurements before this
    fix were off by up to 37 distinct sites.
  - What remains unresolved, by distinct sites (339 of 713): 120 variables
    assigned other expansions, 85 words with literal text and an
    expansion, 54 positional parameters, 32 variables never assigned in
    the script (environment), 15 assigned command substitutions, 15
    variables assigned only literals that are still read with their
    environment value on some path, 7 command substitutions and 11 others.
    117 distinct sites are resolved and 257 unreachable.
  - Verification of the 130 scripts with a resolved or unreachable site
    (cvc5, `--unwind 2`, 180 s and 5 GB each): 60 finish, 56 time out and
    14 run out of memory; all 265 residual assertions that were decided
    hold.
- The marker function must not be interned in programs without sites.
  Interning one more identifier in every program changed the order of
  identifiers, which made the 24 refined string kernels take 216 s rather
  than 39 s.

## Spikes (5 Nov 2026)

Measured on the 244 census scripts that convert and have dynamic command
names (790 site occurrences), with a throwaway driver in `cbmc` and
up to 5 rounds.

**A. `variable-sensitivity` as the domain.** It does not handle strings:
values of `string` type crash the interval and constant code paths
(`constant_interval_exprt`, `to_constant_expr`). Supporting them would
need a new abstract value kind throughout the framework. The analysis
interface also needs two things from the marker: a body (an empty one
suffices) and named parameters with symbols, which
`dynamic_site_marker` now creates.

**B. A dedicated string domain** (sets of at most 16 strings, or any
string), over `ait` with the native representation. The table compares
four variants on the census; values are summed over scripts.

| Variant | Finished | Finite | Unreached | Any | Analysis time | Max memory |
|---|---|---|---|---|---|---|
| flow-sensitive, `std::map` | 217 of 244 | 94 | 248 | 339 | — | 3.9 GB (out of memory) |
| flow-sensitive, `sharing_mapt`, only symbols that reach a site | 242 | 102 | 248 | 431 | 451 s | 3.9 GB |
| same, with a call-stack history of 1 | 202 | 80 | 284 | 231 | 499 s | 3.9 GB |
| flow-insensitive values, plus must-assigned and reachability | 244 | 90 | 269 | 425 | 43 s | 0.9 GB |

- The flow-insensitive variant is ten times cheaper and never fails. It
  is nearly as precise: 90 finite sites against 102.
- Call-stack histories resolve some sites, such as values saved and
  restored around subshells, but fail on 42 scripts.
- The flow-insensitive variant needs one shell-independent refinement.
  A static-lifetime symbol that `__CPROVER_initialize` sets from the
  environment takes its flow-insensitive value only where the program
  has assigned it on every path; elsewhere it is any string. That is a
  must-assigned analysis.

**C. Residual assertions catch analysis bugs.** A first flow-insensitive
version ignored the environment. Of the 53 residual assertions decided
in time, 9 failed, each a real case of a value that the candidates
missed, such as `zcat=${ZCAT:-zstdcat}`. With the fix, all decided
residual assertions hold (20 of 20). This confirms section 3: the model
stays sound when the analysis is wrong.

**D. The fixpoint.**
- Rounds: 1 for 119 scripts, 2 for 112 and 3 for 13. None needed more.
- Each round converts with native strings. The final conversion uses
  the representation the user chose; site identifiers are the same in
  both.
- With the flow-sensitive variant, the loop took 1.7 times as long as a
  single conversion at the median, 2.6 times at the 90th percentile and
  3.8 times at most. Conversion dominates.

**E. Verification.** The 39 scripts with finite sites were run with
cvc5 and `--unwind 2`, at 180 s each.
- 3 scripts that timed out without resolutions finished with them.
- No script went the other way.
- The scripts had 372 properties rather than 447, as dispatches have
  fewer branches.

**Decisions** (revising sections 2 and 4):
- A dedicated string domain in `src/analyses`, not `variable-sensitivity`.
- The flow-insensitive value analysis with must-assigned and
  reachability first. Flow sensitivity and call-stack histories may
  follow where the data show that they pay off.
- A fresh `goto_modelt` each round, with native strings for the rounds.
- Resolutions are joined across rounds, and a site whose value may be
  any string stays unresolved. This makes rounds monotone.

**What the remaining sites need.** By a syntactic classification of the
sites whose value may be any string:
- 161 assign expansions (command substitutions and parameters);
- 103 are words with literal text, such as `$lib/ugc`, which need
  regular languages;
- 55 are positional parameters, which need values of function arguments;
- 32 are never assigned, so their values come from the environment.

## Open questions

- Settled by the spikes: each round builds a fresh `goto_modelt`, and
  the domain sees shell values as structs whose string member it tracks.
- Which candidate bound N (the spikes used 16), and which nesting bound?
- Whether to report, per site, why it remained unresolved. This would help
  to direct the work on precision.
