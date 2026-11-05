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

**Supported** (updated 2026-10-30):
- simple commands and assignments, including before special builtins;
  in bash outside POSIX mode and in zsh, those assignments last only for
  the command;
- `:`, `true`, `false`, `test`/`[` with up to four arguments, `exit`,
  `export`, `unset`, `break`/`continue` with counts, `shift`, `set`
  (positional parameters and the options below), `return`, `exec`;
- `.` and `source` with literal absolute paths (§7.2);
- functions: definition, redefinition, recursion, and shadowing of
  builtins (§7.2);
- subshells, command substitution, pipelines, and `&`, as subshell
  environments (§7.2);
- `&&`, `||`, `!`, `if`, `while`, `until`, `for` (including
  `for x; do` and `"$@"`), and `case` with `;;` and `;&`;
- parameter expansion: `-`, `=`, `?`, `+` with and without `:`,
  `${#x}`, and `#`, `##`, `%`, `%%`; `$@`, `$*`, and `$#`;
- pattern matching as in XCU 2.14, in `case` and in pattern removal;
- arithmetic expansion, including `${x:-1}` with a literal word;
- the options `-e`, `-u`, `-f`, and `pipefail`, set with `set` or on the
  `#!` line. Options without an effect on the model, such as `-x` and
  `-C`, are accepted;
- redirections. Those that open files may fail: the command is then
  skipped with a non-zero status, or the shell exits for special
  builtins in dash and mksh.

**Rejected with an error** (never ignored):
- unquoted expansions where field splitting matters;
- unquoted pattern characters where pathname expansion applies. If
  `set -f` can be on, they are a `shell-unsupported` property instead;
- unquoted expansions in patterns, and patterns that have both pattern
  characters and expansions;
- tilde expansion; `$!`; command names that are expansions;
- `local`, `read`, `cd`, `eval`, `trap`, `getopts`, `command`, `umask`,
  and other builtins that change the shell's state. If a function may
  shadow such a builtin, it is a `shell-unsupported` property instead;
- variables the shell maintains (`LINENO`, `RANDOM`, `IFS`, …);
- redirections with side effects; redirections of compound commands;
- `test` arguments whose values could change how `test` parses them.

**Known imprecision.** In mksh, `$((1-$x))` with `x=-1` is an error
(`--` needs a variable), but the model computes 2, as dash and bash do.
In mksh, `unset` of a local variable reveals the outer value; this is
not modelled, as `local` is not supported yet.

### 7.2 Functions, subshells, dot scripts, options (done 2026-10-30)

**Functions.** The K-th definition of a name in the source becomes
`shell::function::NAME#K`. `shell::fn::NAME` records which definition
has run, so a call dispatches on it, and falls back to the builtin or
utility otherwise. A call's arguments become its positional parameters,
and the caller's are restored afterwards.

**Exit.** `exit`, and `-e` failures, set `shell::exited`; every caller
then returns in turn.

**Subshells.** Each subshell is a GOTO function that saves and restores
all state symbols except the status. The commands of pipelines and `&`
run sequentially at the point where they would start. They cannot see
each other's changes to the shell state, so this is exact for
everything the model observes. This is the first concurrency stage of
§5. zsh runs the last command of a pipeline in the current shell.

**Dot scripts** are parsed when the script is converted. This assumes
that the script finds the same files when it runs.

**Option `-e`.**
- Inside the code being converted, whether `-e` is ignored is known
  statically: in conditions, before `&&`/`||`, and after `!`.
- `shell::errexit_ignored` carries this into called functions.
- The exit check follows simple commands, pipelines, and subshells.
- bash outside POSIX mode turns `-e` off in command substitutions.

**Other options.** `-u`, `-f`, and pipefail are modelled only if the
script can turn them on.

**Additional assumptions**:
- the script is run as a program, so its `#!` options apply;
- the environment does not define functions (bash's `BASH_FUNC_*`), and
  `ENV`/`BASH_ENV` do not cause startup files to run;
- the script receives no signals, so that only `trap` actions on EXIT
  run (when the shell or a subshell exits).

**Differential checks.**
- `scripts/shell-frontend/pattern_differential.py` compares 49,200
  pattern/subject/operation combinations with dash, bash, and busybox.
  There were no mismatches.
- 33 `set -e`/`-u`/pipefail scripts agree with dash, bash, and mksh.

**Corpus.** Of 843 local scripts, 129 now convert completely, up from
72. The leading blockers are:
- `.` with a non-literal or relative path, mostly
  `. "$VAR/lib/..."` (181 before the dot support);
- field splitting (66);
- `exec` (59 before the exec support);
- pathname expansion (41);
- `local` (37);
- command names that are expansions (33).

### 7.3 String representation: measurements (2026-10-31)

`--shell-strings` chooses the representation (branch `shell-strings`,
which is `shell-frontend` merged into `tautschnig/py` for its string
solver work):
- `bounded` (the default): the struct `{set, length, data[N]}` and the
  C runtime unrolled over N;
- `refined`: refined strings for the string refinement solver
  (`--refine-strings`);
- `native`: the SMT-LIB String sort, for `--z3` and `--cvc5`.

**What shell does and does not have.** Shell has no structs, unions,
`sizeof`, or way to look at the bytes of a value. So an unbounded value
can be a struct of the set flag and the string with no layout to
preserve, and no byte operator ever reaches a string (the obstacle the
Python front-end met with strings inside aggregates). Values are byte
strings without NUL: native inputs are constrained to bytes 1 to 255,
as SMT-LIB strings have code points up to 0x2FFFF. Values are never
mutated in place and there are no pointers, so the functional style of
SMT-LIB strings fits. Many values are integers: counters, `$?`, `$#`,
`$((...))`. Every value therefore also carries the integer it was made
from, if any (`is_int`, `int_value`), and integer operands with a known
value are not parsed. Literal decimal strings without leading zeros
count too.

**Census** (833 scripts; `scripts/shell-frontend/string_ops_census.py`).
Of 8,654 case patterns, 4,784 are literal, 1,669 have one star at an
end (`*.txt`, `--*`), 890 more have only stars and literals, 689 are
`*`, 530 have `?` or brackets, and 92 contain expansions. Of 882
pattern removals, 456 are `L*` or `*L`, 299 are literal, 73 contain
expansions, 49 have `?` or brackets, and 5 have two stars. Decomposing
star patterns into literal segments therefore covers 93% of case
patterns and 86% of removals. Native strings match the remaining case
patterns with regular expressions; removals with `?` or brackets are
not supported by either unbounded representation yet.

**Encodings.**
- Equality, concatenation, length, prefix, suffix, substring and
  index_of map to one operation each on both back ends.
- The last occurrence (for `##*L` and `%L*`) is `last_index_of` with
  refinement. With native strings it is a fresh index and an `indexof`
  after it, or, for one byte, a suffix that does not contain the byte,
  which z3 decides much faster.
- Integer operands of `test`. Native: a regular expression for the
  syntax, then `str.to_int`, for which smt2_conv now has a lowering;
  blanks and signs are split off only if the string is not plain
  digits. Refined: `is_valid_long` and `parse_int`. Blanks around the
  number and numbers longer than 18 bytes are over-approximated there,
  because the solver's overflow axioms would otherwise constrain other
  strings.
- Native arithmetic, exit statuses, counts and indices are mathematical
  integers with range checks. With 64-bit vectors, every conversion
  between strings and integers mixed Int and bit-vectors, which neither
  solver could do (`int_encodings.py`: the mixed encoding times out in
  cvc5; a digit-by-digit bit-vector encoding times out in both; pure Int
  takes 0.02 s in both).
- Refined results need a backing array associated with their pointer
  and length. Without one, the solver gave concatenation results a
  length unrelated to the output length, and wrong verdicts.

**Results.**

Regression tests (`regression/shell`, 66 tests; per-test bounds of
120 s and 6 GB):

| Representation | Pass | Total time | Slowest |
|---|---|---|---|
| bounded | 66 | 66 s | 10 s |
| native, cvc5 | 65 | 6 s | 1 s |
| native, z3 | 65 | 18 s | 13 s |
| refined | 62 | 133 s | 43 s |

The test that native strings fail checks the bounded-only
`shell-string-length` property. Refined strings fail it too, as well as
two tests with bracket expressions (not supported) and one with
`" 12 " -eq 12`, which blank over-approximation makes fail.

Kernels (`string-study/run_kernels.py`): 12 scripts from common idioms
and a failing variant of each, with `--unwind 10 --unwinding-assertions`,
300 s and 7 GiB per run. "Correct" means the expected verdict; time is
the sum over correct runs.

| Representation | Correct | Time | Wrong | Timeout or out of memory | Max memory |
|---|---|---|---|---|---|
| bounded, N=16 | 22/24 | 514 s | 2 | 0 | 3.4 GB |
| bounded, N=64 | 22/24 | 514 s | 2 | 0 | 3.4 GB |
| bounded, N=256 | 15/24 | 643 s | 2 | 7 | 6.6 GB |
| refined, inputs ≤ 64 | 19/24 | 70 s | 0 | 5 | 6.5 GB |
| refined, inputs ≤ 1000 | 19/24 | 71 s | 0 | 5 | 6.5 GB |
| native, z3 | 18/24 | 42 s | 0 | 6 | 103 MB |
| native, cvc5 | 22/24 | 47 s | 0 | 2 | 58 MB |

- Bounded strings fail `concat-eq` and `loop-concat` spuriously: a
  concatenation with an input can exceed N. They are 10 to 100 times
  slower than the unbounded representations and use 5 to 60 times the
  memory; with N=256, 7 of 24 runs exceed the time or memory bound.
- Refined strings run out of memory on the kernels that remove patterns
  in a loop or split paths (`csv-loop`, `option-split`, `path-split`).
- z3 times out on the same kernels and on `int-octal-fail`; cvc5 solves
  them in 0.3 to 2.4 s.
- Neither native solver finishes `int-octal-fail` (" 08" is valid for
  `test` but an invalid octal constant in `$((...))`) or its companion
  `int-roundtrip-fail`. Both combine blank decomposition, the octal
  check and arithmetic on unknown strings.
- `prefix-remove` uses `${#x}` unquoted in `$((...))`, which no
  representation supports yet.

Encodings against dash (`encoding_differential.py`, 400 cases with
constant inputs: pattern removal, case, `${#x}`, `test -eq`, `$((x+1))`):
- no mismatches in any representation;
- refined strings were imprecise on 16 integers with blanks or more than
  18 digits, as designed;
- unsupported values (octal and hexadecimal constants, reported as
  properties) were 16 for refined, 4 to 5 for native, and 0 for bounded,
  whose runtime handles them;
- with z3, two of the ten batches (70 cases) timed out; earlier runs
  traced such timeouts to integers with blanks.

The Python front-end's earlier numbers (`results-python.tsv`) do not
measure the solvers: its encoding gave spurious results.

**Conclusions.**
- Native strings with cvc5 are the fastest and most precise. They
  support every pattern, need no length bound, and use the least
  memory. With z3 they are correct but time out on loops over pattern
  removal and on integers with blanks, which are z3 string-solver
  weaknesses that the encodings above only partly work around.
- Refined strings are correct where they answer, but are slower than
  native strings and run out of memory on pattern removal in loops.
  They need an input length bound for the solver's model check (2^26
  bytes), over-approximate some integers, and have no regular
  expressions for `?` and brackets. Their solver needed fixes on the
  shell's side (explicit backing arrays) to give correct results.
- Bounded strings work with every back end and their integer parsing is
  exact. They are much slower, need a length bound that makes
  concatenations with inputs fail, and N=256 is already too large.

**Proposal, to decide.**
- Make `native` the representation whenever `--z3` or `--cvc5` is
  given, and recommend cvc5.
- For SAT runs, keep `bounded` as the default for now. Offer `refined`
  as an option until it handles pattern removal in loops; those
  out-of-memory runs are the next thing to investigate in the solver.
- Remove `--shell-max-string-length` from unbounded runs, except as the
  input bound for refined strings.
- Open work:
  - z3: the blank decomposition and loops over `index_of`;
  - native octal and hexadecimal constants in `$((...))`;
  - refined: `?` and brackets between two stars (those before the first
    or after the last star are matched byte by byte), and more than two
    blanks around an integer (fewer are skipped; stripping any number with
    the strip builtin made integer kernels up to 70 times slower).
  - refined: string axioms on paths that do not run, and lengths of arrays
    made for pointers; see the design in
    `string-refinement-guards-and-lengths.md`.

Decided (2026-10-31): native strings are the default with `--cvc5` or
`--z3`; bounded strings stay the SAT default; `--shell-strings refined`
turns on the refinement solver.

**The refinement solver's memory blow-up** (profiled with
`scripts/profile_cbmc.py --no-skip-solver` on `option-split` and
`csv-loop`, at a 6 GB limit):
- Symbolic execution is negligible: `option-split` has 645 steps and one
  VCC. All memory goes to the string refinement loop.
- The loop does not converge. Each round finds the model violates a
  universal axiom, adds index terms, and instantiates the axioms at them.
  The index set grows by about 1.6× per round (149, 292, 440, 697, 1117,
  1789 terms on `option-split`). The new terms are nested
  `if`-expressions over lengths of strings merged at join points (after
  `case` and in loops), so each instantiation creates more.
- The SAT formula grows with them. Array reads at symbolic indices get
  Ackermann constraints (`i = j ⇒ a[i] = a[j]`), quadratic in the index
  terms of each array class. `arrayst::add_array_Ackermann_constraints`
  takes 49% of the time on `option-split`, and the sixth round had 11
  million variables and 53 million clauses. On `csv-loop`, 59% of the time
  is in MiniSat and 23% in `add_lemma`.
- Experiments that did not help:
  - Refining only from concrete counterexamples, not from index-set
    growth. Memory stays under 1 GB on `option-split` and `path-split`,
    but neither finishes in 300 s.
  - Initialising positional parameters without branches, so that inputs
    are not `if`-expressions over strings. This is kept, because it also
    simplifies the formula.
  - Copying a string after a join.
- A fix belongs in the solver's instantiation strategy: stop index terms
  from nesting merged lengths, or concretize them from the model before
  instantiating, and bound the Ackermann constraints. That is a project
  of its own, so refined strings stay an option.

**The growth within the refinement loop: causes and candidate fixes**
(2026-11-01). The quadratic array constraints are being handled
elsewhere; this concerns only the string solver's own growth. The
experiments are on branch `string-refinement-experiments`, behind
environment variables. The kernels ran with 200 s and 6 GB each.

Causes, each measured:
1. **Not-contains axioms.** `index_of` with a string needle generates
   them, and they are instantiated over pairs of index terms (new ×
   all, both ways). On `option-split`, from round 6 on the only
   violated axioms were not-contains axioms, while the index set kept
   growing.
2. **Index terms grow with merged strings.** A string assigned in a loop
   or in one branch of a `case` is an if-expression over the strings of
   each path. Its length, and every index term derived from it, nests
   those choices. On `csv-loop` the median term had 85 characters and the
   largest 7,693. Substituting the terms into 110 axiom bodies produced
   19,622 lemmas of 1.5 GB in the first round alone: 77 KB on average,
   1.5 MB at most.
3. **Many terms denote the same position.** Evaluated in the current
   model, 33 to 956 of the new terms per round repeat the value of a term
   already instantiated, against 30 to 68 with a new value.

Fixes tried:

| Fix | Where | Effect |
|---|---|---|
| Search for one-byte literal needles as characters | front end | `option-split` and `path-split`: out of memory → 2 to 3 s, 60 MB |
| Skip the integer parser for known integers (branch) | front end | `int-format` 13 s → 0.3 s, `count-args-fail` 18.6 s → 3.4 s |
| End temporaries per command (DEAD) | front end | fewer merged strings; with the filter, `csv-loop-fail` 5.3 → 3.0 GB |
| Model-value filter: instantiate only index terms whose model value is new for their array; hold the others back and retry them later | solver | `option-split` 18.9 → 2.2 s, `path-split` 15.7 → 3.2 s |
| Counterexample-only refinement (violated axiom at its witness) | solver | memory bounded, but one point per round: `option-split` needed 221 rounds and 107 s |
| Ground range instances: each violated axiom at every position within its bounds in the model (at most 64), with arrays concretized | solver | converges in a few rounds; solves `csv-loop` (63 s) and `arg-loop-fail` (8.8 s) |
| Filter, then ground instances once the index set exceeds 100 terms | solver | 23/24 kernels correct (default 21/24) |
| Naming large index terms with fresh symbols | solver | worse: out of memory sooner |
| Propagating witness positions to all axioms on the same array | solver | worse |

Combined result (front-end changes plus filter and switch at 100;
`CBMC_STRING_IS_FILTER=1 CBMC_STRING_SWITCH=100`):
- Kernels: 23 of 24 correct, against 21 of 24 by default, with no wrong
  verdicts. `csv-loop-fail` takes 8.8 s and `arg-loop-fail` 87 s, where
  the default runs out of memory or time. `csv-loop` alone still exceeds
  200 s; it finished in 191 s and 2.5 GB in a single run.
- Python regression suite (1,278 tests): every test passes, against one
  failure for the default solver; total time 346 s against 352 s.
  `regex-findall-split-sound` needed a bound on the ground rounds: when
  ground instances add nothing new, or after 64 rounds, the model is
  taken as a counterexample. That is the same sound fallback the solver
  already uses for an empty index set.
- Shell regression tests in refined mode and the C string tests: no
  change.

**Soundness.** Every instance is implied by its axiom, so the filter, the
ground instances and the switch only affect progress. A satisfying model
is still checked against all axioms. The only other way to SAT is the
existing fallback (model treated as a counterexample), which can cause a
false alarm but cannot hide a bug.

**Implemented and enabled by default** (2026-11-02):
- The solver searches for constant one-character needles of `index_of`,
  `last_index_of` and `contains` as characters, for every front end. The
  shell's own workaround is gone.
- The model-value filter on index sets, with one rule added: if a round
  would defer every new term, all deferred terms are released, so the
  refinement never instantiates less than before, only later.

Results with both, on the default settings:
- Kernels: 21/24 correct, 22 s in total (45 s before), with no wrong
  verdicts.
- Python regression suite (1,278 tests): all pass.
- C string tests: all pass.
- Shell tests: unchanged.
- `csv-loop`, `csv-loop-fail` and `arg-loop-fail` still exceed 200 s,
  now by time rather than memory.

**Second round: profiling with DWARF call graphs** (2026-11-03;
`perf record --call-graph dwarf`, as the Release build has no frame
pointers). Once the index sets were bounded, the remaining time went into
handling copies of the same large terms, not into solving. Each fix
targets one hotspot, none changes the constraints' meaning, and all are
in `2c311bf17f`:

| Hotspot | Share of time (`arg-loop-fail`) | Fix |
|---|---|---|
| Simplifying index terms again for every array and axiom | 86% | simplify each term once (cache) |
| `substitute_array_access` walking shared terms as trees | 90% | memoise on the term DAG, shared by all lemmas; equal out-of-bounds accesses now share one value symbol |
| Lengths of if-expressions over strings copied into every use | (term size) | one symbol per such length, defined once |
| The conversion cache comparing unshared equal terms deeply | 52 to 68% | hash-cons lemmas bottom up before conversion |

`arg-loop-fail` now finishes in 61 s and 1.7 GB; it timed out at
200 s before. All other kernels keep their verdicts, and the Python suite,
the C string tests and the shell tests pass.

Also tried: naming every index term with a fresh symbol, so that each
instance would convert it only once. It solved `arg-loop-fail` in 19 s,
but `string-of-float-twice-called-function` ran out of memory, because
derived terms were no longer normalised as linear functions. Naming only
the non-linear atoms avoided that but helped less. Both were dropped.

**Where the time goes now.** On `csv-loop` and `csv-loop-fail`, 85 to 90%
of the time is in MiniSat and 6 to 10% in the array Ackermann
constraints; the string solver's own work is under 5%. The remaining
limit is the propositional problem, that is the array encoding being
worked on elsewhere, and the number of instances, which grows with the
number of strings merged at join points (110 axioms; 1,352 new index
terms in round 2 of `csv-loop`).

**Held back as a heuristic:** ground range instances once the index set
passes a budget (branch `string-refinement-experiments`). It solves the
remaining kernels, but its three thresholds would need tuning. Other open
candidates:
- encode `index_of`/`contains` with a longer constant needle as a
  universal axiom over fixed offsets instead of a not-contains axiom;
- in front ends, avoid strings merged at join points where possible.

The remaining cost is the SAT problem itself: about 1.7 million
variables on `arg-loop-fail` after one round. That is where the
array-constraint work being done elsewhere comes in.

**Next.**
- field splitting with the default IFS, for `for x in $list`,
  `set -- $x`, and function arguments;
- `local`, with dash keeping the outer value and bash leaving the
  variable unset;
- `read` as a nondeterministic assignment;
- command names that are expansions, dispatched on their value;
- `. "$dir/file"` with an option that maps such paths to files;
- modelling the output of command substitutions of `echo` and `printf`.

### 7.4 Corpus census (2026-11-04)

To rank this backlog, `scripts/shell-frontend/corpus-census.sh` converts
the sh and bash scripts installed on the development machine (Ubuntu
24.04): 1,787 files, 1,652 after removing duplicates by content (1,518 sh,
134 bash). Most come from `/var/lib/dpkg/info` (944 before removing
duplicates), `/usr/bin` and `/usr/sbin`. Each conversion is bounded to
4 GB and 60 s.

**How many convert.** 597 of 1,652 (36%) with bounded strings, 606 (37%)
with `--shell-strings refined`. The difference is 69 scripts whose first
blocker with bounded strings is a string literal longer than
`--shell-max-string-length` (64 by default).

**Every unsupported construct, not only the first.** A census build that
reports each unsupported construct and carries on with the next command
(not committed: it crashed on 5 scripts, whose state after a failed
command was inconsistent) gives, with refined strings, 1,047 scripts with
at least one construct. Per construct: the scripts that have it, its
occurrences, and the scripts for which it is the only one ("sole").

| Construct | Scripts | Occurrences | Sole |
|---|---|---|---|
| unquoted expansion where field splitting matters | 540 | 3,375 | 84 |
| command name that is an expansion | 302 | 879 | 40 |
| builtin `command` | 298 | 463 | 208 |
| builtin `read` | 279 | 615 | 11 |
| builtin `local` | 211 | 2,525 | 16 |
| variable `IFS` | 149 | 547 | 0 |
| builtin `eval` | 148 | 613 | 6 |
| redirection of a compound command | 144 | 240 | 7 |
| `$@` other than as the word `"$@"` | 130 | 2,342 | 2 |
| pattern removal other than `L*`, `*L`, `L1*L2` (refined) | 114 | 221 | 0 |
| unquoted `*` (pathname expansion) | 104 | 282 | 8 |
| `.` with a file name that is an expansion | 102 | 191 | 10 |
| `test` with more than four arguments | 89 | 205 | 19 |
| builtin `trap` | 84 | 125 | 5 |
| redirection without a command name | 58 | 60 | 2 |
| builtin `cd` | 57 | 80 | 4 |
| redirection with an expansion with side effects | 56 | 379 | 2 |
| `shift` operand other than a decimal number | 49 | 142 | 0 |
| builtin `getopts` (with `OPTARG`, `OPTIND`) | 47 | 141 | 0 |

`command` is nearly always `command -v NAME` (446 of about 450 uses), the
idiom for testing whether a utility exists. `read` is `read` or `read -r`
(446 of 451 uses).

**Order of the fixes that unlock most scripts** (greedy: at each step the
construct that is the last one left in most scripts; cumulative number of
scripts that convert, from 606):

| Fix | Scripts converting |
|---|---|
| builtin `command` | 813 |
| field splitting | 903 |
| command names that are expansions | 957 |
| `.` with a file name that is an expansion | 986 |
| `local` | 1,017 |
| `eval` | 1,051 |
| `test` with more than four arguments | 1,081 |
| `read` | 1,113 |
| unquoted `*` | 1,137 |
| redirection of a compound command | 1,161 |
| `cd` | 1,184 |
| `return` outside a function | 1,205 |

The counts after the first few steps are rough: a script's other blockers
may only show up once its first is fixed.

### 7.5 The SAT default: bounded or refined strings (2026-11-04)

Measured after the solver fixes of 7.3 and the commits that followed
(`c0d27c1efe` to `eba42815b6`): the 26 string-study kernels
(`--unwind 10 --unwinding-assertions`, 200 s and 4 GB per run), and the
597 installed scripts that convert in both modes (`--unwind 3`, 120 s and
4 GB).

| | Bounded | Refined |
|---|---|---|
| Kernels correct | 21 of 26 | 22 of 26 |
| Kernel time (correct ones) | 464 s | 23 s |
| Kernel peak memory (correct ones) | 2,968 MB | 121 MB |
| Scripts that convert (of 1,652) | 597 | 606 (a superset) |
| Scripts decided (of 597) | 597 | 597 |
| Time on those | 763 s (median 0.91 s, max 22 s) | 369 s (median 0.04 s, max 117 s) |
| Peak memory | median 47 MB, max 931 MB | median 14 MB, max 1,846 MB |

- **Verdicts.** Apart from bounded strings' `shell-string-length`
  properties (strings that would exceed `--shell-max-string-length`), the
  two modes report the same failed properties on every script. Bounded
  strings fail on 33 scripts, and on the kernels `concat-eq` and
  `loop-concat`, only through those properties.
- **Kernels neither mode solves.** `csv-loop` and `csv-loop-fail` (bounded
  solves `csv-loop` in 118 s), and `prefix-remove` and its `-fail` variant,
  which use `${#x}` in `$((...))`, which is unsupported.
- **Where refined is slower.** Three scripts are 4 to 6 times slower than
  with bounded strings (60 to 115 s against about 19 s): `tarcat`,
  `c99-gcc` and `src-hilite-lesspipe.sh`, loops over the arguments with
  `case` patterns. The time goes into `dec_solve`'s setup (symbol
  resolution that walks expressions as trees), not into refinement.
- **What refined does not support that bounded does.** Pattern removal
  with patterns other than `L*`, `*L` and `L1*L2` (found in 114 scripts),
  and `?` or brackets between two stars (21).

**Proposal:** make `--shell-strings refined` the SAT default, and keep
`bounded` as an option for the patterns above.

### 7.6 Removing bounded strings (2026-11-05)

Rather than keep bounded strings for the patterns of 7.5, the unbounded
representations now support all patterns, and bounded strings, with their
C runtime (`shell_runtime.cpp`) and generated pattern matchers, are gone.
`--shell-strings` is `refined` (the default, with the SAT back end) or
`native` (the default with `--cvc5` or `--z3`); `--shell-max-string-length`
bounds the inputs of refined strings only.

- **Patterns.** A pattern `S0*S1*...*Sn` matches with `S0` and `Sn` at
  known positions, checked element by element, and the middle segments at
  their first occurrences; the removals then take the first or last
  occurrence of the segment at the end that they search for. A segment is
  searched for by its core, without the `?`s around it: a literal core,
  which may include quoted expansions, with `index_of`; one bracket
  expression with the solver's new `index_of_set` (refined) or regular
  expressions (native); and up to 16 literal alternatives of the
  characters of the bracket expressions. Native strings search for other
  cores without expansions with regular expressions. Refined strings do
  not support a `?` inside a searched core, such as `*-?-*`.
- **Jumps.** The items of `case` and the bodies of loops are blocks, so
  that the jumps to them, and `break` and `continue`, skip no declarations:
  goto_convert implemented those with `__CPROVER_going_to` flags, whose
  merges made string pointers exponentially large trees for the string
  solver. With this, `csv-loop` and `csv-loop-fail` verify (8 and 26 s),
  so the 24 kernels without `${#x}` in `$((...))` are all correct with
  refined strings, and of the
  three slow scripts of 7.5, one takes 2 s rather than 90 s, and two 26 and
  55 s rather than 117 and 109 s.
- **Differential tests.** `pattern_differential.py` and
  `runtime_differential.py` assert what dash and bash compute, one input
  per arm of a `case`, and check the assertions in both modes, telling
  over-approximations from wrong results. They found that bash accepts
  only space and tab after a `test` integer (now modelled), and a crash of
  the string solver on not-contains axioms of dead paths (fixed). Of 239
  patterns with 14 values in 5 operations, 16,275 checks agree with refined
  strings and 16,450 with native strings, none mismatch, and 6 (refined)
  and 3 (native) patterns are unsupported. Of 3,000 integer strings, none
  mismatch.
- **Census.** No installed script converts with bounded strings only: 597
  convert with bounded strings, 606 (a superset) with refined strings.
- **Still slow with refined strings.** A script's solving time grows about
  quadratically with the number of `case` arms that have failed
  properties, as CBMC solves once per newly failed property; the
  differential tests use small scripts.

### 7.7 Field splitting, dynamic command names, eval (2026-11-05)

- **IFS.** Every shell sets IFS to space, tab and newline at startup,
  whatever the environment says (checked for dash, bash, busybox, mksh
  and zsh). Every access to a variable now goes through one accessor that
  rejects IFS and the other variables the shell maintains, and bash's
  `printf -v` is unsupported (a first operand that is an expansion is
  checked to have no `-v`). Field splitting then only needs the default
  IFS.
- **Single fields.** An unquoted expansion where one field is needed (an
  operand of `test`, say) is supported, with properties that report the
  values that field splitting, pathname expansion, or the removal of an
  empty field would change.
- **Lists of fields.** A single unquoted expansion in `for` items,
  function arguments and `set --` is split: refined strings search with
  `index_of_set`, each field only after the previous one was found;
  native strings decompose the value, which is much harder for the
  solvers.
- **Command names that are expansions** run what the value names: a branch
  per function and modelled builtin, a property for unsupported builtins,
  and a utility otherwise. Refined strings compare the name byte by byte:
  string equalities of one unknown string with 60 constants took over
  100 seconds for `"$1" x`, and now take 5.
- **Literal values.** The converter tracks which variables hold a known
  literal (forgetting, at compound commands, what they may assign, and
  everything at calls), so that their expansions, splitting, and use as
  command names need no solver: `cmd="greet you"; $cmd` calls `greet`.
- **eval** of text known during conversion is parsed and converted in
  place; of other text (mostly `eval "$(cmd)"`), it is reported as a
  property, and the path ends.
- **Census.** 746 of 1,652 installed scripts convert, against 606. Of the
  140 new ones, most fail properties that field splitting, pathname
  expansion or `eval` report; 24 are not decided with `--unwind 3` in 120
  seconds, as their field splitting is costly for the solvers.

### 7.8 The remaining census blockers (2026-11-06, updated 2026-11-07)

Each construct below was the first unsupported one of at least 20
scripts of the census.
- **IFS** (supersedes 7.7's restriction). Its value is tracked during
  conversion. Where it is not known, as in a function or after
  `IFS="$saved"`, the ghosts `shell::ifs_default` and `shell::ifs_unset`
  record whether it splits as by default, which splitting and `$IFS`
  check. Values not known during conversion are split at an IFS of white
  space, or of bytes that are not white space; mixed IFS values are
  checked to be absent. `local IFS` saves and restores the ghosts.
- **`read [-r] NAME...`** assigns values without newlines, and with -r
  the variables but the last get no byte of IFS. `IFS= read` changes the
  splitting for that read only.
- **`$@`.** `x"$@"y` follows dash, bash, busybox, mksh and zsh, which
  agree. Unquoted `$@` splits each parameter.
- **Redirections** of compound commands, and of commands without a
  name, may fail with a non-zero status. No shell exits, although XCU
  2.8.1 says it shall.
- **Pathname expansion** in lists of fields yields the word itself, or
  1 to N+1 unknown pathnames that match it as a case pattern. Unquoted
  expansions in patterns are checked to have no pattern characters or
  backslashes, which shells treat differently.
- **Builtins:** `cd`, `umask`, `type`, `kill`, `getopts` (with OPTIND,
  OPTARG and its own position in grouped options), `return` at the top
  level, `trap` (only EXIT traps run: the script is assumed to receive
  no signals, see the assumptions in 7.1).
- **Dialects.** The POSIX dialect stands for dash, busybox, and bash run
  as sh. Several models had followed dash alone: `command -v` and `type`
  for missing names (bash: 1), `local x` (busybox and bash unset x),
  `shift` too far (busybox and bash continue). Each now allows every
  behaviour.
- **Refined strings** do not support strings read at symbolic array
  indices, which `getopts` and `shift $n` first did. Those reads are now
  at constant indices. The refined solver is also sensitive to the order
  of identifiers: interning three unused names made the csv-loop kernel
  take 40 s rather than 3 s.
- **Smaller blockers** (each fewer than 20 scripts):
  - redirections whose expansions have side effects;
  - `"$@"` as a command;
  - `${#*}` and `${*:-w}`;
  - `x="$@"`;
  - assignments to `_`;
  - `export $names` and `export -n`;
  - `set $x`;
  - `REPLY`;
  - `$!`;
  - `~` and `~/path`;
  - `RANDOM`;
  - `eval` with a redirection that can fail;
  - `break` outside a loop;
  - `print` (mksh, zsh);
  - `&>`;
  - assignments before a function call, which all shells restore after
    it;
  - `(( ))` and `let`.
- **Bash extensions** (bash, and mksh or zsh where they agree):
  - `[[ ]]` without `=~`;
  - `${x/p/s}` with a literal pattern (also `/#` and `/%`);
  - `${x:o:l}` with non-negative operands;
  - dense, global arrays of at most N elements, including `"${a[@]}"`
    as a command;
  - process substitution, `$[...]` and `time`.
- **Arithmetic.**
  - Each `$(...)` runs first, and its output must be an integer
    constant.
  - `${#name}` is the length of the value.
  - In sh, `++` and `--` follow dash (where `++x` is `+(+x)` and `x++`
    a syntax error) or bash and busybox, which increment. The choice is
    made once, at startup (`shell::posix_increments`).
  - With -u, bash and zsh report unset names, also in `x += 1` and
    `x++`.
- **-u in redirections** depends on the shell and the command:
  - a redirection target of a utility makes dash, busybox and mksh
    exit, while bash and zsh go on;
  - a here-document of a utility lets every shell go on;
  - a here-document of a regular builtin or compound command lets
    dash, busybox and mksh go on, while bash and zsh exit;
  - otherwise every shell exits.
  Where a shell goes on, the word is expanded in a subshell.
- **UID, EUID and PPID** are decimal numbers in bash. In sh,
  assignments either succeed (dash, busybox) or make the shell exit
  (bash run as sh).
- **Others:**
  - pathname expansion in an operand of `test`;
  - `"$@"` and `$@` as a command name with further words;
  - assignments before `command -v`;
  - `type "$x"`.
- **Shell programs use 16 object bits** (`src/util/config.cpp`): scripts
  with many strings had too many addressed objects for the default.
- **Linking.** `link_to_library` links each library function in its
  own round. Every round walked all existing functions with empty
  rename maps, un-sharing every instruction. Skipping that walk cut the
  conversion of a 197-line script from 70 s to 16 s, which removed all
  census timeouts.
- **Census.** 1,584 of 1,652 scripts convert, against 746. Each
  remaining blocker affects at most 4 scripts:
  - refined-string patterns with `?`;
  - `shopt`, `alias`, `declare`, `typeset`, `setopt` and `emulate`;
  - `$"..."`, as translations could introduce expansions;
  - PIPESTATUS;
  - `read` options;
  - 4 scripts that shfmt rejects (bash syntax under `#!/bin/sh`).
- **Platforms.** `configt::shellt::platform` is Unix (Linux, macOS, the
  BSDs) or Cygwin, which `--os windows` selects. bash has `igncr` on
  Cygwin only: on Unix `set -o igncr` fails with status 2; on Cygwin,
  enabling it is unsupported.
- **Locale.** Matching and `${#x}` are by bytes, as in the C locale. In
  UTF-8 locales, bash, busybox, mksh and zsh count characters instead;
  this is not modelled yet. Allowing both lengths for `${#x}` (bytes for
  ASCII, otherwise from a quarter of the bytes to all of them) is sound
  but loses too much: `${#t}` of `t="$s$x"` with an ASCII `s` of 10
  bytes may then be below 10. Counting the ASCII bytes would keep that
  bound. `?` and bracket expressions would need the same.

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
