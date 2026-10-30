#!/bin/bash
case b in [^a]) r=1 ;; *) r=0 ;; esac
__CPROVER_assert "$r" = 1
