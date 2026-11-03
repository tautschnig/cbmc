#!/bin/sh
case x.tar.gz in *.tar.*) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
__CPROVER_assert "${1#-}" != "$1"
