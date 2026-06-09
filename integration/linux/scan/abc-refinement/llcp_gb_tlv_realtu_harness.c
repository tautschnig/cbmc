// Harness for the REAL, full-TU-compiled nfc_llcp_parse_gb_tlv from
// net/nfc/llcp_commands.c (goto-cc'd with the real kernel headers).
// Linked against /tmp/llcp.gb; CBMC verifies the actual compiled function
// (real loop, real llcp_tlv8/16 helpers, real llcp_tlv_length table) with
// a buffer of exactly tlv_array_len bytes.
typedef unsigned char u8;
typedef unsigned short u16;

struct nfc_llcp_local; // opaque; the real type comes from the linked TU
extern int
nfc_llcp_parse_gb_tlv(struct nfc_llcp_local *, const u8 *, u16);

extern void *malloc(unsigned long);

int harness(void)
{
  u16 len;
  __CPROVER_assume(len >= 1 && len <= 8); // bounded model of the GB array

  u8 *tlv = malloc(len); // exactly `len` valid bytes (as received)
  __CPROVER_assume(tlv != 0);

  // over-allocate the local struct so its field writes stay in bounds;
  // the property under test is the OOB *read* of `tlv`.
  struct nfc_llcp_local *local = malloc(65536);
  __CPROVER_assume(local != 0);

  return nfc_llcp_parse_gb_tlv(local, tlv, len);
}
