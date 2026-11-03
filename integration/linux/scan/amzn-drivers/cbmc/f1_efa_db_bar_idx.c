/* Verbatim slice of efa_request_doorbell_bar() (amzn-drivers efa_main.c:232-265,
 * identical in mainline drivers/infiniband/hw/efa/efa_main.c:190-218).
 * BIT(), pci_select_bars(), pci_resource_start() modelled as in linux 6.12. */
#include <assert.h>
#define UL(x) (x##UL)
#define BIT(nr) (UL(1) << (nr))
#define PCI_NUM_RESOURCES 17 /* x86_64 6.12: 6 std + ROM + 4 IOV + 6 bridge */
#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))
struct resource
{
  unsigned long start, end, flags;
};
struct pci_dev
{
  struct resource resource[PCI_NUM_RESOURCES];
};
#define pci_resource_start(dev, bar) ((dev)->resource[(bar)].start)
unsigned short nondet_u16(void);
int nondet_int(void);
int pci_select_bars(struct pci_dev *d)
{
  int b = nondet_int();
  __CPROVER_assume((b & ~0x1ffff) == 0);
  return b;
}
int pci_request_selected_regions(struct pci_dev *d, int bars)
{
  return 0;
}
int main(void)
{
  struct pci_dev pdev;
  unsigned short db_bar_field =
    nondet_u16(); /* resp.u.device_attr.db_bar (u16, from device) */
  unsigned char db_bar_idx =
    (unsigned char)db_bar_field;     /* result->db_bar is u8 */
  __CPROVER_assume(db_bar_idx < 64); /* exclude the separate UB-shift range */
  int pci_mem_bars, db_bar;
  db_bar = BIT(db_bar_idx);
  if(!(db_bar & EFA_BASE_BAR_MASK))
  {
    pci_mem_bars = pci_select_bars(&pdev);
    if(db_bar & ~pci_mem_bars)
      return -19; /* -ENODEV */
    if(pci_request_selected_regions(&pdev, db_bar))
      return -1;
  }
  unsigned long addr =
    pci_resource_start(&pdev, db_bar_idx); /* bounds-checked by CBMC */
  return (int)addr;
}
