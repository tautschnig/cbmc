/* As union_pts.c, but the ring is reached the way the driver reaches it: a
 * struct adapter holding an ARRAY of rings, published through a void *
 * (dev->data->dev_private) and read back with a cast. */
#ifndef N_RINGS
#define N_RINGS 65
#endif
static unsigned short etr[4];
static int tbi[4];
static unsigned char push[8];

struct ring {
	unsigned short *empty_tx_reqs;
	int *tx_buffer_info;
	unsigned char *push_buf;
	int type;
};
struct adapter { long pad[16]; struct ring tx_ring[N_RINGS]; };
static struct adapter adp;
static void *dev_private;

static void create_io_queue(struct ring *ring)
{
	for(unsigned i = 0; i < 4; i++)
		ring->empty_tx_reqs[i] = (unsigned short)i;
}

int main(void)
{
	adp.tx_ring[0].empty_tx_reqs = etr;
	adp.tx_ring[0].tx_buffer_info = tbi;
	adp.tx_ring[0].push_buf = push;
	adp.tx_ring[0].type = 1;
	dev_private = &adp;

	struct adapter *a = (struct adapter *)dev_private;
	create_io_queue(&a->tx_ring[0]);

	__CPROVER_assert(adp.tx_ring[0].type == 1, "type survives the free-list init");
	return 0;
}
