/*
 * Stage 3: the BASE CASE of the induction -- ena_tx_queue_setup establishes
 * the configured-state invariant.  Together with the step (harness_tx*.c)
 * this closes the induction over operation sequences.
 *
 * The lifecycle splits the invariant, matching the API automaton:
 *   INV_CONFIGURED  established by ena_tx_queue_setup  (this harness)
 *   INV_STARTED     = INV_CONFIGURED + the ena_com io_sq clauses,
 *                     established by ena_queue_start   (next increment)
 * The data-path step proof assumes INV_STARTED.
 *
 * ring_size stays SYMBOLIC: the driver's `empty_tx_reqs[i] = i` loop is
 * discharged by a quantified loop invariant (setup_loop_contract.json), not by
 * unrolling, so the base case holds for every admissible ring size.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

/* An arbitrary slot index, fixed before the call and referenced by BOTH the
 * loop invariant and the post-condition.  Proving the single instance for a
 * nondeterministic index is universal quantification over the index, and it
 * avoids __CPROVER_forall, which CBMC currently ignores in a loop-contracts
 * file ("warning: ignoring forall"). */
uint16_t g_probe_idx;

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }

/*
 * The DPDK allocator.  It hands out slices of static pools rather than
 * malloc'd memory: empty_tx_reqs and tx_buffer_info are held in anonymous
 * unions, and CBMC cannot resolve a union-held pointer to a dynamic object
 * (the access becomes byte_extract_little_endian(txq->$anon0, ...) and the
 * property status comes back ERROR rather than SUCCESS/FAILURE).  Pointing at
 * known static objects keeps the sizes exact for the geometry under proof.
 */
#define RING (CONCRETE_RING)
#define PUSH_BUF_SIZE 96
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[PUSH_BUF_SIZE];
static bool alloc_tbi_fail, alloc_etr_fail, alloc_push_fail;

void *rte_zmalloc_socket(const char *type, size_t size, unsigned align, int socket)
{
	(void)type; (void)align; (void)socket;
	/* every allocation may fail: the error paths are part of the proof */
	if (size == sizeof(pool_tbi)) {
		if (alloc_tbi_fail)
			return NULL;
		memset(pool_tbi, 0, sizeof(pool_tbi));
		return pool_tbi;
	}
	if (size == sizeof(pool_etr)) {
		if (alloc_etr_fail)
			return NULL;
		memset(pool_etr, 0, sizeof(pool_etr));
		return pool_etr;
	}
	if (size == PUSH_BUF_SIZE) {
		if (alloc_push_fail)
			return NULL;
		memset(pool_push, 0, PUSH_BUF_SIZE);
		return pool_push;
	}
	return NULL;
}
/* a no-op: the allocator above hands out static pools, so the error paths of
 * ena_tx_queue_setup must not call free() on them */
void rte_free(void *p) { (void)p; }

/* the PMD registration constructor (RTE_PMD_REGISTER_PCI) runs before main */
struct rte_pci_driver;
void rte_pci_register(struct rte_pci_driver *driver) { (void)driver; }

/* DPDK logging.  rte_log is variadic and extern; left bodyless it is reported
 * as "undefined function should be unreachable" with status ERROR under DFCC,
 * which also leaves the loop-contract obligations undecided. */
int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
	(void)level; (void)logtype; (void)format;
	return 0;
}
int rte_log_register_type_and_pick_level(const char *name, uint32_t level_def)
{
	(void)name; (void)level_def;
	return 0;
}

/* What ena_tx_queue_setup must establish.  `probe_idx` is an arbitrary index,
 * so asserting the per-slot clauses for it proves them for ALL slots. */
static bool INV_CONFIGURED_SLOTS(const struct ena_ring *r, uint16_t probe_idx)
{
	bool ok = r->configured == 1 &&
		  r->tx_buffer_info != NULL &&
		  r->empty_tx_reqs != NULL &&
		  r->push_buf_intermediate_buf != NULL &&
		  r->size_mask == r->ring_size - 1 &&
		  rte_is_power_of_2(r->ring_size) &&
		  r->next_to_use == 0 && r->next_to_clean == 0 &&
		  r->pkts_without_db == false;
	if (probe_idx < r->ring_size) {
		/* empty_tx_reqs and tx_buffer_info live in anonymous unions
		 * (ena_ethdev.h:158-167).  Under DFCC, dereferencing the union
		 * member directly yields a byte_extract that CBMC's pointer
		 * analysis cannot resolve (property status ERROR), so copy the
		 * pointer into a local first. */
		uint16_t *etr = r->empty_tx_reqs;
		struct ena_tx_buffer *tbi = r->tx_buffer_info;

		/* the req_id indirection the step proof depends on */
		ok = ok && etr[probe_idx] < r->ring_size;
		/* rte_zmalloc_socket zeroes, so every slot starts free */
		ok = ok && tbi[probe_idx].mbuf == NULL &&
		     tbi[probe_idx].num_of_bufs == 0;
	}
	return ok;
}

int main(void)
{
	static struct rte_eth_dev dev;
	static struct rte_eth_dev_data data;
	static struct ena_adapter adapter;
	static struct rte_eth_txconf tx_conf;
	uint16_t nb_desc;
	int rc;

	__CPROVER_havoc_object(&dev);
	__CPROVER_havoc_object(&data);
	__CPROVER_havoc_object(&adapter);
	__CPROVER_havoc_object(&tx_conf);
	dev.data = &data;
	data.dev_private = &adapter;
	/* ethdev owns the tx_queues array; ena_tx_queue_setup publishes the
	 * ring into it (dev->data->tx_queues[queue_idx] = txq) */
	static void *tx_queues[1];
	data.tx_queues = tx_queues;
	data.nb_tx_queues = 1;

	/* the ethdev contract: rte_eth_dev_tx_queue_setup has already checked
	 * nb_desc against the nb_min/nb_max this driver advertises */
	alloc_tbi_fail = nd_u16() & 1;
	alloc_etr_fail = nd_u16() & 1;
	alloc_push_fail = nd_u16() & 1;
	g_probe_idx = nd_u16();
	nb_desc = nd_u16();
	__CPROVER_assume(rte_is_power_of_2(nb_desc));
#ifdef CONCRETE_RING
	/* DFCC cannot model writes through a pointer held in an anonymous
	 * union (struct ena_ring puts empty_tx_reqs/tx_buffer_info in two),
	 * so the loop-invariant route is unavailable here and the init loop is
	 * unrolled instead.  ENA_MIN_RING_DESC is the smallest ring the driver
	 * advertises, i.e. the most constrained geometry. */
	__CPROVER_assume(nb_desc == CONCRETE_RING);
#else
	__CPROVER_assume(nb_desc >= ENA_MIN_RING_DESC);
#endif
	adapter.max_tx_ring_size = nd_u16();
	__CPROVER_assume(nb_desc <= adapter.max_tx_ring_size);

	/* queue 0, not yet configured.
	 *
	 * Device init (ena_init_rings, ena_ethdev.c:2529) has already set
	 * sgl_size from adapter->max_tx_sgl_size, which ena_calc_io_queue_size
	 * clamps with RTE_MIN(ENA_PKT_MAX_BUFS, device value).  Setup does not
	 * touch it, so its bound is a device-init precondition of this base
	 * case, stated here rather than proven: the same status as INV_DEV_LLQ. */
	adapter.tx_ring[0].configured = 0;
	__CPROVER_assume(adapter.tx_ring[0].sgl_size <= ENA_PKT_MAX_BUFS);
	adapter.tx_ring[0].tx_max_header_size = 96;

	rc = ena_tx_queue_setup(&dev, 0, nb_desc, 0, &tx_conf);

#if VACUITY_CHECK
	/* both must FAIL, or one outcome is unreachable and its obligation vacuous */
	__CPROVER_assert(rc != 0, "vacuity witness: a setup can succeed");
	__CPROVER_assert(rc == 0, "vacuity witness: a setup can fail");
#endif
	if (rc == 0)
	{
		/* the predicate the next base case (queue start) assumes, from
		 * ena_inv.h, plus the slot clauses at an arbitrary index */
		__CPROVER_assert(INV_CONFIGURED(&adapter.tx_ring[0]) &&
				 INV_CONFIGURED_SLOTS(&adapter.tx_ring[0], g_probe_idx),
				 "base case: ena_tx_queue_setup establishes INV_CONFIGURED");
#ifdef CONCRETE_RING
		__CPROVER_assert(INV_STARTABLE(&adapter.tx_ring[0], CONCRETE_RING),
				 "base case: ena_tx_queue_setup establishes INV_STARTABLE");
#endif
	}
	return 0;
}
