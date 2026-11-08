/*
 * Verify the two ena_com callee models used by the queue-start base case
 * (harness_start_basecase.c) against the REAL ena_com code.
 *
 * The base case replaces ena_com_create_io_queue and ena_com_get_io_handlers
 * by models that state the weakest post-conditions INV_STARTED needs.  The
 * induction is only as good as those models, so this harness runs the real
 * functions and asserts each modelled post-condition.  Whatever the real code
 * guarantees beyond the model is irrelevant; whatever the model promises and
 * the real code does not is a hole in the induction.
 *
 * Boundaries modelled here, each as weakly as possible:
 *   - ena_com_execute_admin_command: the device answers.  Any status, any
 *     completion payload (the device is untrusted), with the doorbell and
 *     interrupt-register offsets inside the BAR under
 *     ASSUME_DEVICE_HONEST_DOORBELL -- the assumption the base case makes,
 *     and finding F8 is that the driver does not check it.
 *   - DMA and heap allocation: each may fail, and on success returns a fresh
 *     object of the requested size.
 * The admin-command model replaces the real one by --replace-calls, so the
 * real ena_com code around it is what runs.
 */
#include "../drivers/net/ena/base/ena_com.c"
/* ena_inv.h is written against struct ena_ring, so it needs the driver header */
#include "../drivers/net/ena/ena_ethdev.h"
#include "ena_inv.h"

static int nd_int(void) { int v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

static uint8_t pool_reg_bar[4096];

/* ---------------- the device side of the admin queue ------------------- */
int model_execute_admin_command(
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
    return ENA_COM_INVAL;
  __CPROVER_havoc_slice(comp, comp_size);
#ifdef ASSUME_DEVICE_HONEST_DOORBELL
  if(comp_size == sizeof(struct ena_admin_acq_create_sq_resp_desc))
  {
    struct ena_admin_acq_create_sq_resp_desc *r = (void *)comp;
    __CPROVER_assume(r->sq_doorbell_offset < sizeof(pool_reg_bar) - 4);
  }
  if(comp_size == sizeof(struct ena_admin_acq_create_cq_resp_desc))
  {
    struct ena_admin_acq_create_cq_resp_desc *r = (void *)comp;
    __CPROVER_assume(
      r->cq_interrupt_unmask_register_offset < sizeof(pool_reg_bar) - 4);
    __CPROVER_assume(r->numa_node_register_offset < sizeof(pool_reg_bar) - 4);
  }
#endif
  return 0;
}

/* ---------------- allocation ------------------------------------------- */
const struct rte_memzone *ena_mem_alloc_coherent(
  struct rte_eth_dev_data *data,
  size_t size,
  int socket_id,
  unsigned int alignment,
  void **virt_addr,
  dma_addr_t *phys_addr)
{
  (void)data;
  (void)socket_id;
  (void)alignment;
  static struct rte_memzone mz;
  if(nd_int() || size == 0 || size > 65536)
  {
    *virt_addr = NULL;
    return NULL;
  }
  *virt_addr = malloc(size);
  __CPROVER_assume(*virt_addr != NULL);
  memset(*virt_addr, 0, size);
  *phys_addr = (dma_addr_t)nd_u32();
  return &mz;
}
void *rte_zmalloc(const char *type, size_t size, unsigned align)
{
  (void)type;
  (void)align;
  if(nd_int() || size == 0 || size > 65536)
    return NULL;
  void *p = malloc(size);
  __CPROVER_assume(p != NULL);
  memset(p, 0, size);
  return p;
}
void *rte_zmalloc_socket(const char *type, size_t size, unsigned align, int s)
{
  (void)s;
  return rte_zmalloc(type, size, align);
}
void rte_free(void *p) { free(p); }
int rte_memzone_free(const struct rte_memzone *mz) { (void)mz; return 0; }
int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}

int main(void)
{
  static struct ena_com_dev ena_dev;
  struct ena_com_create_io_ctx ctx;
  struct ena_com_io_sq *sq = NULL;
  struct ena_com_io_cq *cq = NULL;
  const uint16_t queue_size = RING;
  int rc;

  /* ---- device state from init (ena_com_config_dev_mode et al.) ---- */
  ena_dev.reg_bar = pool_reg_bar;
  /* Set at device init from ena_com_get_dma_width.  In DPDK that return value
   * is stored without checking for an error (F13), so a device that reports
   * a width outside 32..48 makes this the negative error code and every
   * later GENMASK_ULL(dma_addr_bits - 1, 0) an undefined shift.  The valid
   * range is stated here as a device-init precondition. */
#ifdef F13_DMA_WIDTH_ERROR
  /* What eth_ena_dev_init stores when ena_com_get_dma_width fails with
   * -EINVAL (width outside 32..48) or -ETIME (MMIO read timeout). */
  ena_dev.dma_addr_bits = (uint32_t)(F13_DMA_WIDTH_ERROR);
#else
  ena_dev.dma_addr_bits = nd_u16();
  __CPROVER_assume(ena_dev.dma_addr_bits >= 32 && ena_dev.dma_addr_bits <= 48);
#endif
  ena_dev.tx_max_header_size = 96;
  ena_dev.llq_info.descs_per_entry = nd_u16();
  ena_dev.llq_info.descs_num_before_header = nd_u16();
  ena_dev.llq_info.desc_list_entry_size = 128 << (nd_u16() % 3); /* 128/256/512 */
  ena_dev.llq_info.desc_stride_ctrl = ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY;
  ena_dev.llq_info.disable_meta_caching = false;
  /* INV_DEV_LLQ for the device-wide LLQ geometry the SQ copies */
  __CPROVER_assume(
    ena_dev.llq_info.descs_per_entry >= 1 &&
    ena_dev.llq_info.descs_per_entry <= 16 &&
    ena_dev.llq_info.descs_num_before_header <= 16);

  /* ---- the request exactly as ena_create_io_queue makes it for TX ---- */
  memset(&ctx, 0, sizeof(ctx));
  ctx.direction = ENA_COM_IO_QUEUE_DIRECTION_TX;
  ctx.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  ctx.queue_size = queue_size;
  ctx.qid = ENA_IO_TXQ_IDX(0);
  ctx.msix_vector = -1;
  ctx.numa_node = 0;

  rc = ena_com_create_io_queue(&ena_dev, &ctx);

#if VACUITY_CHECK
  __CPROVER_assert(rc != 0, "vacuity witness: create can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: create can fail");
#endif

  if(rc != 0)
    return 0;

  /* ---- ena_com_get_io_handlers, as ena_create_io_queue calls it ---- */
  int rc2 = ena_com_get_io_handlers(&ena_dev, ctx.qid, &sq, &cq);

  /* the get_io_handlers model: success, and the handles name the queues */
  __CPROVER_assert(
    rc2 == 0 && sq == &ena_dev.io_sq_queues[ctx.qid] &&
      cq == &ena_dev.io_cq_queues[ctx.qid],
    "model: get_io_handlers names the created queues");

  /* the create_io_queue model, post-condition by post-condition */
  __CPROVER_assert(
    sq->q_depth == queue_size && cq->q_depth == queue_size,
    "model POST 1: SQ and CQ sized by the request");
  __CPROVER_assert(
    (uint8_t *)sq->db_addr >= pool_reg_bar &&
      (uint8_t *)sq->db_addr + 4 <= pool_reg_bar + sizeof(pool_reg_bar),
    "model POST 2: doorbell inside the register BAR");
  __CPROVER_assert(
    sq->mem_queue_type == ctx.mem_queue_type,
    "model POST 2b: placement mode as requested");
  __CPROVER_assert(
    sq->tail == 0 && sq->next_to_comp == 0,
    "model POST 3: nothing in flight on a new queue");
  __CPROVER_assert(INV_DEV_LLQ(sq), "model POST 4: valid LLQ geometry");
  __CPROVER_assert(
    (uint8_t *)cq->unmask_reg >= pool_reg_bar &&
      (uint8_t *)cq->unmask_reg + 4 <= pool_reg_bar + sizeof(pool_reg_bar),
    "model POST 0a: CQ unmask register inside the BAR");
  __CPROVER_assert(
    cq->numa_node_cfg_reg == NULL ||
      ((uint8_t *)cq->numa_node_cfg_reg >= pool_reg_bar &&
       (uint8_t *)cq->numa_node_cfg_reg + 4 <=
         pool_reg_bar + sizeof(pool_reg_bar)),
    "model POST 0b: CQ NUMA register absent or inside the BAR");
  /* what INV_STARTED reads from the SQ, so the start base case's conclusion
   * really follows */
  __CPROVER_assert(
    INV_COM(sq) && sq->db_addr != NULL, "INV_COM holds on the created SQ");
  return 0;
}
