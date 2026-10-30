#!/bin/sh
x=a/b/c.txt
__CPROVER_assert "${x##*/}" = c.txt
__CPROVER_assert "${x#*/}" = b/c.txt
__CPROVER_assert "${x%/*}" = a/b
__CPROVER_assert "${x%%/*}" = a
__CPROVER_assert "${x%.*}" = a/b/c
__CPROVER_assert "${x%.txt}" = a/b/c
__CPROVER_assert "${x#a/}" = b/c.txt
__CPROVER_assert "${x#z}" = "$x"
y=aaa
__CPROVER_assert "${y#a*}" = aa
__CPROVER_assert "${y##a*}" = ""
__CPROVER_assert "${y%a*}" = aa
__CPROVER_assert "${y%%a*}" = ""
z="a*b"
__CPROVER_assert "${z#"a*"}" = b
__CPROVER_assert "${z#a\*}" = b
p=b
__CPROVER_assert "${z%"$p"}" = "a*"
case abc in a*c) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case "a*c" in "a*c") r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case axc in "a*c") r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case b in [!a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case "]" in []a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case - in [a-]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case 5 in [[:digit:]]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case "[" in [) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case ab in a[) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case x.tar.gz in *.tar.*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case "" in ?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case "$1" in --*=*) __CPROVER_assert "${1%%=*}" != "$1" ;; esac
case "$1" in -[0-9]*) __CPROVER_assert "${1#-}" != "$1" ;; esac
