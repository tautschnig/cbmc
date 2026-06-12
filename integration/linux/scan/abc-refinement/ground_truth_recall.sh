#!/bin/bash
# #2 ground-truth precision/recall for the count/index OOB-write discharge.
# Differential vuln/fixed test: for each real guarded candidate, the
# validator-PRESENT slice (fixed) must verify SAFE (precision = clears the
# guarded version), and the validator-ABSENT slice (the vulnerable/CVE shape,
# archetype CVE-2019-3701 cgw OOB-write) must FAIL (recall = flags the bug).
CBMC=/home/ubuntu/cbmc-github.git/build/bin/cbmc
gen(){ cat > /tmp/gt.c <<E
int main(void){ unsigned char arr[$1]; unsigned long v;
#ifdef FIXED
  __CPROVER_assume($2);
#endif
  $3
  return 0; }
E
}
run(){ timeout 30 $CBMC /tmp/gt.c $1 --bounds-check --unwind 2 2>&1 | grep -oE 'VERIFICATION (SUCCESSFUL|FAILED)'; }
pass=0; tot=0
chk(){ name=$1; gen "$2" "$3" "$4"
  f=$(run -DFIXED); v=$(run "")
  ok="FAIL"; [ "$f" = "VERIFICATION SUCCESSFUL" ] && [ "$v" = "VERIFICATION FAILED" ] && ok="OK" && pass=$((pass+1)); tot=$((tot+1))
  printf "  %-26s fixed=%-22s vuln=%-20s [%s]\n" "$name" "$f" "$v" "$ok"; }
ulimit -v 8000000
echo "ground-truth recall/precision (fixed must clear, vuln must flag):"
chk fl_set_key_mpls_lse 7  "v>=1 && v<=7" "unsigned long l=v-1; arr[l]=0;"
chk cec_num_log_addrs   4  "v<=4"         "unsigned long i;__CPROVER_assume(i<v);arr[i]=0;"
chk mqprio_num_tc       16 "v<=16"        "unsigned long i;__CPROVER_assume(i<v);arr[i]=0;"
chk taprio_num_tc       16 "v<=16"        "unsigned long i;__CPROVER_assume(i<v);arr[i]=0;"
echo "recall+precision pairs passed: $pass/$tot"
rm -f /tmp/gt.c
