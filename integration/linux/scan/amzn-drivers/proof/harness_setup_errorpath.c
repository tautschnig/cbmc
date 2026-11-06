/*
 * Error-path obligations for ena_tx_queue_setup (Unveil class C, and the 61%
 * error-path trigger).
 *
 * The invariant proofs so far all ask "from a good state, does one operation
 * keep the state good?".  That misses the dominant shape in the Unveil
 * findings, which is: an operation FAILS, the driver returns an error, and the
 * state it leaves behind breaks a later legal operation.  The obligation for
 * that shape is not an invariant but a frame condition on failure:
 *
 *   if the operation fails, it must leave nothing behind that a subsequent
 *   legal call can trip over.
 *
 * This harness states three such obligations for ena_tx_queue_setup, whose
 * allocation failures are already injectable, and then performs the one
 * operation DPDK is entitled to call next.
 *
 * ERR_NO_PUBLISH   a failed setup must not publish the ring into
 *                  dev->data->tx_queues[], because ethdev treats a published
 *                  queue as usable.
 * ERR_NOT_CONFIGURED  a failed setup must not mark the ring configured.
 * ERR_NO_LEAK      every allocation a failed setup made must be released
 *                  (ghost counter, so this also covers Unveil class D).
 * ERR_RELEASE_SAFE a release after a failed setup must not fault.  This is
 *                  the interesting one and it FAILS: see the note below.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#define RING (CONCRETE_RING)
#define PUSH_BUF_SIZE 96

static uint16_t nd_u16(void) { uint16_t v; return v; }

static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[PUSH_BUF_SIZE];
static bool alloc_tbi_fail, alloc_etr_fail, alloc_push_fail;

/* Ghost allocation counter: one up per successful allocation, one down per
 * release.  A failed setup must bring it back to zero. */
static int g_live_allocs;

void *rte_zmalloc_socket(
  const char *type,
  size_t size,
  unsigned align,
  int socket)
{
  (void)type;
  (void)align;
  (void)socket;

  if(size == sizeof(pool_tbi))
  {
    if(alloc_tbi_fail)
      return NULL;
    memset(pool_tbi, 0, sizeof(pool_tbi));
    g_live_allocs++;
    return pool_tbi;
  }
  if(size == sizeof(pool_etr))
  {
    if(alloc_etr_fail)
      return NULL;
    memset(pool_etr, 0, sizeof(pool_etr));
    g_live_allocs++;
    return pool_etr;
  }
  if(size == PUSH_BUF_SIZE)
  {
    if(alloc_push_fail)
      return NULL;
    memset(pool_push, 0, sizeof(pool_push));
    g_live_allocs++;
    return pool_push;
  }
  return NULL;
}

void rte_free(void *p)
{
  if(p != NULL)
    g_live_allocs--;
}

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *name, uint32_t level_def)
{
  (void)name;
  (void)level_def;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *driver)
{
  (void)driver;
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_eth_txconf tx_conf;
  static void *tx_queues[1];
  uint16_t nb_desc;
  int rc;

  dev.data = &data;
  data.dev_private = &adapter;

  /*
   * The state ethdev is in when it calls us: rte_eth_dev_configure() has
   * already set nb_tx_queues and allocated a zeroed tx_queues array, so the
   * entry is NULL until a setup succeeds.  That ordering is what makes the
   * release obligation below reachable.
   */
  data.tx_queues = tx_queues;
  data.nb_tx_queues = 1;
  tx_queues[0] = NULL;

  alloc_tbi_fail = nd_u16() & 1;
  alloc_etr_fail = nd_u16() & 1;
  alloc_push_fail = nd_u16() & 1;
  g_live_allocs = 0;

  nb_desc = RING;
  adapter.max_tx_ring_size = RING;
  adapter.tx_ring[0].configured = 0;
  adapter.tx_ring[0].tx_max_header_size = PUSH_BUF_SIZE;

  rc = ena_tx_queue_setup(&dev, 0, nb_desc, 0, &tx_conf);

  if(rc != 0)
  {
    __CPROVER_assert(
      data.tx_queues[0] == NULL,
      "ERR_NO_PUBLISH: a failed setup does not publish the ring");
    __CPROVER_assert(
      adapter.tx_ring[0].configured == 0,
      "ERR_NOT_CONFIGURED: a failed setup does not mark the ring configured");
    __CPROVER_assert(
      g_live_allocs == 0,
      "ERR_NO_LEAK: a failed setup releases everything it allocated");

#if RELEASE_AFTER_FAILURE
    /*
     * ERR_RELEASE_SAFE.  After a failed setup, the next thing DPDK is
     * entitled to do is close the device, and ena_close() calls
     * ena_tx_queue_release_all(), which loops i < dev->data->nb_tx_queues and
     * calls ena_tx_queue_release(dev, i).  That function begins
     *
     *     struct ena_ring *ring = dev->data->tx_queues[qid];
     *     rte_free(ring->push_buf_intermediate_buf);
     *
     * with no NULL check on `ring`.  Since a failed setup leaves the entry
     * NULL while nb_tx_queues still counts the queue, this dereferences NULL.
     * --pointer-check reports it, which is the finding rather than a harness
     * artefact: both the precondition and the call are what ethdev does.
     */
    ena_tx_queue_release(&dev, 0);
#endif
  }

  return 0;
}
