# Shell Front-End for CBMC — Specifications, Prior Work, and Plan

Status: **Step 0 (choosing normative references)**. No code yet.

This document records which language references the shell front-end
cites (the role ECMA-262 played for TypeScript and the Python Language
Reference played for Python), what academic and tooling work exists,
and the resulting first-cut plan. The generic front-end guidance in
`building-a-new-frontend.md` and `cbmc-frontend-architecture.md`
(on the `typescript-frontend` branch) applies here too. In particular,
the per-spec soundness review methodology requires a spec with stable,
citable section numbers.

---

## 1. The dialect landscape

"Shell" covers three unrelated language families with very different
semantics:

| Family | Members | Data model | Normative spec? |
|--------|---------|------------|-----------------|
| **POSIX / Bourne** | `sh` (dash, busybox ash, posh, yash), bash, ksh93, mksh, zsh | Everything is a string; words undergo multi-stage expansion; processes and pipes | **Yes**: POSIX.1-2024. Per-dialect reference manuals cover extensions. |
| **PowerShell** | Windows PowerShell 5.1, PowerShell 7.x | .NET objects; typed pipeline of objects; C#-like expressions | **Partial**: PowerShell Language Specification 3.0 (2012, frozen), plus `about_*` topics for later features |
| **Windows Batch** | `cmd.exe` (`.bat`/`.cmd`) | Strings; `%var%` vs `!var!` delayed expansion; multi-phase line parser | **No**: only informal Microsoft command docs plus community reverse-engineering |

The POSIX family shares one core semantics. Bash, zsh, and ksh are
best treated as **deltas over POSIX**, as Bash's own manual does.
PowerShell and Batch would be separate front-ends that at most share
infrastructure such as string modelling and the process/environment
model.

---

## 2. Normative references (recommended citation targets)

Local copies are under `~/shell-specs/`, outside the repository.
Licensing notes are in §2.5.

### 2.1 Base: POSIX.1-2024 (IEEE Std 1003.1-2024, Open Group Base Specifications Issue 8)

- **Shell & Utilities volume (XCU), Chapter 2 "Shell Command
  Language"** is the core. It has 66 numbered sections, extracted to
  `~/shell-specs/posix-xcu2-sections.txt`. Representative sections:
  2.2 Quoting (now including 2.2.4 Dollar-Single-Quotes, new in Issue 8),
  2.3 Token Recognition, 2.5 Parameters and Variables,
  2.6 Word Expansions (2.6.1 Tilde through 2.6.7 Quote Removal),
  2.7 Redirection, 2.8 Exit Status and Errors, 2.9 Shell Commands
  (2.9.2 Pipelines, 2.9.3.1 Asynchronous AND-OR Lists, 2.9.4 compound
  commands), 2.10 Shell Grammar (a yacc grammar with lexical
  conventions), 2.12 Signals and Error Handling, 2.13 Shell Execution
  Environment, 2.14 Pattern Matching Notation, 2.15 Special Built-In
  Utilities.
- **XCU Chapter 3 (Utilities)** has 170 utility pages, for example
  `test`, `printf`, `read`, `cd`, `echo`, `kill`, `wait`, `getopts`.
  This is the shell's "standard library", the analogue of ECMA's
  built-in objects. Regular built-ins (`test`, `[`, `printf`, `read`)
  need models; external utilities (`grep`, `sed`, `rm`, …) need
  *specifications*. See §4.3.
- **Base Definitions (XBD)**: §3 Definitions, §8 Environment
  Variables, §9 Regular Expressions (BRE/ERE, needed for `expr`,
  `grep`, `case` interactions), §12 Utility Conventions (option
  parsing).
- **Rationale (XRAT) C.2** records the intent behind XCU §2. It is
  invaluable when the normative text is ambiguous.
- **Citation convention** (proposed): `POSIX.1-2024 XCU §2.6.2` in
  comments and commit messages. The HTML anchors (`tag_19_06_02`) are
  stable within the edition.
- A Corrigendum (P1003.1-2024/Cor 1) is in progress, so track errata.

**Why POSIX is the base:** every POSIX-family dialect claims (partial)
conformance, the text is rigorous enough that Smoosh (§3.1) could
mechanize it, and the conformance test infrastructure in that line of
work (the Smoosh test suite, PaSh's use of it) is keyed to it.

### 2.2 Dialect delta: Bash 5.3 Reference Manual (Edition 5.3, 18 May 2025)

- Source is `doc/bashref.texi` from the GNU bash-5.3 tarball (local
  copy: `~/shell-specs/bash-5.3/doc/bashref.texi`). Texinfo gives
  stable numbering, for example §3.5.3 Shell Parameter Expansion,
  §3.2.5 Compound Commands, §6.5 Shell Arithmetic, §6.7 Arrays,
  §6.4 Bash Conditional Expressions (`[[ … ]]`).
- **§6.11 "Bash and POSIX"** enumerates exactly where Bash's default
  mode deviates from POSIX and what `set -o posix` changes. It is the
  ready-made delta list between the two specs.
- **§6.12 Shell Compatibility Mode** (`BASH_COMPAT`) documents
  version-dependent behaviour; we should pin a target version.
- Citation convention (proposed): `Bash 5.3 §3.5.3`.
- Bash is the most important dialect in practice: CI scripts, the
  Koala benchmark programs (§3.4), and installer scripts.

### 2.3 Dialect delta (later): Zsh manual (zsh 5.9; `Doc/Zsh/*.yo` from current master)

- Local copy: `~/shell-specs/zsh-src/Doc/Zsh/`, 71 yodl files. The
  chapters are named (Shell Grammar, Expansion, Parameters, …) rather
  than numbered, so citations would use chapter and section names,
  e.g. `zsh: Expansion > Parameter Expansion Flags`.
- Zsh is *not* POSIX by default (no word splitting of unquoted
  parameters, 1-based arrays, glob qualifiers). `emulate sh` and
  `emulate ksh` switch to POSIX-like behaviour. This makes zsh a
  larger delta than bash, so defer it.
- ksh93 and mksh: reachable through `man` pages only. Low priority.

### 2.4 Separate front-ends (much later)

**PowerShell.** The *Windows PowerShell Language Specification 3.0*
(Microsoft, Dec 2012) is published as markdown in
`MicrosoftDocs/PowerShell-Docs`, under
`reference/docs-conceptual/lang-spec/` (local copy:
`~/shell-specs/ps-docs/…`). It has 13 numbered chapters (2 Lexical
Structure … 8 Statements … 13 Cmdlets) plus Appendix B, a full
lexical and syntactic grammar, with about 300 numbered subsections.
The spec states it is frozen at v3.0 and will not be updated;
classes, `using`, ternary, `??`, and pipeline chains are documented
only in `about_*` topics. Citation: `PS3 §7.1.3`. PowerShell's object
pipeline and .NET type system make it closer to a dynamic OO
language, so it may borrow more from the Python and TypeScript
front-ends than from the POSIX one.

**Windows Batch.** There is no specification. The best descriptions
are Microsoft's command reference (`windowsserverdocs`) and the
community-derived multi-phase parsing model: the well-known Stack
Overflow answer "How does the Windows Command Interpreter (CMD.EXE)
parse scripts?" (phases 0–7: percent expansion, special-character
handling, delayed `!` expansion, etc.). I found no academic semantics
of Batch. Without a spec and without a Windows host for differential
testing, Batch is a research project in its own right. Park it.

### 2.5 Licensing of the reference texts

- POSIX HTML archive: "personal use" download. **Never commit it.**
  Cite section numbers only and do not paste normative text beyond
  short quotes.
- Bash manual: GNU FDL. Zsh manual: zsh licence. PowerShell docs:
  CC-BY-4.0 (docs) / MIT (code). Short quotations in comments are
  fine; keep the copies out of tree anyway.

---

## 3. Prior academic and tooling work

All papers cited here are in `~/shell-specs/papers/`. Download sources
are in `urls.tsv` and file names are mapped in `~/shell-specs/README.md`.

### 3.1 Semantics of the POSIX shell

| Work | What it is | Use for us |
|------|-----------|------------|
| Greenberg & Blatt, *Executable Formal Semantics for the POSIX Shell* (**Smoosh**), POPL 2020 | Mechanized, executable small-step semantics in Lem, cross-tested against bash, dash, zsh, OSH, mksh, ksh93, yash. Uses **libdash** (dash's parser as a library). | The **reference semantics** to cross-check our model against. Its test suite gives ready-made probe tests and documents where real shells disagree with POSIX. |
| Greenberg, *Word Expansion Supports POSIX Shell Interactivity*, ‹Programming› 2018 companion | Argues expansion order and semantics is the heart of the shell | Guides how we structure the expansion engine (§4.1) |
| Greenberg, *The POSIX Shell is an Interactive DSL for Concurrency*, DSLDI 2018 | Pipelines, `&`, `wait`, traps as concurrency primitives | Framing for the concurrency model (§5) |
| Greenberg, *Understanding the POSIX shell as a programming language*, OBT 2017 (talk) | Survey of semantic pitfalls | Pitfall checklist |
| Jeannerod, Marché, Treinen, *A Formally Verified Interpreter for a Shell-like Programming Language*, VSTTE 2017 (CoLiS language) | A "sane" intermediate language for shell, interpreter verified in Why3 | Design reference for our internal IR between shell AST and GOTO |

### 3.2 Parsing

| Work | Use for us |
|------|-----------|
| Régis-Gianas, Jeannerod, Treinen, *Morbig: A Static Parser for POSIX Shell*, SLE 2018 (extended version in J. Computer Languages 2020) | Explains why shell is hard to parse statically: lexing depends on parsing state, here-docs, alias expansion, and `eval`. Morbig handles a well-defined realistic subset using Menhir's incremental LR(1) and emits JSON concrete syntax trees. Defines what "statically parseable" means. |
| libdash (Greenberg) | dash's real parser as a C library with OCaml and Python bindings, used by Smoosh and PaSh. Faithful to one real POSIX shell. |
| mvdan/sh (`shfmt --to-json`) | Go parser for POSIX, Bash, mksh, and (since a 2026 release) Zsh, with typed JSON AST output and a `-ln` dialect flag. Ubuntu packages 3.8.0, which predates Zsh support. |
| tree-sitter-bash, Oils (OSH) | Alternatives. OSH has the most detailed bash-compatibility documentation. |
| PowerShell `System.Management.Automation.Language.Parser` | The official parser API (`ParseFile`/`ParseInput` → AST). Needs `pwsh`, which is not installed. |

### 3.3 Static analysis and verification of shell scripts

| Work | Use for us |
|------|-----------|
| Mazurak & Zdancewic, *ABASH: Finding Bugs in Bash Scripts*, PLAS 2007 | Earliest abstract interpreter for bash. Uses **signatures for external programs** (a precursor of command specs). Found bugs in 20 of 49 scripts. |
| Becker, Jeannerod, Marché, Régis-Gianas, Sighireanu, Treinen, *Analysing Installation Scenarios of Debian Packages*, TACAS 2020 | **Symbolic execution** of about 28k Debian maintainer scripts, with **feature-tree constraints** modelling the file system and specs of about 30 core utilities. The closest prior work to bounded model checking of shell. |
| *The CoLiS Platform for the Analysis of Maintainer Scripts in Debian Software Packages*, STTT 2022 | Journal version with the full toolchain and utility specifications |
| Jeannerod & Treinen, *Deciding the First-Order Theory of an Algebra of Feature Trees with Updates*, IJCAR 2018; Jeannerod, Régis-Gianas, Treinen, *Having Fun With 31.521 Shell Scripts* (2017 report, obtained) | The file-system logic, and an empirical survey of which features real scripts use |
| Lazarek, Lamprou et al., *Ahead-of-time Analysis of Shell Program Effects*, **SOSP 2026** (best paper) | The state of the art for static effect analysis of shell programs |
| Lazarek et al., *From Ahead-of- to Just-in-Time and Back Again: Static Analysis for Unix Shell Programs*, HotOS 2025 | Position paper for the effect-analysis line |
| Zhao, Li, Lazarek et al., *Trees, Walks, and Words: A Logic for File System Interactions*, **POPL 2027** | A modern file-system logic. Compare against CoLiS feature trees when choosing our FS model. |
| Li, Lazarek et al., *Rt: Regular Types for the Streaming Shell*, OSDI 2026 | Types of pipeline streams as regular languages. Relevant to modelling `grep`/`sed`/`cut` pipelines. |
| Lamprou et al., *Caruca: Specification Mining for Opaque Software Components*, arXiv 2510.14279 | LLM-assisted mining of command specs. A possible source of models for external utilities. |
| Narsipur, *Towards Automated Reasoning for Shell Programs* (Shseer), Brown thesis 2024, and *Statically Analyzing Shell Scripts and Their Effects in Context* | File-tree logic prototypes |
| Lamprou, Zhu et al., *Controlling Opaque-Component Effects with Semisolates and Try*, OSDI 2026 | Runtime containment of effects. A dynamic complement to our static view. |

### 3.4 Systems work (parallelization, benchmarks, concurrency)

| Work | Use for us |
|------|-----------|
| Vasilakis, Kallas et al., *PaSh*, EuroSys 2021; Handa, Kallas, Vasilakis, Rinard, *An Order-Aware Dataflow Model for Parallel Unix Pipelines*, ICFP 2021 | A formal dataflow model of pipelines. It tells us when pipeline concurrency is **unobservable** and can be sequentialized soundly (§5). |
| Kallas et al., *PaSh-JIT*, OSDI 2022; Mustafa et al., *DiSh*, NSDI 2023; Huang et al., *Fractal*, NSDI 2026 | How dynamic shell state (expansions, filesystem) blocks static reasoning, and which run-time facts would need to be nondet in our model |
| Liargkovas et al., *Executing Shell Scripts in the Wrong Order, Correctly*, HotOS 2023, and *hS*, OSDI 2026 | Dependency and effect tracking between commands. Relevant to partial-order reduction of concurrent commands. |
| Lamprou et al., **Koala** benchmarks, ATC 2025 (best paper) and TOCS 2026 | Real-world shell program corpus. Our "integration corpus" (analogous to the 35-repo TypeScript audit). |
| Greenberg, Kallas, Vasilakis, *Unix Shell Programming: The Next 50 Years*, HotOS 2021, and the panel report (arXiv 2109.11016) | Research agenda and open problems |

### 3.5 PowerShell and Batch

| Work | Use for us |
|------|-----------|
| Li et al., *Effective and Light-Weight Deobfuscation and Semantic-Aware Attack Detection for PowerShell Scripts*, CCS 2019 | AST-subtree emulation for deobfuscation. Shows the dynamic-code (`Invoke-Expression`) problem in PowerShell. |
| *PowerPeeler: A Precise and General Dynamic Deobfuscation Method for PowerShell Scripts*, arXiv 2406.04027 (2024) | Same problem space, instruction-level |
| Batch | No academic semantics found. Malware-analysis blog material only. |

---

## 4. Key semantic modelling problems (POSIX family)

These are the analogues of "strings, prototype chains, async" in
TypeScript:

1. **Word expansion (XCU §2.6) is the core semantics.** Tilde,
   parameter, command substitution, arithmetic, field splitting
   (`IFS`), pathname expansion, and quote removal, in that order,
   produce *lists of fields* from words. Nearly every shell bug class
   (unquoted variables, empty-field disappearance, glob injection) is
   an expansion bug. The model needs symbolic strings and symbolic
   lists of strings. The refined-string work on `typescript-frontend`
   (and P4.1 SMT strings) is directly relevant, so we may want to
   rebase on it later.
2. **The environment and subshell semantics (§2.13).** `( … )`,
   pipeline stages, and command substitution run in subshell
   environments, which are *copies* of the parent environment.
   Assignments there do not propagate back. This is a classic
   verification target ("why is my variable empty after
   `cmd | while read`?").
3. **External commands are opaque.** A script's meaning depends on
   `rm`, `grep`, `sed`, `mkdir`, and so on. We need:
   - built-in models for POSIX regular built-ins (`test`, `printf`,
     `read`, `cd`, `export`, …);
   - a *specification mechanism* for external utilities. Options:
     ABASH-style signatures, CoLiS-style utility specs over a
     file-system model, or Caruca-mined specs. The default for an
     unknown command is a nondet exit status and nondet stdout, with
     a havoc of file-system state it may touch.
4. **A file-system model** for globbing, `test -e`, redirections, and
   `rm`. Candidates are CoLiS feature trees and the POPL 2027
   trees/walks/words logic. For BMC, start with a bounded set of
   symbolic paths.
5. **Control flow from exit statuses:** `&&`, `||`, `if`, `while`,
   `set -e` (with its notoriously subtle exemptions, §2.8.1 and
   `set -e` in XCU `set`), `trap … ERR/EXIT`.
6. **Dynamic code:** `eval`, `.`/`source`, aliases (§2.3.1), `trap`
   strings, and `sh -c`. Handle these like TypeScript's `eval`:
   constant-string arguments get parsed and inlined, anything else
   becomes a sound havoc with a warning.
7. **Verification properties** (the analogue of the TypeScript
   security primitives): unquoted-expansion safety, no option
   injection (`--`), no deletion outside a directory (the classic
   `rm -rf "$DIR/"*` with empty `DIR`), `set -u` violations,
   exit-status contracts, and injection into `eval`/`sh -c`.

---

## 5. Concurrency

Concurrency enters through pipelines (§2.9.2: every stage is a
concurrent process), asynchronous lists (`&`, §2.9.3.1), `wait`,
signals and `trap` (§2.12), and job control (§2.11, interactive only,
so out of scope).

Proposed staging:

1. **Sequential pipeline model first** (like TypeScript's default
   sequential async). Execute stages left to right with pipe contents
   modelled as a bounded string buffer. The PaSh order-aware dataflow
   model justifies this as sound whenever stages are stream filters
   that do not share other state (files, environment). Flag pipelines
   whose stages touch shared files.
2. **Opt-in true concurrency** using CBMC's existing thread support
   (`goto-symex` multi-threading with the partial-order encoding of
   Alglave, Kroening, Tautschnig, CAV 2013). Map each background job
   or pipeline stage to a thread, and pipes to bounded channels.
3. **Performance work** if (2) proves necessary. Candidates are
   ordering-consistency theory as in Deagle (He, Sun, Fan, PLDI 2021,
   built on CBMC), lazy sequentialization (Lazy-CSeq), and
   effect-based independence from the hS/SOSP 2026 work to prune
   interleavings of non-interfering commands.

---

## 6. Parser evaluation (done 2026-10-29)

### 6.1 Setup

- **shfmt built from source** (mvdan/sh `e96ef1da`, 2026-10-27,
  v3.14.0+; reports `v3.14.1-0.20261027094050-e96ef1dae52c`) with
  Go 1.26.10, installed user-locally in `~/tools/go`, binary at
  `~/tools/bin/shfmt`. Dialects: `-ln posix|bash|mksh|bats|zsh`.
  Ubuntu's packaged 3.8.0 lacks zsh.
- **Reference shells installed for differential testing:** bash 5.2.21,
  dash 0.5.12, zsh 5.9, ksh93u+m 1.0.8, mksh 59c, yash 2.55, posh 0.14.1,
  busybox ash.
- Probe tools: `scripts/shell-frontend/shfmt_astview.py` (compact AST
  viewer) and `scripts/shell-frontend/parse_corpus.sh` (corpus
  parse-rate driver).

### 6.2 What the AST gives us

- `shfmt --to-json` reads **stdin only** and writes one typed JSON tree.
  Interface-typed nodes carry `"Type"`; every node carries `Pos`/`End`
  as `{Offset, Line, Col}`, which maps directly to `source_locationt`.
- 48 node types in total (`syntax/nodes.go`). The POSIX subset is
  about 20: `File Stmt CallExpr Assign Redirect BinaryCmd Subshell
  Block IfClause WhileClause ForClause WordIter CaseClause CaseItem
  FuncDecl Word Lit SglQuoted DblQuoted CmdSubst ParamExp Expansion
  ArithmExp BinaryArithm UnaryArithm ParenArithm`. The rest are bash,
  mksh, or zsh extensions (`TestClause` for `[[ ]]`, `DeclClause`,
  arrays, `ProcSubst`, `ExtGlob`, `CoprocClause`, …).
- **Words keep their quoting structure** as a list of parts (`Lit`,
  `SglQuoted`, `DblQuoted`, `ParamExp`, `CmdSubst`, `ArithmExp`).
  This is exactly what §2.6.5 field splitting and §2.6.6 pathname
  expansion need, since both depend on what was quoted.
- Operators are strings (`"&&"`, `"|"`, `">&"`, `";;"`); `&` is a
  `Background` flag on `Stmt`; here-doc bodies are attached to the
  `Redirect` as a `Hdoc` word with expansions already split out.
- **Globs stay literal** (`*.txt` is a `Lit`). This is correct, since
  pathname expansion is a run-time step, but the converter must
  re-scan unquoted literal parts for pattern characters.

### 6.3 Fidelity gaps found

| Input | Real shells | shfmt | Consequence |
|-------|-------------|-------|-------------|
| `$'tab\there'` (POSIX.1-2024 §2.2.4) | bash, zsh, ksh93, mksh, busybox: TAB; dash 0.5.12, yash, posh: literal `$tab\there` | `-ln posix`: wrong (literal `$` + `'…'`); `-ln bash`: correct (`SglQuoted{Dollar:true}`) | shfmt's POSIX mode targets pre-Issue-8 POSIX. Our "POSIX" means 2024, so either parse POSIX scripts in bash mode and reject bashisms ourselves, or patch shfmt. |
| `op=+; echo $((1 $op 2))` | dash and bash: `3` | parse error in all dialects | §2.6.4 says arithmetic is evaluated on the text *after* expansion. shfmt parses `$(( ))` statically. We need our own arithmetic parser applied after expansion; this is needed regardless of which word parser we use. |
| aliases, `eval`, `.`, `sh -c`, `trap` strings | parsed at run time | unparsed strings | Inherent to static parsing (see Morbig). Parse constant strings at conversion time. |

### 6.4 Corpus robustness

All `#!/bin/sh`, bash, dash, and zsh scripts on this machine (Debian
maintainer scripts plus `/usr/bin`, `/usr/sbin`, `/etc/init.d`):
**1,266 scripts** (1,190 sh, 73 bash, 2 dash, 1 zsh), each parsed in
the dialect its shebang names.

- **1,265 / 1,266 parse (99.9%)**, the same rate Morbig reports for
  Debian's 31,521 maintainer scripts.
- The single failure is a real bug, not a parser problem.
  `/usr/bin/xdg-settings` (xdg-utils) is `#!/bin/sh` but uses the
  bashism `${browser:1}` (lines 736, 1092). On Ubuntu `/bin/sh` is
  dash, which reports `Bad substitution` on that code path. Dialect-
  aware parsing alone finds portability bugs.
- Cost: about 13 ms per file through the subprocess. JSON is verbose:
  a 2,819-line script yields 5 MB, mostly position records.

### 6.5 Decision

1. **Use `shfmt --to-json` now**, as the TypeScript front-end used the
   tsc API. It gives broad multi-dialect coverage immediately and lets
   us concentrate on the hard part, expansion semantics.
2. **Convert into our own internal shell AST first, not straight to
   GOTO.** The converter then depends on that IR rather than on
   shfmt's schema, which is what makes a native parser (§6.6) a
   drop-in replacement later. The IR also normalises dialect
   differences such as `$'…'`.
3. **Write a native arithmetic-expression parser immediately.** It is
   small (C-like precedence grammar, §2.6.4 / XCU `expr` rules), it is
   required anyway for post-expansion arithmetic, and it is the first
   piece of a native parser.
4. Use the corpus (`parse_corpus.sh`) and the Smoosh test suite as
   parser regression and differential-parse corpora.

### 6.6 Could we have a native parser?

Yes, and it is more tractable than it first looks:

| Data point | Size |
|------------|------|
| POSIX.1-2024 XCU §2.10.2 grammar | ~45 productions, 424 lines, **already in yacc notation** |
| dash `parser.c` + `parser.h` + `nodetypes` (BSD licence) | 2,076 lines of C (POSIX plus dash quirks) |
| mvdan/sh `syntax/` (non-test, all dialects) | 10,366 lines of Go (parser.go 3,128, lexer.go 1,445, parser_arithm.go 410) with 98 dialect checks |
| bash | its parser *is* a yacc grammar (`parse.y`) with lexer hooks |

The difficulty is the lexer, not the grammar. §2.3 token
recognition and the §2.10.1/§2.10.2 context rules (reserved words are
only recognised in command position, assignment-word detection, `case`
patterns, here-documents read after the next newline, alias
substitution) require **feedback from the parser to the lexer**.
Morbig solved this with Menhir's incremental LR(1) API; bash does it
with lexer hooks around bison; dash and mvdan/sh use hand-written
recursive descent.

Options, in order of preference:

- **(a) Bison grammar transcribed from §2.10.2 plus a hand-written
  context-aware lexer.** This is CBMC's established approach
  (`src/ansi-c/`), cites the standard production by production, and
  needs no run-time dependency. Bison's push-parser interface or
  explicit lexer state flags can provide the feedback.
- **(b) Hand-written recursive descent in C++**, modelled on dash or
  mvdan/sh. Simpler feedback handling; POSIX-only would be roughly
  2–3 kLOC.
- **(c) Vendor libdash** (dash's parser extracted as a library,
  BSD-licensed). Highest fidelity to one real shell, but it is tied to
  dash's allocator and longjmp error handling, and it is POSIX/dash
  only.

A native parser would also allow parsing strings that only become
known during conversion (`eval "…"`, `sh -c "…"`, `trap` handlers,
post-expansion arithmetic) without a subprocess per string. That is a
stronger argument for it than parse speed.

Plan: keep shfmt as the multi-dialect front door. Build the native
POSIX parser (option a) once the IR and semantics have stabilised.
Validate it by differential parsing against shfmt (bash mode) and dash
on the corpus. Bash, zsh, and mksh would remain on shfmt until there is
a reason to go native for them.

---

## 7. Next steps

1. ~~Install shfmt and reference shells~~ (done, §6.1). Fetch the
   Smoosh test suite as a conformance corpus.
2. ~~Define the internal shell IR and a JSON → IR reader for shfmt's
   schema~~ (done: `src/shell/shell_ast.h`, `shfmt_reader.cpp`).
   Bash's `DeclClause` is rebuilt as the simple command POSIX mode
   yields, so declaration utilities (XCU 2.9.1.1) are handled by
   command name in every dialect.
3. ~~Skeleton `src/shell/` front-end~~ (done: `shell_languaget`,
   `--show-parse-tree`; `typecheck` errors until a converter exists).
   Still open: language selection is by extension only (langapi);
   `#!` sniffing for extensionless scripts needs a langapi change.
4. ~~Native arithmetic-expression parser~~ (done: `shell_arith.cpp`,
   with dialect options for `++`/`--` and the comma operator, each
   checked against dash, bash, mksh, zsh, ksh93, yash, posh, busybox).
   On 843 local scripts the reader handles 783 completely and never
   crashes; the rest use bash/zsh extensions or fail in shfmt.
5. ~~Minimal semantics~~ (first slice done: `shell_converter.cpp`,
   `shell_runtime.cpp`; see §7.1). Still open from the original list:
   functions, subshells, field splitting, `$@`/`$*`.
6. Word expansion done properly (§2.6 in full), with a per-spec
   review of XCU §2.6 as the first soundness review.
7. Decide the file-system and external-command specification
   strategy (CoLiS utility specs vs POPL 2027 logic vs ABASH
   signatures).

### 7.1 The first converter (done 2026-10-29)

`shell_convert` turns the IR into `shell::main`, called from
`__CPROVER__start` after `__CPROVER_initialize`, and CBMC verifies it.

**Values.** Every value is a bounded string: a C struct
`__CPROVER_shell_string {set, length, data[N]}` with
N = `--shell-max-string-length` (default 64). The helpers (equality,
concatenation, integer conversion, nondeterministic strings) are C
code generated with their loops unrolled (`shell_runtime.cpp`), added
through the C front-end without a preprocessor. They are hidden in
traces, so `--unwind` bounds for the script do not cut them short, and
checks such as `--bounds-check` are disabled inside them.

**Inputs and assumptions.** These are explicit and sound for scripts
whose inputs fit the bound:
- Variables the script does not assign come from the environment:
  set or unset, with any contents without NUL bytes.
- `$1`, `$2`, … are set iff `$#` is large enough; `$#` is at most
  2^31−1, and `$0`, `$$`, `$-` are nondeterministic.
- External commands (anything not modelled as a builtin) have a
  nondeterministic exit status and cannot change shell variables.
  Command substitution yields a nondeterministic string and status.
  `echo` and `printf` are treated the same way, since writes can fail.
- File tests, `-ef`/`-nt`/`-ot`, and `<`/`>` in `test` are
  nondeterministic. So are the results POSIX leaves unspecified.
- Inputs are at most N bytes; strings the script builds that exceed N
  fail the `shell-string-length` property.

**Checks.** Arithmetic is ISO C `long` (32-bit in mksh). Division by
zero, overflow, out-of-range shifts, and variables whose values are not
integer constants are properties (`division-by-zero`, `overflow`,
`undefined-shift`, `shell-arithmetic`). They are asserted, then assumed.
`__CPROVER_assert` and `__CPROVER_assume` take `test` arguments.

**Dialects,** each checked against the real shells:
- `$((010))` is 10 in mksh and zsh.
- `[ x -eq 1 ]` gives status 2 in dash and bash, and a
  nondeterministic status in mksh and zsh, which evaluate the operand
  as an expression.
- Trailing blanks in integer operands are rejected in zsh.
- `==` in `test` is an extension (bash, mksh, zsh), not POSIX.
- The integer conversions were compared with dash and bash on 3000
  generated strings. There were no mismatches. Values the shells
  accept but the model flags: 45 for dash and 612 for bash. Dash's are
  44 blank strings, which dash reads as 0, and `0b0`; dash 0.5.12 reads
  binary constants, apparently through glibc's C23 `strtoimax`. Bash's
  are expressions such as `+-1`, or names.

**Supported:** simple commands; assignments, including before special
builtins; `:`, `true`, `false`, `test`/`[` with up to four arguments;
`exit`, `export`, `unset`, `break`/`continue` with counts; pipelines
and `&` of commands without side effects; `&&`, `||`, `!`; `if`,
`while`, `until`, `for … in` with words that each yield one field;
`case` with literal patterns, `*`, and `;&`; parameter expansion
`${x}`, `${#x}`, `-`, `=`, `?`, `+` with and without `:`; arithmetic
expansion.

**Rejected with an error** (never ignored):
- functions and subshells;
- unquoted expansions where field splitting matters, and unquoted
  pattern characters (pathname expansion);
- tilde expansion; pattern removal (`%`, `#`);
- `$@`, `$*`, `$!`;
- builtins that change the shell's state (`cd`, `read`, `set`, `shift`,
  `eval`, `.`, `local`, …);
- variables the shell maintains (`LINENO`, `RANDOM`, `IFS`, …);
- redirections that could fail on builtins, and redirections with side
  effects;
- `test` arguments whose values could change how `test` parses them.

**Known imprecision.** In mksh, `$((1-$x))` with `x=-1` is an error
(`--` needs a variable), but the model computes 2, as dash and bash do.

**Next.** Functions with positional parameters and `return`; subshells
by copying the state; field splitting with a modelled `IFS`; `read`
as a nondeterministic assignment; command substitution of commands
that only print (such as `echo`), so their output can be modelled.

---

## 8. Bibliographic details for key references

Checked against Crossref. All are in the local collection; the SLE 2018
Morbig paper and the IJCAR LNCS paper are represented by their
extended versions.

- Yann Régis-Gianas, Nicolas Jeannerod, Ralf Treinen. *Morbig: A Static
  Parser for POSIX Shell.* SLE 2018, pp. 29–41.
  doi:10.1145/3276604.3276615 (preprint: HAL hal-01890044). Extended
  version: *Morbig: A Static Parser for POSIX Shell*, Journal of
  Computer Languages 57 (2020), article 100944,
  doi:10.1016/j.cola.2020.100944.
- Nicolas Jeannerod, Ralf Treinen. *Deciding the First-Order Theory of
  an Algebra of Feature Trees with Updates.* IJCAR 2018, LNCS 10900,
  pp. 439–454. doi:10.1007/978-3-319-94205-6_29 (extended version:
  HAL hal-01760575).
- Fei He, Zhihang Sun, Hongyu Fan. *Satisfiability Modulo Ordering
  Consistency Theory for Multi-threaded Program Verification.*
  PLDI 2021, pp. 1264–1279. doi:10.1145/3453483.3454108 (Deagle).

Related follow-ups:

- Hongyu Fan, Zhihang Sun, Fei He. *Satisfiability Modulo Ordering
  Consistency Theory for SC, TSO, and PSO Memory Models.* TOPLAS 2023,
  doi:10.1145/3579835.
- Hongyu Fan, Weiting Liu, Fei He. *Interference Relation-Guided SMT
  Solving for Multi-threaded Program Verification.* PPoPP 2022,
  pp. 163–176, doi:10.1145/3503221.3508424.
- Nicolas Jeannerod. *Verification of Shell Scripts Performing File
  Hierarchy Transformations.* PhD thesis, Université Paris Cité, 2021
  (HAL tel-03917971). The consolidated CoLiS account.
- Paul D. Gallot. *Safety of Transformations of Data Trees: Tree Transducer Theory
  Applied to a Verification Problem on Shell Scripts.* PhD thesis
  (HAL tel-03517128). Tree-transducer view of file-hierarchy
  transformations, from the same project.
- Nicolas Jeannerod, Claude Marché, Yann Régis-Gianas, Mihaela
  Sighireanu, Ralf Treinen. *Specification of UNIX Utilities* (HAL hal-02321691).
  The utility specifications used by the CoLiS symbolic engine, and the
  most direct prior art for our external-command models (§4 item 3).
