#!/bin/bash
# Ground-truth precision/recall for the CBMC slice discharge, across asset
# shapes.  Differential vuln/fixed test: each `pair` provides a C body with
# the validator under `#ifdef FIXED`.  The FIXED build (validator present =
# patched kernel) must verify SAFE (precision); the vuln build (validator
# absent = the pre-fix / CVE shape) must FAIL (recall).  Archetypes:
# count/index OOB-write CVE-2019-3701; skb read-before-length-check L2CAP/
# btmtk family; A2 copy-length over-read infoleak.
CBMC=/home/ubuntu/cbmc-github.git/build/bin/cbmc
pass=0; tot=0
pair(){ # name, cbmc-flags, body
  printf '#include <stdlib.h>\nint main(void){\n%s\nreturn 0;\n}\n' "$3" > /tmp/gt.c
  f=$(timeout 40 $CBMC /tmp/gt.c -DFIXED $2 --unwind 2 2>&1 | grep -oE 'VERIFICATION (SUCCESSFUL|FAILED)')
  v=$(timeout 40 $CBMC /tmp/gt.c $2 --unwind 2 2>&1 | grep -oE 'VERIFICATION (SUCCESSFUL|FAILED)')
  ok="FAIL"; [ "$f" = "VERIFICATION SUCCESSFUL" ] && [ "$v" = "VERIFICATION FAILED" ] && { ok="OK"; pass=$((pass+1)); }
  tot=$((tot+1)); printf "  %-30s fixed=%-12s vuln=%-12s [%s]\n" "$name=$1" \
    "${f##* }" "${v##* }" "$ok"; }
ulimit -v 8000000

echo "## count/index OOB-write (CVE-2019-3701 shape)"
name=cnt
pair fl_set_key_mpls_lse "--bounds-check" \
'unsigned char arr[7]; unsigned long v;
#ifdef FIXED
__CPROVER_assume(v>=1 && v<=7);
#endif
unsigned long l=v-1; arr[l]=0;'
pair cec_num_log_addrs "--bounds-check" \
'unsigned char arr[4]; unsigned long v;
#ifdef FIXED
__CPROVER_assume(v<=4);
#endif
unsigned long i; __CPROVER_assume(i<v); arr[i]=0;'
pair mqprio_num_tc "--bounds-check" \
'unsigned char arr[16]; unsigned long v;
#ifdef FIXED
__CPROVER_assume(v<=16);
#endif
unsigned long i; __CPROVER_assume(i<v); arr[i]=0;'

echo "## skb/cursor field-read-before-length-check (L2CAP/btmtk shape)"
name=skb
pair read_field_at_offset "--pointer-check --bounds-check" \
'unsigned long len; __CPROVER_assume(len<=64);
unsigned char *buf=malloc(len); __CPROVER_assume(buf!=0);
#ifdef FIXED
__CPROVER_assume(len>=6);
#endif
unsigned char x=buf[5]; (void)x;'
pair decode_le16_at_cursor "--pointer-check --bounds-check" \
'unsigned long len; __CPROVER_assume(len<=64);
unsigned char *p=malloc(len); __CPROVER_assume(p!=0);
unsigned long off; __CPROVER_assume(off<=len);
#ifdef FIXED
__CPROVER_assume(off+2<=len);
#endif
unsigned short v=p[off]|(p[off+1]<<8); (void)v;'

echo "## A2 confidentiality: copy-length over-read of source (infoleak shape)"
name=a2
pair copy_to_user_overread "--bounds-check" \
'unsigned long len; __CPROVER_assume(len<=256);
unsigned char src[16];
#ifdef FIXED
__CPROVER_assume(len<=16);
#endif
unsigned long i; __CPROVER_assume(i<len); unsigned char v=src[i]; (void)v;'

echo
echo "recall+precision pairs passed: $pass/$tot"
rm -f /tmp/gt.c
