"""Helpers for the differential tests of the shell front end.

The tests generate shell scripts whose assertions state what real shells
compute, and check them with CBMC in the unbounded string modes. Each
input is an arm of a case on $1, so that a property that the front end
reports for one input, and follows by an assumption, does not hide the
checks of the other inputs. Each check is one line, so that the line
numbers of the properties identify the checks.

A check asserts that a value is the shell's, and is followed by an
assertion that fails if it is reachable: a check that holds is then not
vacuous. For each check that fails, a second phase asserts that the value
is not the shell's: if that fails, the model admits the shell's value and
others, an over-approximation, which is sound.
"""

import concurrent.futures
import os
import re
import resource
import subprocess

PROPERTY = re.compile(
    r'^\[([^\]]+)\] file \S+ line (\d+) .*: (SUCCESS|FAILURE)$')

MODES = {
    'refined': ['--shell-strings', 'refined'],
    'native': ['--shell-strings', 'native', '--cvc5'],
}


def quote(s):
    """s as a single-quoted shell word."""
    return "'" + s.replace("'", "'\\''") + "'"


def run_shell(shell, script):
    """The standard output of the command list shell for script."""
    return subprocess.run(shell, input=script, capture_output=True,
                          text=True).stdout


class Script:
    """A generated script: arms of a case on $1 of items:
      - ('setup', text): commands, which may span lines;
      - ('value', command, value, description): command sets r, whose
        value must be value, a shell word;
      - ('rejected', command, description): the shell rejects the input of
        command, which the front end must report as a failed property.
    """

    def __init__(self, shebang='#!/bin/sh', negate=False):
        self.text_lines = [shebang, 'case "$1" in']
        self.negate = negate
        self.arms = 0
        # The checks by line: (item, line of their reachability assertion).
        self.checks = {}

    def _add(self, text):
        self.text_lines.append(text)

    def _next_line(self):
        return sum(t.count('\n') + 1 for t in self.text_lines) + 1

    def arm(self, items):
        """Add an arm of items. A value check is followed by an assertion
        that fails if the check is reachable, so that a check that holds
        is not vacuous."""
        self.arms += 1
        self._add(f'  {self.arms})')
        for item in items:
            if item[0] == 'setup':
                self._add('    ' + item[1])
                continue
            line = self._next_line()
            if item[0] == 'value':
                _, command, value, _ = item
                assert '\n' not in command + value
                operator = '!=' if self.negate else '='
                self._add(
                    f'    {command}; __CPROVER_assert "$r" {operator} {value}')
                self._add('    __CPROVER_assert 0 = 1')
                self.checks[line] = (item, line + 1)
            else:
                _, command, _ = item
                assert '\n' not in command
                self._add(f'    {command}; __CPROVER_assert 0 = 1')
                self.checks[line] = (item, None)
        self._add('    ;;')

    def text(self):
        return '\n'.join(self.text_lines + ['esac']) + '\n'


def run_cbmc(cbmc, path, options, timeout, memory):
    """Check the script at path with CBMC, within timeout seconds and memory
    bytes. The result is a status (ok, unsupported, timeout, or error) and
    the outcomes of the properties by line."""
    def limit():
        resource.setrlimit(resource.RLIMIT_AS, (memory, memory))
    try:
        p = subprocess.run([cbmc, path] + options, capture_output=True,
                           text=True, timeout=timeout, preexec_fn=limit)
    except subprocess.TimeoutExpired:
        return 'timeout', {}
    out = p.stdout + p.stderr
    if 'unsupported shell construct' in out:
        return 'unsupported', {}
    if p.returncode not in (0, 10):
        return f'error {p.returncode}', {}
    properties = {}
    for line in out.splitlines():
        m = PROPERTY.match(line)
        if m:
            properties.setdefault(int(m.group(2)), []).append(
                (m.group(1), m.group(3)))
    return 'ok', properties


def failed(results, assertion):
    """Whether an assertion (or, if not assertion, a property of the front
    end) of results failed."""
    return any(name.startswith('assertion.') == assertion and s == 'FAILURE'
               for name, s in results)


def run_scripts(cbmc, mode, scripts, work, prefix, jobs, timeout, memory):
    def check(item):
        index, script = item
        path = os.path.join(work, f'{prefix}{mode}-{index}.sh')
        with open(path, 'w') as f:
            f.write(script.text())
        return path, script, run_cbmc(cbmc, path, MODES[mode], timeout,
                                      memory)
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        return list(pool.map(check, enumerate(scripts)))


def check_scripts(cbmc, mode, arms, make_script, work, jobs, timeout,
                  memory):
    """Check the arms, lists of items, in mode, in scripts that
    make_script(arms, negate) builds. The result counts the scripts by
    status and the checks by outcome:
      - agree: the front end computes what the shell does, or, for an
        input that the shell rejects, reports a failed property;
      - flagged: the front end reports a failed property for an input that
        the shell accepts: sound, but imprecise;
      - imprecise: the model admits the shell's value and others;
      - mismatch: the model excludes the shell's value, or accepts an input
        that the shell rejects;
      - vacuous: no path reaches the end of the arm, although the front end
        reports no property.
    It also lists the mismatches and the scripts whose status is not ok."""
    statuses, outcomes, problems, not_ok = {}, {}, [], []

    def count(outcome, path, item):
        outcomes[outcome] = outcomes.get(outcome, 0) + 1
        if outcome in ('mismatch', 'vacuous'):
            problems.append((outcome, path, item[-1]))

    candidates = []
    for path, script, (status, properties) in run_scripts(
            cbmc, mode, make_script(arms, False), work, '', jobs, timeout,
            memory):
        statuses[status] = statuses.get(status, 0) + 1
        if status != 'ok':
            not_ok.append((status, path))
            continue
        for line, (item, reach_line) in script.checks.items():
            results = properties.get(line, [])
            if failed(results, False):
                count('agree' if item[0] == 'rejected' else 'flagged', path,
                      item)
            elif item[0] == 'rejected':
                count('mismatch', path, item)
            elif failed(results, True):
                candidates.append(item)
            elif not failed(properties.get(reach_line, []), True):
                count('vacuous', path, item)
            else:
                count('agree', path, item)

    # Phase 2: does the model admit the shell's values of the candidates?
    if candidates:
        setups = {}
        for arm in arms:
            for item in arm:
                if item[0] == 'value':
                    setups[id(item)] = [i for i in arm if i[0] == 'setup']
        negated = [setups[id(item)] + [item] for item in candidates]
        for path, script, (status, properties) in run_scripts(
                cbmc, mode, make_script(negated, True), work, 'negated-',
                jobs, timeout, memory):
            if status != 'ok':
                not_ok.append((status, path))
                for item, _ in script.checks.values():
                    count('unknown', path, item)
                continue
            for line, (item, reach_line) in script.checks.items():
                if failed(properties.get(line, []), True):
                    count('imprecise', path, item)
                elif not failed(properties.get(reach_line, []), True):
                    count('vacuous', path, item)
                else:
                    count('mismatch', path, item)
    return statuses, outcomes, problems, not_ok


def report(mode, statuses, outcomes, problems, not_ok, show=20):
    print(f'{mode}: scripts {statuses}, checks {outcomes}')
    for p in problems[:show]:
        print('  ', *p)
    for s in not_ok[:show]:
        print('  ', *s)
    return bool(problems)


def chunked(per_script, shebang='#!/bin/sh'):
    """A make_script function for check_scripts: scripts of per_script
    arms each."""
    def make_script(arms, negate):
        scripts = []
        for start in range(0, len(arms), per_script):
            script = Script(shebang, negate)
            for arm in arms[start:start + per_script]:
                script.arm(arm)
            scripts.append(script)
        return scripts
    return make_script


def preflight(cbmc, modes, work):
    """Exit with CBMC's output if it cannot check a trivial script in one of
    modes, as when it cannot find shfmt (see CBMC_SHFMT)."""
    path = os.path.join(work, 'preflight.sh')
    with open(path, 'w') as f:
        f.write('#!/bin/sh\n:\n')
    for mode in modes:
        p = subprocess.run([cbmc, path] + MODES[mode], capture_output=True,
                           text=True)
        if p.returncode != 0:
            raise SystemExit(f'{cbmc} fails in mode {mode}:\n' + p.stdout +
                             p.stderr)
