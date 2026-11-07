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

Specifications of utilities, which the residual properties of templates
call for (what `mktemp` prints), also have prior work:

- CoLiS (Jeannerod, Marché, Régis-Gianas, Sighireanu, Treinen; VSTTE 2017,
  TACAS 2020, STTT 2022) specifies UNIX utilities by their effects on a
  file system, as feature-tree constraints, and symbolically executes
  Debian maintainer scripts with them; it reported over 150 policy
  violations.
- RT (OSDI 2026) types the standard input and output of commands as
  regular languages, with transductions, and checks pipelines against
  them.
- SaSh (SOSP 2026) symbolically executes shell programs, with the effects
  of external commands on a file-system model and an abstract domain for
  word expansion.

What differs here: the specifications would be used inside a bounded
model checker with the whole shell semantics, for the values that
utilities print, as preconditions of the residual properties of dynamic
sites, which keep the model sound whatever the specifications omit.

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
- Languages of candidates (29 Oct 2026). A resolution may include all
  strings that contain a text (`{"values": [...], "containing": "/"}`).
  The analysis tracks this one language, for `/`: any string concatenated
  with strings that all contain a slash contains one. The shell front-end
  converts such a command name as a utility, with branches for functions
  whose names have a slash (bash, zsh). On the 244 scripts, 60 more
  distinct sites are resolved (177 of 713), and resolved occurrences rise
  from 131 to 196 of 804; the 36 decided residual assertions of the 48
  scripts with such sites hold.
- Sites for `.`, `eval` and `trap` text (29 Oct 2026). Each site's marker
  says whether it can use the language of names with a slash (only
  command names can). They are marked only with a dynamic-site option, so
  that the default model is unchanged: marking expands the words as
  values, which adds properties.
  - `.` with a file name that is an expansion: `shell-dot:...`. Each
    candidate with an absolute path is read and run in its branch. On the
    69 census scripts with such commands (119 distinct sites): 5 resolved,
    26 unreachable, 88 unresolved, mostly from the environment
    (`$HOME/.byoburc`, `${DPKG_DATADIR:-...}`) or the arguments.
  - `eval` of text not known during conversion: `shell-eval:...`, the
    arguments as one field each, joined with spaces. Each candidate text
    is parsed and converted in its branch. On 115 scripts (306 distinct
    sites): 25 resolved, 124 unreachable, 157 unresolved. Resolved are
    mostly xdg-utils' `eval '...'$xdg_redirect_output`.
  - The action of a trap on EXIT: `shell-trap:...`; each candidate becomes
    an action. All 11 distinct sites in 48 scripts stay unresolved, as the
    actions are templates such as `"rm -f $tmp"`.
  - Indirect expansion `${!name}` does not occur in the census; the
    indirection there is through `eval` (`eval "echo \${$1+set}"`).
  - What this shows for languages: for text that becomes code, a finite set
    of strings or a glob is not what is needed. The texts are templates of
    literal code with holes that hold values (`rm -f HOLE`). A candidate
    of that form can be converted with each hole as a quoted reference to
    a ghost variable that holds its value at the site, which is exact if
    the residual assertion checks that the value is a single word without
    metacharacters, the token class of the hole.
- The marker function must not be interned in programs without sites.
  Interning one more identifier in every program changed the order of
  identifiers, which made the 24 refined string kernels take 216 s rather
  than 39 s.

- Templates (6 Nov 2026). Candidates are a regular language of a
  restricted shape: a finite union of at most 16 templates
  L0 Σ* L1 … Σ* Ln of at most 5 holes, with concatenation exact. Finite
  sets are templates without holes, and "contains /" is Σ* / Σ*. In JSON,
  a candidate is a string or the array of a template's parts.
  - Command names: the functions and builtins whose names a template
    matches get branches, and other names it matches run a utility. On
    the 244 scripts, 218 distinct sites are resolved (99 with templates)
    rather than 181, and 245 stay unresolved rather than 282.
  - The text of eval and trap: each hole of a template becomes a quoted
    reference to a ghost variable that holds its part, checked to be in
    the class of its position (see `shell_convert_templates.cpp`); a
    hole in a command name makes a dynamic command name, and one in the
    name of a variable dispatches over the script's variables. Parsed
    text has a file name of its own (`FILE#eval-LINE-COLUMN`), so that
    its sites have identifiers of their own.
  - On the 135 scripts with unknown eval and EXIT trap text: 137 eval
    sites and all 11 trap sites have templates, and 36 eval sites stay
    unresolved (157 before). Of the template candidates, 544 are usable,
    20 have a hole elsewhere, and 14 do not convert for other reasons
    (such as `${1+"$@"}`).
  - Verifying the 70 scripts with usable templates (cvc5, `--unwind 2`,
    180 s) decides 336 site assertions, which all hold, and 297 class
    checks, of which 12 fail: `eval set -- $VAR`, whose values have
    blanks by design (it needs a model of field splitting of the parts),
    and trap actions with file names from `mktemp`, whose output the model
    does not restrict (it needs specifications of utilities' output).
  - With refined strings, the decomposition of a value into parts was
    slow, and wrong for a last part: it compared payloads (a length and a
    pointer) rather than strings. With the comparison fixed and the
    encodings measured (a prefix test, the last bytes compared with the
    last part, classes checked on the value from each part's start), the
    template-eval test takes 68 s rather than not finishing in 30
    minutes; cvc5 takes 3 s. What remains is the string solver's
    instantiation of quantified axioms: with two templates, the index
    sets grow to about 300 terms and the SAT problem to 780 000
    variables over 8 refinements.

- Utility output, split holes and quoted holes (7 Nov 2026).
  - The output of a command substitution that is one call of a specified
    utility follows its specification, an assumption about the utility
    that the platform provides (`shell_convert_utilities.cpp`). The first
    is `mktemp`, as the union of GNU, BSD and busybox outputs. With it,
    `trap "rm -f $f" EXIT` for `f=$(mktemp /dev/shm/x.XXXX)` is shown to
    be one word; without a template, `$TMPDIR` may have a blank, which is
    reported.
  - An unquoted hole in a word that is split into fields may hold several
    words, and becomes an unquoted reference, so that `eval "set -- -v
    $opts"` splits as parsing would. cvc5 is slow on field splitting of
    unknown values (214 s for `set -- -v $x`, 8 s with refined strings),
    so 8 census scripts with `eval set -- "$(getopt ...)"` time out at
    180 s with cvc5 where the class check failed at once before. Their
    texts are getopt output, with quoted words: they need a specification
    of getopt's output and a class of quoted word lists.
  - With native strings, a hole in single quotes may hold `'\''`, as
    quoting a value for the shell makes; its ghost variable holds the
    part with each `'\''` replaced by `'`. The string refinement solver
    did not decide this in 300 s (cvc5: 4 s), so refined strings keep the
    stricter class. The census texts with such holes (zgrep and
    relatives) build the quoted list in a loop, which no template
    describes.
  - `${1+"$@"}` alone is `"$@"`; it was not supported.
  - On the 70 census scripts with usable templates, 4 class checks fail
    rather than 12 (two trap actions with names from `mktemp` without a
    template, or with a variable template, and `eval "echo
    \${XDG_$1_DIR:-...}"` with an argument that may not be a name); 33
    rather than 24 time out.
  - The string refinement solver reported `case "$1ab" in *ab*)` as not
    matching (a false alarm, also before these changes): each
    counter-example instance of index_of's not-contains constraint was at
    one position of the existential, chosen by the witness function, which
    the next model moved, until the solver took the model as a
    counterexample. The counter-example lemmas now instantiate the
    existential over its whole range in the model (at most 64 positions),
    at the violating position and at each index term with its value. The
    reproducers decide in under a second; the C, Java and Python string
    suites are unchanged in results and time. Instantiating violated
    universal axioms over their ranges in the same way did not help the
    remaining case (`case "$(mktemp)" in */tmp.*)`), where the
    concatenation axioms of a nondeterministic choice of parts do not
    converge, and was not kept.
  - Two such pattern matches in one program still do not finish with
    refined strings (in 30 minutes), although each takes under a second
    on its own; this was also so before.

- Design for `eval set -- "$(getopt ...)"` (not implemented). getopt's
  output is a regular language that is not a template: options ` -x` and
  ` --long`, their arguments and the operands as ` 'value'` with `'\''`
  for `'`, and ` --` before the operands. Two parts are needed:
  - a specification of getopt (util-linux): the output for "$@" by the
    option strings, with permutation of operands, abbreviations of long
    options, optional arguments (`::`), and status 1 on errors;
  - a hole class of quoted word lists, whose eval sets each word,
    dequoted, as a field. Membership is a regular expression, which only
    native strings have; with refined strings, a bounded decomposition
    into at most --shell-max-arguments words would be needed.

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

## Spike: templates (6 Nov 2026)

A restricted regular language: a finite union of templates
L0 Σ* L1 … Σ* Ln, literal parts with holes, at most 5 holes and 16
templates. Concatenation is exact; an unknown value is a hole, and
adjacent holes merge. The spike (branch `spike-templates`) replaced the
value domain by this one and printed the templates of each site; the
script `scripts/shell-frontend/spike-classify-templates.py` parsed each
template with placeholders for its holes and classified where the holes
sit.

On the 188 census scripts with `.`, `eval` or `trap` sites, templates
(rather than any string) are found for 120 of the 157 unresolved `eval`
sites, all 11 `trap` sites and 84 of the 88 `.` sites. For the `eval`
and `trap` sites, by the worst hole of their templates:

| Hole position | eval | trap |
|---|---|---|
| a word, or part of one, as an argument or assignment value | 52 | 10 |
| the command name (`$cmd "$1"`, `$dir/msg`) | 32 | |
| a variable name (`${kopt_HOLE}`, `${HOLE+set}`, `_set=$HOLE`) | 28 | |
| other | | 1 |
| does not parse (quoting through `sed "s/'/'\\''/g"`) | 8 | |

What follows for the design:

- The value domain becomes finite unions of templates; the finite sets
  of strings and "contains /" (Σ* / Σ*) are special cases.
- The front-end parses each candidate template once, with each hole a
  placeholder, and derives the token class of each hole from its
  position: a word without blanks or metacharacters (unquoted), without
  `"`, `$`, `` ` `` or `\` (in double quotes), or without `'` (in single
  quotes). Each hole is then a quoted reference to a ghost variable that
  holds its part of the value, and the residual assertion checks that the
  value is the template with parts in their classes. The classes exclude
  the characters that the literals start with, so that the parts are
  determined.
- A hole as a command name becomes a dynamic command name site, which
  exists. This covers 84 of the 131 sites (64%).
- A hole in a variable name is an indirect variable: its candidates are
  the script's variables whose names match, and any other name reads an
  unset variable or one from the environment. This covers 28 more (85%
  in all).
- `.` sites gain nothing: a template path names no file to read.
- Text built with `sed` to quote values needs transducers (the papers'
  approach), and stays unresolved.

## Open questions

- Settled by the spikes: each round builds a fresh `goto_modelt`, and
  the domain sees shell values as structs whose string member it tracks.
- Which candidate bound N (the spikes used 16), and which nesting bound?
- Whether to report, per site, why it remained unresolved. This would help
  to direct the work on precision.
