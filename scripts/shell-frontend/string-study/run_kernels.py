#!/usr/bin/env python3
"""Run the string kernels under each string representation.

Each kernel directory holds main.sh. Kernels whose name ends in -fail must
fail; the others must succeed. All configurations use the shell front-end
(--shell-strings): refined strings for the string refinement solver
(inputs of at most 64 or 1000 bytes), and native SMT-LIB strings with z3
and cvc5. (results.txt and results.tsv also have bounded strings of 16,
64 and 256 bytes, which the front end no longer has.)

Usage: run_kernels.py CBMC [JOBS]
       run_kernels.py --table
Writes results.tsv and prints a table; --table prints the table of
results.tsv.
"""

import concurrent.futures
import os
import re
import resource
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = 300
MEMORY = 7 * 1024 ** 3


COMMON = ['--unwind', '10', '--unwinding-assertions']


def configurations(cbmc):
    configs = []
    for n in (64, 1000):
        configs.append(
            (f'refined-in{n}', 'main.sh',
             [cbmc, '--shell-strings', 'refined', '--refine-strings',
              '--shell-max-string-length', str(n)] + COMMON))
    for solver in ('z3', 'cvc5'):
        configs.append(
            (f'native-{solver}', 'main.sh',
             [cbmc, '--shell-strings', 'native', f'--{solver}'] + COMMON))
    return configs


def limit():
    resource.setrlimit(resource.RLIMIT_AS, (MEMORY, MEMORY))


def run(kernel, name, source, command):
    path = os.path.join(HERE, 'kernels', kernel)
    start = time.time()
    # Run in a new session so that a timeout kills the whole process group:
    # killing only /usr/bin/time would leave cbmc running.
    proc = subprocess.Popen(
        ['/usr/bin/time', '-f', 'MAXRSS %M'] + command + [source],
        cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, preexec_fn=limit, start_new_session=True,
        env=dict(os.environ, CBMC_SHFMT=os.environ.get('CBMC_SHFMT',
                                                       'shfmt')))
    try:
        out, _ = proc.communicate(timeout=TIMEOUT)
        elapsed = time.time() - start
        if 'VERIFICATION SUCCESSFUL' in out:
            verdict = 'SUCCESSFUL'
        elif 'VERIFICATION FAILED' in out:
            verdict = 'FAILED'
        elif 'CONVERSION ERROR' in out or 'PARSING ERROR' in out:
            verdict = 'UNSUPPORTED'
        elif 'Invariant check failed' in out or proc.returncode < 0:
            verdict = 'CRASH'
        else:
            verdict = 'ERROR'
        m = re.search(r'MAXRSS (\d+)', out)
        memory = int(m.group(1)) // 1024 if m else -1
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.communicate()
        elapsed, verdict, memory = TIMEOUT, 'TIMEOUT', -1
    expected = 'FAILED' if kernel.endswith('-fail') else 'SUCCESSFUL'
    ok = verdict == expected
    return kernel, name, verdict, ok, elapsed, memory


def print_table(results, kernels, names):
    print(f'{"kernel":20}' + ''.join(f'{n:>15}' for n in names))
    for k in kernels:
        row = f'{k:20}'
        for n in names:
            _, _, verdict, ok, sec, mib = results[(k, n)]
            cell = (f'{float(sec):.1f}s' if ok else verdict[:7]) + \
                (f'/{mib}M' if int(mib) >= 0 else '')
            row += f'{cell:>15}'
        print(row)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == '--table':
        # Print the table of an earlier run from results.tsv.
        with open(os.path.join(HERE, 'results.tsv')) as f:
            lines = [line.rstrip('\n').split('\t') for line in f][1:]
        results = {(r[0], r[1]): (r[0], r[1], r[2], r[3] == 'True', r[4],
                                  r[5]) for r in lines}
        kernels = sorted({r[0] for r in lines})
        names = list(dict.fromkeys(r[1] for r in lines))
        print_table(results, kernels, names)
        return
    jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    kernels = sorted(os.listdir(os.path.join(HERE, 'kernels')))
    configs = configurations(sys.argv[1])
    tasks = [(k, name, src, cmd) for k in kernels
             for name, src, cmd in configs]
    results = {}
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for r in pool.map(lambda t: run(*t), tasks):
            results[(r[0], r[1])] = r
    with open(os.path.join(HERE, 'results.tsv'), 'w') as f:
        f.write('kernel\tconfig\tverdict\tcorrect\tseconds\tMiB\n')
        for r in results.values():
            f.write('\t'.join(str(x) for x in r) + '\n')
    names = [c[0] for c in configs]
    print_table(results, kernels, names)

if __name__ == '__main__':
    main()
