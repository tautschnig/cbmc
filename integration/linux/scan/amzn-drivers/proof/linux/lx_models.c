// Models of the kernel and device boundary for the Linux ENA harnesses.
//
// Each model states the contract of the function it replaces and nothing
// more.  Allocation can fail; freeing a pointer the model did not hand out,
// or freeing twice, is an assertion failure.  The device answers every admin
// command with an arbitrary completion and an arbitrary status from the set
// ena_com_comp_status_to_errno can produce (ena_com.c), so every device
// refusal the driver might see is explored.
//
// Included into a harness translation unit (not linked separately), so the
// kernel headers the harness already includes are in scope.

#ifndef LX_MODELS_C
#define LX_MODELS_C

#define LX_MAX_ALLOCS 16

int nondet_int(void);
unsigned int nondet_uint(void);
_Bool nondet_bool(void);
void *malloc(__kernel_size_t size);
void free(void *p);

// ---- allocation ledger ----------------------------------------------------

static void *lx_live[LX_MAX_ALLOCS];
static int lx_nlive;

static void *lx_alloc(__kernel_size_t size)
{
  void *p;
  if(nondet_bool())
    return NULL; // every allocation can fail
  p = malloc(size);
  __CPROVER_assume(p != NULL);
  __CPROVER_assume(lx_nlive < LX_MAX_ALLOCS);
  lx_live[lx_nlive++] = p;
  return p;
}

static void lx_release(const void *p, const char *what)
{
  int i;
  for(i = 0; i < LX_MAX_ALLOCS; i++)
  {
    if(i < lx_nlive && lx_live[i] == p)
    {
      lx_live[i] = lx_live[--lx_nlive];
      free((void *)p);
      return;
    }
  }
  __CPROVER_assert(0, "LX_FREE_KNOWN: only a live allocation is released");
  (void)what;
}

void *dma_alloc_attrs(
  struct device *dev,
  size_t size,
  dma_addr_t *dma_handle,
  gfp_t flag,
  unsigned long attrs)
{
  void *p = lx_alloc(size);
  (void)dev;
  (void)flag;
  (void)attrs;
  if(p)
  {
    __CPROVER_array_set((char *)p, 0); // callers rely on zeroed memory
    *dma_handle = (dma_addr_t)nondet_uint();
  }
  return p;
}

void dma_free_attrs(
  struct device *dev,
  size_t size,
  void *cpu_addr,
  dma_addr_t dma_handle,
  unsigned long attrs)
{
  (void)dev;
  (void)size;
  (void)dma_handle;
  (void)attrs;
  lx_release(cpu_addr, "dma_free_attrs");
}

void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
  void *p = lx_alloc(size);
  (void)dev;
  if(p && (gfp & __GFP_ZERO))
    __CPROVER_array_set((char *)p, 0);
  return p;
}

void devm_kfree(struct device *dev, const void *p)
{
  (void)dev;
  lx_release(p, "devm_kfree");
}

// ---- device: the admin queue ----------------------------------------------

int ena_com_execute_admin_command(
  struct ena_com_admin_queue *admin_queue,
  struct ena_admin_aq_entry *cmd,
  size_t cmd_size,
  struct ena_admin_acq_entry *comp,
  size_t comp_size)
{
  int rc = nondet_int();
  (void)admin_queue;
  (void)cmd;
  (void)cmd_size;
  // the completion is whatever the device wrote
  __CPROVER_havoc_slice(comp, comp_size);
#ifdef LX_ADMIN_HOOK
  LX_ADMIN_HOOK(cmd, comp); // harness-specific bound on the answer
#endif
  __CPROVER_assume(
    rc == 0 || rc == -ENOMEM || rc == -EOPNOTSUPP || rc == -EINVAL ||
    rc == -EPERM || rc == -ETIME || rc == -ENODEV || rc == -EFAULT);
  return rc;
}

#endif
