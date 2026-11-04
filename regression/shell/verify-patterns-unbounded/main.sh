#!/bin/sh
# The expected values are those that dash computes.
d=mid
x=a-b-c.tar.gz
__CPROVER_assert "${x##*.[tg]*}" = ''
x=a-b-c.tar.gz
__CPROVER_assert "${x%*.[tg]*}" = a-b-c.tar
x=abcabc
__CPROVER_assert "${x#a*c*b}" = c
x=abcabc
__CPROVER_assert "${x%%[ab]*c}" = ''
x='x  y	z'
__CPROVER_assert "${x#[! 	][ 	]}" = ' y	z'
x='x  y	z'
__CPROVER_assert "${x##[! 	][ 	]}" = ' y	z'
x='x	y z'
__CPROVER_assert "${x%[ 	]*}" = 'x	y'
x='x	y z'
__CPROVER_assert "${x%%[ 	]*}" = x
x=5.10.0-21-amd64
__CPROVER_assert "${x%%[!0-9]*}" = 5
x=5.10.0-21-amd64
__CPROVER_assert "${x##*[!0-9.]}" = 64
x=5.10.0-21-amd64
__CPROVER_assert "${x%*[0-9]}" = 5.10.0-21-amd6
x=--name=value=x
__CPROVER_assert "${x#*=*}" = value=x
x=--name=value=x
__CPROVER_assert "${x%*=*}" = --name=value
x=--name=value=x
__CPROVER_assert "${x##-?*=}" = x
x=--name
__CPROVER_assert "${x#??-*}" = --name
x=/dev/loop7
__CPROVER_assert "${x%%/dev/loop[0-9]}" = ''
x=key:Amiga:x:y
__CPROVER_assert "${x#*:[Aa]miga:*}" = x:y
x=key:Amiga:x:y
__CPROVER_assert "${x%*:[Aa]miga:*}" = key
x=abc
__CPROVER_assert "${x%?*?}" = a
x=aXbXc
__CPROVER_assert "${x##*X?}" = ''
x=aXbXc
__CPROVER_assert "${x%?X*}" = aX
x='hello world'
__CPROVER_assert "${x#*[aeiou]*[aeiou]*}" = ' world'
x='hello world'
__CPROVER_assert "${x%*[aeiou]*[aeiou]*}" = hell
x=pre.mid.suf
__CPROVER_assert "${x#"$d"*}" = pre.mid.suf
x=pre.mid.suf
__CPROVER_assert "${x%%*"$d"*}" = ''
x=pre.mid.suf
__CPROVER_assert "${x#?"$d"*}" = pre.mid.suf
x=x.mid.y.mid.z
__CPROVER_assert "${x%*"$d"?}" = x.mid.y.mid.z
x=x.mid.y.mid.z
__CPROVER_assert "${x##*[.]"$d"*}" = ''
case a1b in *[0-9]*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case ab in *[0-9]*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case key:Amiga:x:y in *:[Aa]miga:*:*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case key:amigo:x:y in *:[Aa]miga:*:*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case 'it'"'"'s' in *\'?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case 'its'"'"'' in *\'?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case 'a b' in ?*\ ?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case 'ab ' in ?*\ ?*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case 1.2.3 in [0-9]*.[0-9]*.[0-9]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case 1.2.x in [0-9]*.[0-9]*.[0-9]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case pre.mid.suf in "$d"*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case 'a mid b' in *\ "$d"\ *) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case mid in ?"$d"*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
case xmid in ?"$d"*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case '#mid#' in *#"$d"#*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case mid/x in "$d"/*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case -1 in -*[a-z]*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 0
