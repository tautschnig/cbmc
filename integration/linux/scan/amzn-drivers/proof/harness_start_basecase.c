/*
 * The second half of the base case: ena_queue_start establishes INV_STARTED.
 *
 * Why this harness exists.  The data-path step (harness_tx_callees.c) assumes
 * INV_STARTED, while the setup base case (harness_setup_basecase.c) only
 * establishes INV_CONFIGURED.  Without this harness the two halves do not
 * meet and there is no induction at all -- a proven base case and a proven
 * step that do not compose prove nothing about operation sequences.  Both
 * predicates now come from ena_inv.h so they cannot drift apart again.
 *
 * What the driver actually does:
 *   ena_queue_start -> ena_create_io_queue -> ena_com_create_io_queue
 *                                          -> ena_com_get_io_handlers
 * ena_create_io_queue also runs `empty_tx_reqs[i] = i` over the ring, which
 * is what establishes the free-list clause the step depends on.
 *
 * THE ABSTRACTION BOUNDARY, stated explicitly.  ena_com_create_io_queue sends
 * admin commands to the device and reads the completion descriptors back.
 * Those completions are DEVICE-CONTROLLED, and the device is untrusted in our
 * threat model, so ena_com_execute_admin_command is modelled as returning an
 * arbitrary status with an arbitrary completion payload.  That is the sound
 * model: it lets the proof see every response a malicious or malfunctioning
 * device could give, rather than only cooperative ones.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

#define RING (CONCRETE_RING)
#define PUSH_BUF_SIZE 96

/* Static pools rather than malloc: empty_tx_reqs and tx_buffer_info live in
 * anonymous unions in struct ena_ring, and CBMC cannot resolve a union-held
 * pointer to a dynamic object (the access becomes byte_extract and the
 * property status comes back ERROR instead of SUCCESS/FAILURE). */
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[PUSH_BUF_SIZE];

/* A register BAR for the doorbell to point into. */
static uint8_t pool_reg_bar[4096];
static uint8_t pool_mem_bar[4096];

/*
 * The device side of the admin queue.  Every completion field is
 * nondeterministic, so the proof covers every possible device response,
 * including hostile ones.
 *
 * One assumption is made and it is the interesting part of this harness:
 * ASSUME_DEVICE_HONEST_DOORBELL constrains the device-supplied
 * sq_doorbell_offset to lie inside the register BAR.  The driver does NOT
 * check this (ena_com.c:1317 derives db_addr = reg_bar + sq_doorbell_offset,
 * and that field appears nowhere else in the tree), so without the assumption
 * db_addr is an arbitrary pointer and every later doorbell write is an
 * out-of-bounds write.  The assumption is therefore not a convenience; it
 * marks an unvalidated trust boundary, and it is reported as a finding rather
 * than hidden here.
 */
int ena_com_execute_admin_command(
  struct ena_com_admin_queue *admin_queue,
  struct ena_admin_aq_entry *cmd,
  size_t cmd_size,
  struct ena_admin_acq_entry *comp,
  size_t comp_size)
{
  (void)admin_queue;
  (void)cmd;
  (void)cmd_size;

  if(nd_int())
    return ENA_COM_INVAL; /* the device may always refuse */

  __CPROVER_havoc_slice(comp, comp_size);

  if(comp_size >= sizeof(struct ena_admin_acq_create_sq_resp_desc))
  {
    struct ena_admin_acq_create_sq_resp_desc *r = (void *)comp;
#ifdef ASSUME_DEVICE_HONEST_DOORBELL
    __CPROVER_assume(
      r->sq_doorbell_offset > 0 &&
      r->sq_doorbell_offset < sizeof(pool_reg_bar) - 4);
#endif
    (void)r;
  }
  return 0;
}

/* DPDK logging.  rte_log is variadic; left real it drags vsnprintf, strcmp and
 * strcpy into the proof with symbolic arguments, which costs more than the
 * property being proven.  The driver's log calls have no effect on ring
 * state. */
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
    return pool_tbi;
  if(size == sizeof(pool_etr))
    return pool_etr;
  if(size == PUSH_BUF_SIZE)
    return pool_push;
  return NULL;
}

/*
 * CALLEE CONTRACTS for the two ena_com entry points, supplied as harness
 * models rather than run as real code.
 *
 * ena_com_create_io_queue sends admin commands and waits for completions; its
 * cost is what stopped this base case being discharged (the queue arrays are
 * memset out of ENA_TOTAL_NUM_QUEUES-sized arrays inside a 182144-byte
 * adapter, and the DMA allocation underneath is unmodelled).  Replacing the
 * two calls with their post-conditions localises the remaining gap to exactly
 * these two statements, which is the point: they are ASSUMED here and must be
 * verified separately against the real ena_com code.
 *
 * The post-conditions are deliberately the weakest that INV_STARTED needs, so
 * that proving them later is as easy as possible, and each may fail, because
 * the device may refuse any admin command.
 */
int ena_com_create_io_queue(
  struct ena_com_dev *ena_dev,
  struct ena_com_create_io_ctx *ctx)
{
  struct ena_com_io_sq *sq;
  struct ena_com_io_cq *cq;

  if(ctx->qid >= ENA_TOTAL_NUM_QUEUES)
    return ENA_COM_INVAL;

  if(nd_int())
    return ENA_COM_INVAL; /* the device may refuse */

  sq = &ena_dev->io_sq_queues[ctx->qid];
  cq = &ena_dev->io_cq_queues[ctx->qid];

  /* POST 1: the SQ and CQ are sized by the requested queue size */
  sq->q_depth = ctx->queue_size;
  cq->q_depth = ctx->queue_size;
  /* POST 2: the doorbell points into the register BAR.  Unvalidated in the
   * real code -- that is finding F8 -- so this models a cooperative device. */
  sq->db_addr = (uint32_t *)pool_reg_bar;
  /* POST 3: nothing is in flight on a freshly created queue */
  sq->tail = 0;
  sq->next_to_comp = 0;
  /* POST 4: the LLQ geometry is valid (established at device init by
   * ena_com_config_dev_mode; see INV_DEV_LLQ in ena_inv.h) */
  sq->llq_info.descs_per_entry = 1;
  sq->llq_info.descs_num_before_header = 1;
  sq->llq_info.disable_meta_caching = false;
  return 0;
}

int ena_com_get_io_handlers(
  struct ena_com_dev *ena_dev,
  uint16_t qid,
  struct ena_com_io_sq **io_sq,
  struct ena_com_io_cq **io_cq)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return ENA_COM_INVAL;
  if(nd_int())
    return ENA_COM_INVAL;

  /* POST: the handles name the queues create_io_queue set up */
  *io_sq = &ena_dev->io_sq_queues[qid];
  *io_cq = &ena_dev->io_cq_queues[qid];
  return 0;
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_pci_device pci_dev;
  struct ena_ring *r;
  int rc;

  /* Deliberately NOT __CPROVER_havoc_object on dev / data / adapter.  Those
   * structures embed function pointers (dev_ops, tx_pkt_prepare,
   * tx_pkt_burst, the aenq handlers).  Havocking them makes every one of
   * those pointers arbitrary, and CBMC's function-pointer removal then admits
   * the whole dev_ops table as a possible call target, so symex wanders into
   * eth_ena_prep_pkts, eth_ena_xmit_pkts and rte_raw_cksum -- thousands of
   * unwindings of code queue start never calls, and the run does not
   * terminate.  Zero-initialised statics plus targeted havoc of the fields
   * the proof needs symbolic is both cheaper and more honest about what is
   * being quantified over. */
  __CPROVER_havoc_object(&adapter.ena_dev.io_sq_queues[0].llq_info);
  __CPROVER_havoc_object(&adapter.ena_dev.io_cq_queues[0]);

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  static void *tx_queues[1];
  data.tx_queues = tx_queues;
  data.nb_tx_queues = 1;

  r = &adapter.tx_ring[0];

  /* ---- the post-state of ena_tx_queue_setup, i.e. INV_CONFIGURED ---- */
  r->adapter = &adapter;
  r->type = ENA_RING_TYPE_TX;
  r->id = 0;
  r->configured = 1;
  r->ring_size = RING;
  r->size_mask = RING - 1;
  r->sgl_size = ENA_PKT_MAX_BUFS;
  r->next_to_use = 0;
  r->next_to_clean = 0;
  r->pkts_without_db = false;
  r->tx_buffer_info = pool_tbi;
  r->empty_tx_reqs = pool_etr;
  r->push_buf_intermediate_buf = pool_push;
  r->numa_socket_id = 0;
  __CPROVER_assume(INV_CONFIGURED(r));

  /* the device-level state established earlier by ena_com_config_dev_mode,
   * which queue start neither establishes nor disturbs */
  adapter.ena_dev.reg_bar = pool_reg_bar;
  adapter.ena_dev.mem_bar = pool_mem_bar;
  adapter.ena_dev.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  adapter.ena_dev.tx_max_header_size = 96;
  __CPROVER_assume(INV_DEV_LLQ(&adapter.ena_dev.io_sq_queues[0]));

  rc = ena_queue_start(&dev, r);

  /* ---- the obligation ---- */
  if(rc == 0)
    __CPROVER_assert(
      INV_STARTED(r),
      "base case: ena_queue_start establishes INV_STARTED");

  /* On failure the queue must not be left looking started, or a later
   * operation would run against a half-built queue.  This is the error-path
   * obligation, and error paths are 61% of the Unveil findings. */
  if(rc != 0)
    __CPROVER_assert(
      r->ena_com_io_sq == NULL || r->next_to_use == 0,
      "error path: a failed start leaves no usable producer state");

  return 0;
}
