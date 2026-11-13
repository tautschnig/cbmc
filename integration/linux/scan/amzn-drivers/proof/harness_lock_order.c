/*
 * Class E, deadlock and lockup: the admin-lock discipline.
 *
 * This is the one Unveil class with nothing proven against it (7 findings,
 * 3%).  A deadlock looks like a liveness property, but the useful part of it
 * is not: the states that cause a lockup here are reachable states, and
 * reachability is safety.  Three of them, with a ghost lock counter:
 *
 *   LKO_NO_RECURSION    the lock is never taken while already held.
 *                       rte_spinlock_t is not recursive, so this is an
 *                       immediate self-deadlock: the holder spins forever
 *                       waiting for itself.
 *   LKO_RELEASED        every entry point returns with the lock not held.
 *                       A lock held on return is a lockup of every later
 *                       admin operation, which is the whole control plane.
 *   LKO_NO_STRAY_UNLOCK no unlock without a matching lock, which would free
 *                       a lock another thread holds.
 *
 * The driver has exactly one lock, adapter->admin_lock, so there is no lock
 * ORDER to get wrong -- a cycle needs two.  What there is instead is nine
 * lock sites across seven entry points, several with error paths between the
 * lock and the unlock, and the hazard is a path that returns without
 * unlocking.  `ena_rss_reta_update` is the interesting one: it unlocks in two
 * places, once on an error inside its loop and once on the normal path.
 *
 * rte_spinlock_lock and rte_spinlock_unlock are static inlines, so the ghost
 * is interposed by macro AFTER <rte_spinlock.h> has been parsed and BEFORE
 * the driver sources are included; the driver's call sites then expand to the
 * ghost, and the header's own definitions are untouched.
 *
 * Scope, stated honestly: ENTRY=0 (ena_rss_reta_update) is proven clean and
 * mutation-validated.  ENTRY 1, 2 and 4 each reach an ena_com_* callee with
 * no body, which cuts the path, and ENTRY 3 returns before its lock-taking
 * loop so its witnesses pass -- i.e. it proves nothing yet.  Each needs a
 * model for the callee concerned; until then only ENTRY=0 is a result.
 *
 * -DENTRY=n selects which entry point to exercise.
 * -DBREAK_RETA_UNLOCK removes the error-path unlock in ena_rss_reta_update,
 *  to confirm the obligations would catch the bug they are there for.
 */
#include <rte_spinlock.h>

#include <stdbool.h>
#include <stdint.h>

static int nd_int(void) { int v; return v; }

/* ---- the ghost lock state ---- */
static int g_held;
static unsigned g_locks, g_unlocks;

static void ghost_lock(rte_spinlock_t *sl)
{
  (void)sl;
  __CPROVER_assert(
    g_held == 0,
    "LKO_NO_RECURSION: the lock is never taken while already held");
  g_held = 1;
  g_locks++;
}

static void ghost_unlock(rte_spinlock_t *sl)
{
  (void)sl;
#ifdef BREAK_RETA_UNLOCK
  /* Mutation test.  The driver sources cannot be edited here, so the bug is
   * injected by dropping the FIRST unlock, which is what an error path that
   * forgets to unlock would do.  LKO_RELEASED must catch it; an obligation
   * that cannot fail is not evidence of anything. */
  if(g_unlocks == 0)
  {
    g_unlocks++;
    return;
  }
#endif
  __CPROVER_assert(
    g_held == 1,
    "LKO_NO_STRAY_UNLOCK: the lock is only released by a holder");
  g_held = 0;
  g_unlocks++;
}

#define rte_spinlock_lock(sl) ghost_lock(sl)
#define rte_spinlock_unlock(sl) ghost_unlock(sl)

#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/ena_rss.c"

/* Without these the logging calls are bodyless, and a reached bodyless callee
 * cuts the path -- which would leave every obligation below vacuous. */
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

/* Models for the callees whose absence would cut the path.  Each returns an
 * arbitrary result, so both the success and failure branches of every caller
 * stay reachable -- the failure branches are where a missing unlock would
 * live, so cutting them would defeat the purpose. */


int ena_com_indirect_table_fill_entry(
  struct ena_com_dev *d, u16 i, u16 v)
{
  (void)d;
  (void)i;
  (void)v;
  return nd_int();
}
int ena_com_indirect_table_set(struct ena_com_dev *d)
{
  (void)d;
  return nd_int();
}
const char *rte_strerror(int e)
{
  (void)e;
  return "";
}
/* The multi-process path: this harness is the primary process, so the
 * request-reply round trip is not taken.  ena_mp_indirect_table_set is
 * modelled rather than executed, because what it does to the lock is
 * nothing -- it is called with the lock HELD, which is itself worth
 * recording as an observation rather than proving here. */
enum rte_proc_type_t rte_eal_process_type(void)
{
  return RTE_PROC_PRIMARY;
}
/* The multi-process round trip, modelled rather than removed: removing
 * ena_mp_indirect_table_set cut the path at exactly the point where the
 * normal-path unlock lives, leaving the obligations vacuous.  An arbitrary
 * return keeps both the success and failure branches reachable. */
int rte_mp_request_sync(
  struct rte_mp_msg *req, struct rte_mp_reply *reply,
  const struct timespec *ts)
{
  (void)req;
  (void)ts;
  reply->nb_received = 0;
  reply->nb_sent = 0;
  reply->msgs = NULL;
  return nd_int();
}
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }

#ifndef ENTRY
#  define ENTRY 0
#endif
#define NRETA 4

static uint8_t nd_u8(void) { uint8_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint64_t nd_u64(void) { uint64_t v; return v; }

static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct ena_adapter adapter;
static struct rte_eth_rss_reta_entry64 reta[NRETA];
static struct rte_eth_rss_conf rss_conf;
static uint8_t rss_key[ENA_HASH_KEY_SIZE];
static struct rte_eth_stats stats;

int main(void)
{
  dev.data = &data;
  data.dev_private = &adapter;
  adapter.edev_data = &data;
  data.nb_rx_queues = 2;
  data.nb_tx_queues = 2;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  /* ena_rss_reta_update and the hash entry points return -ENOTSUP before
   * taking the lock unless RSS was configured, so without this the lock is
   * never reached and every obligation holds vacuously.  The vacuity
   * witnesses caught exactly that. */
  data.dev_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_RSS_HASH;

  /* the lock as ena_com_init leaves it: initialised and free */
  rte_spinlock_init(&adapter.admin_lock);
  g_held = 0;
  g_locks = 0;
  g_unlocks = 0;

  uint16_t reta_size = nd_u16();
  __CPROVER_assume(reta_size <= NRETA * RTE_ETH_RETA_GROUP_SIZE);
  for(unsigned i = 0; i < NRETA; i++)
  {
    reta[i].mask = nd_u64();
    for(unsigned j = 0; j < RTE_ETH_RETA_GROUP_SIZE; j++)
      reta[i].reta[j] = nd_u16() % 2;
  }
  rss_conf.rss_key = rss_key;
  rss_conf.rss_key_len = ENA_HASH_KEY_SIZE;
  rss_conf.rss_hf = nd_u64();

  /* one arbitrary admin entry point, each of which takes the lock */
  int rc = 0;
  switch(ENTRY)
  {
  case 0: rc = ena_rss_reta_update(&dev, reta, reta_size); break;
  case 1: rc = ena_rss_reta_query(&dev, reta, reta_size); break;
  case 2: rc = ena_rss_hash_update(&dev, &rss_conf); break;
  case 3: rc = ena_rss_hash_conf_get(&dev, &rss_conf); break;
  case 4: rc = ena_stats_get(&dev, &stats); break;
  default: break;
  }
  (void)rc;

  /* The obligation that makes a lockup a safety property: whatever path was
   * taken, including every error path, the lock is free again on return. */
  __CPROVER_assert(
    g_held == 0,
    "LKO_RELEASED: the entry point returns with the admin lock released");

#ifdef VACUITY_CHECK
  /* Both must FAIL.  An entry point that never takes the lock satisfies all
   * three obligations trivially, so witness that the lock is really taken
   * and really released on the path explored. */
  __CPROVER_assert(g_locks == 0, "witness: the admin lock can be taken");
  __CPROVER_assert(g_unlocks == 0, "witness: the admin lock can be released");
#endif
  return 0;
}
