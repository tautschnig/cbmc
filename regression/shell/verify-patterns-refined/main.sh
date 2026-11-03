#!/bin/sh
case b in [!a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case a in [!a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case "]" in []a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case 5 in [[:digit:]]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case abc in a?c) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case abcd in a?c) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case "" in ?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case main.c in *.[ch]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case x-y.h in ?-*.[ch]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case "$1" in -[0-9]*) __CPROVER_assert "${1#-}" != "$1" ;; esac
case "$1" in [!-]*) __CPROVER_assert "${1#-}" = "$1" ;; esac
