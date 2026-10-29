#!/bin/bash
# Parse every script in corpus.txt with shfmt in the dialect named by its
# shebang (sh/dash -> posix, bash -> bash, zsh -> zsh, ksh -> mksh).
# Also parse every /bin/sh script in bash mode to see how much of the
# POSIX-mode failure is shfmt's older-POSIX strictness.
SHFMT=${SHFMT:-$HOME/tools/bin/shfmt}
: > results.tsv
while read -r f; do
  first=$(head -1 "$f")
  case "$first" in
    *bash*) ln=bash ;; *zsh*) ln=zsh ;; *ksh*) ln=mksh ;; *) ln=posix ;;
  esac
  if err=$(timeout 10 "$SHFMT" -ln "$ln" --to-json < "$f" 2>&1 >/dev/null); then
    st=ok
  else
    st=fail
  fi
  alt=-
  if [ "$ln" = posix ]; then
    timeout 10 "$SHFMT" -ln bash --to-json < "$f" >/dev/null 2>&1 && alt=ok || alt=fail
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$f" "$ln" "$st" "$alt" "${err%%$'\n'*}" >> results.tsv
done < corpus.txt
