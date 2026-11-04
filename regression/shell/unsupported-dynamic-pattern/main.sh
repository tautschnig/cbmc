#!/bin/sh
p="a*"
case abc in $p) : ;; *) __CPROVER_assert 0 = 1 ;; esac
case "a*" in "$p") : ;; *) __CPROVER_assert 0 = 1 ;; esac
case abc in "$p") __CPROVER_assert 0 = 1 ;; esac
