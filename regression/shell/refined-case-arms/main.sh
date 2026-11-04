#!/bin/sh
case "$1" in
  1)
    x=''; d='a'
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'y'
    r="${x#}"; __CPROVER_assert "$r" = ''
    r="${x##}"; __CPROVER_assert "$r" = ''
    r="${x%}"; __CPROVER_assert "$r" = ''
    r="${x%%}"; __CPROVER_assert "$r" = ''
    ;;
  2)
    x='!-a['; d=''
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#}"; __CPROVER_assert "$r" = '!-a['
    r="${x##}"; __CPROVER_assert "$r" = '!-a['
    r="${x%}"; __CPROVER_assert "$r" = '!-a['
    r="${x%%}"; __CPROVER_assert "$r" = '!-a['
    ;;
  3)
    x='*!**['; d='b*'
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#}"; __CPROVER_assert "$r" = '*!**['
    r="${x##}"; __CPROVER_assert "$r" = '*!**['
    r="${x%}"; __CPROVER_assert "$r" = '*!**['
    r="${x%%}"; __CPROVER_assert "$r" = '*!**['
    ;;
  4)
    x='*][a'; d='a'
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#}"; __CPROVER_assert "$r" = '*][a'
    r="${x##}"; __CPROVER_assert "$r" = '*][a'
    r="${x%}"; __CPROVER_assert "$r" = '*][a'
    r="${x%%}"; __CPROVER_assert "$r" = '*][a'
    ;;
  5)
    x='*ba'; d=''
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#}"; __CPROVER_assert "$r" = '*ba'
    r="${x##}"; __CPROVER_assert "$r" = '*ba'
    r="${x%}"; __CPROVER_assert "$r" = '*ba'
    r="${x%%}"; __CPROVER_assert "$r" = '*ba'
    ;;
  6)
    x='-['; d='b*'
    case "$x" in "") r=y ;; *) r=n ;; esac; __CPROVER_assert "$r" = 'n'
    r="${x#}"; __CPROVER_assert "$r" = '-['
    r="${x##}"; __CPROVER_assert "$r" = '-['
    r="${x%}"; __CPROVER_assert "$r" = '-['
    r="${x%%}"; __CPROVER_assert "$r" = '-['
    ;;
esac