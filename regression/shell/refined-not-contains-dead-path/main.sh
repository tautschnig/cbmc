#!/bin/sh
case "$1" in
  1)
    x=''; d='a'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = ''
    ;;
  2)
    x='!'; d=''
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = '!'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = '!'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = '!'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = '!'
    ;;
  3)
    x='-*'; d='b*'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = '-*'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = '-*'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = '-*'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = '-*'
    ;;
  4)
    x='--'; d='a'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = '--'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = '--'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = '--'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = '--'
    ;;
  5)
    x='[*'; d=''
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = '[*'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = '[*'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = '[*'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = '[*'
    ;;
  6)
    x=']'; d='b*'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = ']'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = ']'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = ']'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = ']'
    ;;
  7)
    x='a!-a'; d='a'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = 'a!-a'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = 'a!-a'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = 'a!-a'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = 'a!-a'
    ;;
  8)
    x='b*'; d=''
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'y'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = ''
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = ''
    ;;
  9)
    x='bbb!!'; d='b*'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = 'bbb!!'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = 'bbb!!'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = 'bbb!!'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = 'bbb!!'
    ;;
  10)
    x='c*-[b'; d='a'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = 'c*-[b'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = 'c*-[b'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = 'c*-[b'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = 'c*-[b'
    ;;
  11)
    x='c['; d=''
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = 'c['
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = 'c['
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = 'c['
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = 'c['
    ;;
  12)
    x='cb]'; d='b*'
    case "$x" in "$d"b*?) r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#"$d"b*?}"; __CPROVER_assert "$r" = 'cb]'
    r="${x##"$d"b*?}"; __CPROVER_assert "$r" = 'cb]'
    r="${x%"$d"b*?}"; __CPROVER_assert "$r" = 'cb]'
    r="${x%%"$d"b*?}"; __CPROVER_assert "$r" = 'cb]'
    ;;
esac
