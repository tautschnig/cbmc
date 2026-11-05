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
- The marker function must not be interned in programs without sites.
  Interning one more identifier in every program changed the order of
  identifiers, which made the 24 refined string kernels take 216 s rather
  than 39 s.

## Open questions

- Should a front-end re-convert in place, or should each round build a fresh
  `goto_modelt`? The shell front-end's state is per conversion, which favours
  a fresh model.
- How should the shell's string structure (set flag, value, integer cache)
  appear to the domain: as a struct with a string member, or as a dedicated
  abstract object?
- Which candidate bound N, and which nesting bound?
- Whether to report, per site, why it remained unresolved. This would help
  to direct the work on precision.
