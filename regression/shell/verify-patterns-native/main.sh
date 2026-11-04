#!/bin/sh
# The expected values are those that dash computes.
d=mid
x=a-b-c.tar.gz
__CPROVER_assert "${x#*-?-*}" = c.tar.gz
__CPROVER_assert "${x%*-?-*}" = a
__CPROVER_assert "${x%*.[tg]*}" = a-b-c.tar
x=5.10.0-21-amd64
__CPROVER_assert "${x%%[!0-9]*}" = 5
__CPROVER_assert "${x##*[!0-9.]}" = 64
x=x.mid.y.mid.z
__CPROVER_assert "${x##*[.]"$d"*}" = ''
__CPROVER_assert "${x%*"$d"?}" = x.mid.y.mid.z
case lat9x16.psf.gz in *[0-9]x[0-9]*.psf.gz) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case '#mid#' in *#"$d"#*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
case xmid in ?"$d"*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
