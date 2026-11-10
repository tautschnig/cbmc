/*
 * Phase 7: DMA into freed memory (Unveil class A, 11 findings / 6%).
 *
 * The obligation is an ordering one: a region the device may still write to
 * must not be freed.  For an ENA IO queue the regions are the SQ descriptor
 * ring and the CQ completion ring, both ENA_MEM_ALLOC_COHERENT'd by
 * ena_com_create_io_queue, and the device is told to stop using them by the
 * destroy-SQ and destroy-CQ admin commands.  The correct order is therefore
 *
 *     ena_com_destroy_io_sq   (admin command: device releases the SQ)
 *     ena_com_destroy_io_cq   (admin command: device releases the CQ)
 *     ena_com_io_queue_free   (now the memory may be freed)
 *
 * which is what ena_com_destroy_io_queue does (ena_com.c:2237-2240).  This
 * harness checks it rather than reading it, on the real ena_com code, with a
 * ghost set recording which regions the device may still access.
 *
 * Obligations:
 *   DMA_NO_FREE_WHILE_LIVE  no region is freed while the device may still
 *                           access it
 *   DMA_ALL_RELEASED        after the teardown the device may access nothing
 *   DMA_NO_DOUBLE_FREE      no region is freed twice
 *
 * The device is untrusted: either destroy admin command may fail, which is
 * the interesting case, because the driver frees the memory regardless.
 * Whether that is correct is exactly what DMA_NO_FREE_WHILE_LIVE decides.
 *
 * Switches: VACUITY_CHECK, ADMIN_FAILS.
 */
#include "../dpdk/drivers/net/ena/base/ena_plat_dpdk.h"

/* The ghost set, declared before ena_com.c so the memory macros can update
 * it.  Three regions per queue: SQ ring, CQ ring, bounce buffers. */
#define R_SQ 0
#define R_CQ 1
#define R_BOUNCE 2
#define NREG 3
static bool dev_may_access[NREG];
static bool freed[NREG];
static unsigned g_double_frees;
static unsigned g_free_while_live;
static void *reg_addr[NREG];

static int reg_of(const void *p)
{
  for(int i = 0; i < NREG; i++)
    if(p != NULL && p == reg_addr[i])
      return i;
  return -1;
}

static void ghost_free(void *p)
{
  const int i = reg_of(p);
  if(i < 0)
    return;
  if(dev_may_access[i])
    g_free_while_live++;
  if(freed[i])
    g_double_frees++;
  freed[i] = true;
}

/* Replace the platform memory macros with ones that maintain the ghost set.
 * The addresses handed out are static pools, so no real allocator is needed. */
static uint8_t pool_sq[4096], pool_cq[4096], pool_bounce[4096];

#undef ENA_MEM_ALLOC_COHERENT_ALIGNED
#define ENA_MEM_ALLOC_COHERENT_ALIGNED(dmadev, size, virt, phys, handle, al)   \
  do                                                                           \
  {                                                                            \
    (void)(dmadev);                                                            \
    (void)(al);                                                                \
    (virt) = ghost_alloc_coherent((size));                                     \
    (phys) = (ena_mem_handle_t)0x200000;                                       \
    (void)(handle);                                                            \
  } while(0)
#undef ENA_MEM_ALLOC_COHERENT
#define ENA_MEM_ALLOC_COHERENT(dmadev, size, virt, phys, handle)                \
  ENA_MEM_ALLOC_COHERENT_ALIGNED(dmadev, size, virt, phys, handle, 0)
#undef ENA_MEM_FREE_COHERENT
#define ENA_MEM_FREE_COHERENT(dmadev, size, virt, phys, handle)                 \
  do                                                                           \
  {                                                                            \
    (void)(dmadev);                                                            \
    (void)(size);                                                              \
    (void)(phys);                                                              \
    (void)(handle);                                                            \
    ghost_free((virt));                                                        \
  } while(0)
#undef ENA_MEM_ALLOC
#define ENA_MEM_ALLOC(dmadev, size) ghost_alloc_plain((size))
#undef ENA_MEM_FREE
#define ENA_MEM_FREE(dmadev, ptr, size)                                        \
  do                                                                          \
  {                                                                           \
    (void)(dmadev);                                                           \
    (void)(size);                                                             \
    ghost_free((ptr));                                                        \
  } while(0)

static unsigned coherent_handed_out;
static void *ghost_alloc_coherent(size_t size)
{
  (void)size;
  /* the SQ ring is requested first, then the CQ ring */
  if(coherent_handed_out++ == 0)
    return pool_sq;
  return pool_cq;
}
static void *ghost_alloc_plain(size_t size)
{
  (void)size;
  return pool_bounce;
}

#include "../dpdk/drivers/net/ena/base/ena_com.c"

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

static struct ena_com_dev ena_dev;

int ena_logtype_com = 0;
int rte_log(uint32_t l, uint32_t t, const char *f, ...)
{
  (void)l;
  (void)t;
  (void)f;
  return 0;
}

/* The two destroy admin commands.  Each tells the device to stop using one
 * region; on success the device may no longer access it.  Either may fail,
 * and the driver frees the memory regardless -- which is the case
 * DMA_NO_FREE_WHILE_LIVE is about.  The real ones arrive with ena_com.c, so
 * the runner redirects calls here with --replace-calls. */
int model_destroy_io_sq(struct ena_com_dev *dev, struct ena_com_io_sq *io_sq)
{
  (void)dev;
  (void)io_sq;
#ifdef ADMIN_FAILS
  if(nd_int())
    return ENA_COM_TIMER_EXPIRED; /* the device did not acknowledge */
#endif
  dev_may_access[R_SQ] = false;
  return 0;
}
int model_destroy_io_cq(struct ena_com_dev *dev, struct ena_com_io_cq *io_cq)
{
  (void)dev;
  (void)io_cq;
#ifdef ADMIN_FAILS
  if(nd_int())
    return ENA_COM_TIMER_EXPIRED;
#endif
  dev_may_access[R_CQ] = false;
  return 0;
}

int main(void)
{
  struct ena_com_io_sq *sq = &ena_dev.io_sq_queues[0];
  struct ena_com_io_cq *cq = &ena_dev.io_cq_queues[0];

  /* a created queue: both rings allocated and handed to the device, and the
   * bounce buffers allocated for LLQ mode */
  reg_addr[R_SQ] = pool_sq;
  reg_addr[R_CQ] = pool_cq;
  reg_addr[R_BOUNCE] = pool_bounce;
  for(int i = 0; i < NREG; i++)
  {
    dev_may_access[i] = true;
    freed[i] = false;
  }
  /* the bounce buffers are driver-private: the device reads the LLQ BAR, not
   * this memory, so it is not in the device's access set */
  dev_may_access[R_BOUNCE] = false;

  sq->qid = 0;
  sq->q_depth = 16;
  sq->desc_entry_size = 16;
  sq->desc_addr.virt_addr = (u8 *)pool_sq;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq->llq_info.desc_list_entry_size = 128;
  sq->bounce_buf_ctrl.base_buffer = pool_bounce;
  cq->qid = 0;
  cq->q_depth = 16;
  cq->cdesc_entry_size_in_bytes = 16;
  cq->cdesc_addr.virt_addr = (u8 *)pool_cq;

  g_double_frees = 0;
  g_free_while_live = 0;

  /* ---- the teardown ---- */
  ena_com_destroy_io_queue(&ena_dev, 0);

  /* ---- obligations ---- */
  __CPROVER_assert(
    g_free_while_live == 0,
    "DMA_NO_FREE_WHILE_LIVE: no region is freed while the device may still "
    "access it");
  __CPROVER_assert(
    !dev_may_access[R_SQ] && !dev_may_access[R_CQ],
    "DMA_ALL_RELEASED: after the teardown the device may access nothing");
  __CPROVER_assert(
    g_double_frees == 0, "DMA_NO_DOUBLE_FREE: no region is freed twice");

#ifdef VACUITY_CHECK
  /* must FAIL: the teardown really does free the rings */
  __CPROVER_assert(
    !freed[R_SQ] || !freed[R_CQ],
    "vacuity witness: the teardown frees both rings");
#endif
  return 0;
}
