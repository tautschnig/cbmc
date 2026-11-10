/*
 * F18: ena_setup_rx_intr returns success after failing and undoing its work.
 *
 * ena_ethdev.c:3813-3878.  After rte_intr_efd_enable succeeds, rc is 0.  Two
 * later failures jump into the unwind chain WITHOUT setting rc:
 *
 *   3857  if (!rte_intr_allow_others(intr_handle))
 *   3860          goto disable_intr_efd;              <-- rc still 0
 *   3863  for (i = 0; i < vectors_nb; ++i)
 *   3864          if (rte_intr_vec_list_index_set(intr_handle, i, ...))
 *   3866                  goto disable_intr_efd;      <-- rc still 0
 *
 *   3871  disable_intr_efd:
 *   3872          rte_intr_efd_disable(intr_handle);
 *   3873  free_intr_vec:
 *   3874          rte_intr_vec_list_free(intr_handle);
 *   3875  enable_intr:
 *   3876          rte_intr_enable(intr_handle);
 *   3877          return rc;                           <-- returns 0 = success
 *
 * So the function disables the event file descriptors, frees the vector list,
 * and tells its caller everything is fine.  ena_start then continues to
 * ENA_ADAPTER_STATE_RUNNING and reports the port started with Rx queue
 * interrupts requested but not configured: the interrupts never fire, which
 * is Unveil class F (lost event / stuck queue), and the application has no
 * way to know.
 *
 * The obligation below is the contract the caller relies on: a zero return
 * means the interrupt setup is in place.  The ghosts track what the EAL calls
 * did, so "in place" is decided by the calls the function actually made, not
 * by its return value.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

static int nd_int(void) { int v; return v; }

/* ghost model of the EAL interrupt state */
static bool g_efd_enabled;   /* rte_intr_efd_enable succeeded and not undone */
static bool g_vec_allocated; /* the vector list is allocated */

int rte_log(uint32_t l, uint32_t t, const char *f, ...)
{
  (void)l;
  (void)t;
  (void)f;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *n, uint32_t l)
{
  (void)n;
  (void)l;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }

int rte_intr_cap_multiple(struct rte_intr_handle *h)
{
  (void)h;
  return 1; /* the PCI driver supports multiple vectors */
}
int rte_intr_disable(struct rte_intr_handle *h)
{
  (void)h;
  return 0;
}
int rte_intr_enable(struct rte_intr_handle *h)
{
  (void)h;
  return 0;
}
int rte_intr_vec_list_alloc(
  struct rte_intr_handle *h,
  const char *name,
  uint16_t size)
{
  (void)h;
  (void)name;
  (void)size;
  if(nd_int())
    return -1;
  g_vec_allocated = true;
  return 0;
}
void rte_intr_vec_list_free(struct rte_intr_handle *h)
{
  (void)h;
  g_vec_allocated = false;
}
int rte_intr_efd_enable(struct rte_intr_handle *h, uint32_t nb_efd)
{
  (void)h;
  (void)nb_efd;
  if(nd_int())
    return -1;
  g_efd_enabled = true;
  return 0;
}
void rte_intr_efd_disable(struct rte_intr_handle *h)
{
  (void)h;
  g_efd_enabled = false;
}
/* the two calls whose failure forgets to set rc */
int rte_intr_allow_others(struct rte_intr_handle *h)
{
  (void)h;
#ifdef ONLY_INDEX_SET_FAILS
  return 1; /* this check passes, so only the vec_list_index_set path remains */
#else
  return nd_int() ? 1 : 0;
#endif
}
int rte_intr_vec_list_index_set(
  struct rte_intr_handle *h,
  int index,
  int vec)
{
  (void)h;
  (void)index;
  (void)vec;
#ifdef ONLY_ALLOW_OTHERS_FAILS
  return 0; /* this call succeeds, so only the allow_others path remains */
#else
  return nd_int() ? -1 : 0;
#endif
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_pci_device pci_dev;
  /* rte_intr_handle is opaque (declared, never defined, in
   * rte_interrupts.h), so the harness only needs a distinct non-NULL
   * address; nothing here dereferences it. */
  static uint8_t handle_storage[64];

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  pci_dev.intr_handle = (struct rte_intr_handle *)handle_storage;

  /* the application asked for Rx queue interrupts */
  data.dev_conf.intr_conf.rxq = 1;
  data.nb_rx_queues = 1;

  g_efd_enabled = false;
  g_vec_allocated = false;

  int rc = ena_setup_rx_intr(&dev);

  /* THE CONTRACT: a zero return must mean the setup is in place. */
  __CPROVER_assert(
    rc != 0 || (g_efd_enabled && g_vec_allocated),
    "F18_SETUP_RC: ena_setup_rx_intr returns 0 only if the Rx interrupt "
    "setup is actually in place");

#ifdef VACUITY_CHECK
  /* each must FAIL, or the obligation is about an unreachable outcome */
  __CPROVER_assert(rc != 0, "vacuity witness: the setup can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: the setup can fail");
  __CPROVER_assert(
    g_efd_enabled, "vacuity witness: the efd can end up disabled");
#endif
  return 0;
}
