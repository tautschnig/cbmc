#!/usr/bin/env python3
"""Compare the shell front-end's pattern matchers with real shells.

Compiles random patterns with src/shell/shell_pattern.cpp, builds the
generated C matchers, and compares, for random subjects, the results of
case and of ${x#p}, ${x##p}, ${x%p}, ${x%%p} with dash, bash, and busybox.

Usage: pattern_differential.py BUILD_DIR
BUILD_DIR is a CMake build directory with the shell front-end built.
"""

import os
import random
import subprocess
import sys
import tempfile

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                   'src')

GENERATOR = r'''
#include <shell/shell_pattern.h>
#include <shell/shell_runtime.h>
#include <iostream>
#include <string>
int main()
{
  std::string line;
  std::cout << shell_runtime_source(8);
  std::size_t n = 0;
  std::string table = "typedef struct __CPROVER_shell_string S;\n"
    "struct entry { _Bool (*match)(S); S (*r[4])(S); int ok; };\n"
    "struct entry entries[] = {\n";
  while(std::getline(std::cin, line))
  {
    shell_word_partt part;
    part.kind = shell_word_partt::kindt::LITERAL;
    part.text = line;
    std::string error;
    auto p = compile_shell_pattern({part}, shell_dialectt::POSIX, error);
    if(!p.has_value())
    {
      table += "  {0, {0, 0, 0, 0}, 0},\n";
      continue;
    }
    const std::string f = "pat" + std::to_string(n++);
    std::string src = shell_pattern_matcher_source(*p, f, 8);
    std::cout << src.substr(src.find("_Bool " + f));
    table += "  {" + f + "_match, {" + f + "_remove_shortest_prefix, " + f +
             "_remove_longest_prefix, " + f + "_remove_shortest_suffix, " +
             f + "_remove_longest_suffix}, 1},\n";
  }
  std::cout << table << "};\n";
}
'''

HARNESS = r'''
#include <stdio.h>
#include <string.h>
int main(void)
{
  char line[64];
  while(fgets(line, sizeof line, stdin))
  {
    line[strcspn(line, "\n")] = 0;
    S s;
    memset(&s, 0, sizeof s);
    s.set = 1;
    s.length = strlen(line);
    memcpy(s.data, line, s.length);
    for(unsigned k = 0; k < sizeof entries / sizeof entries[0]; ++k)
    {
      if(!entries[k].ok)
      {
        printf("E|E|E|E|E|");
        continue;
      }
      printf("%c|", entries[k].match(s) ? 'y' : 'n');
      for(int j = 0; j < 4; ++j)
      {
        S r = entries[k].r[j](s);
        printf("%.*s|", (int)r.length, r.data);
      }
    }
    putchar('\n');
  }
}
'''

LIBS = ['shell-frontend', 'ansi-c', 'linking', 'goto-programs', 'langapi',
        'util', 'big-int', 'json', 'analyses', 'pointer-analysis',
        'assembler']


def quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    random.seed(3)
    alphabet = list('ab*?[]!-') + ['\\*', '[!a]', '[a-c]', '[]]',
                                    '[[:alpha:]]']
    patterns = sorted({''.join(random.choice(alphabet)
                               for _ in range(random.randint(0, 4)))
                       for _ in range(400)})
    subjects = sorted({''.join(random.choice('ab-]![*c')
                               for _ in range(random.randint(0, 5)))
                       for _ in range(50)})
    with tempfile.TemporaryDirectory() as work:
        gen_cpp = os.path.join(work, 'gen.cpp')
        with open(gen_cpp, 'w') as f:
            f.write(GENERATOR)
        gen = os.path.join(work, 'gen')
        subprocess.run(['g++', '-std=c++17', '-I', SRC, gen_cpp, '-o', gen,
                        '-L', os.path.join(sys.argv[1], 'lib'),
                        '-Wl,--start-group'] + ['-l' + l for l in LIBS] +
                       ['-Wl,--end-group'], check=True)
        source = subprocess.run([gen], input='\n'.join(patterns) + '\n',
                                capture_output=True, text=True,
                                check=True).stdout
        # Compile the runtime with a C compiler, without CBMC's built-ins.
        source = (source.replace('__CPROVER_assume', '(void)')
                  .replace('#pragma', '//pragma')
                  .replace('__CPROVER_HIDE:;', ''))
        harness_c = os.path.join(work, 'h.c')
        with open(harness_c, 'w') as f:
            f.write(source + HARNESS)
        harness = os.path.join(work, 'h')
        subprocess.run(['gcc', '-w', '-o', harness, harness_c], check=True)
        ours = subprocess.run([harness], input='\n'.join(subjects) + '\n',
                              capture_output=True, text=True,
                              check=True).stdout.splitlines()

    script = []
    for s in subjects:
        line = f'x={quote(s)}; '
        for p in patterns:
            line += (f'case "$x" in {p}) printf "y|";; *) printf "n|";; '
                     'esac; ')
            for op in ['#', '##', '%', '%%']:
                line += f'printf "%s|" "${{x{op}{p}}}"; '
        script.append(line + 'echo')
    failed = False
    for name, shell in [('dash', ['dash']), ('bash', ['bash']),
                        ('busybox', ['busybox', 'sh'])]:
        out = subprocess.run(shell, input='\n'.join(script) + '\n',
                             capture_output=True, text=True).stdout
        mismatches = []
        for s, a, b in zip(subjects, ours, out.splitlines()):
            for i, (x, y) in enumerate(zip(a.split('|'), b.split('|'))):
                if x != 'E' and x != y:
                    mismatches.append((s, patterns[i // 5],
                                       ['case', '#', '##', '%', '%%'][i % 5],
                                       x, y))
        print(f'{name}: {len(subjects)} subjects x {len(patterns)} patterns '
              f'x 5 operations, {len(mismatches)} mismatches')
        for m in mismatches[:20]:
            print('  ', m)
        failed |= bool(mismatches)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
