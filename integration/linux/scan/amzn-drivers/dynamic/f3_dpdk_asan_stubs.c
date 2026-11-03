/* Minimal stubs: the harness only calls ena_com_get_hash_key(), which does a
 * single memcpy.  These satisfy the linker for the rest of ena_com.c. */
#include <sys/time.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
int ena_logtype_com;
void rte_delay_us_block(unsigned u)
{
  (void)u;
}
void rte_delay_us_sleep(unsigned u)
{
  (void)u;
}
void *rte_zmalloc(const char *n, size_t s, unsigned a)
{
  (void)n;
  (void)a;
  return calloc(1, s);
}
void *rte_zmalloc_socket(const char *n, size_t s, unsigned a, int sock)
{
  (void)n;
  (void)a;
  (void)sock;
  return calloc(1, s);
}
void rte_free(void *p)
{
  free(p);
}
void rte_memzone_free(const void *mz)
{
  (void)mz;
}
uint64_t rte_get_tsc_hz(void)
{
  return 1000000000ULL;
}
int rte_log(uint32_t level, uint32_t logtype, const char *fmt, ...)
{
  (void)level;
  (void)logtype;
  (void)fmt;
  return 0;
}
void ena_mem_alloc_coherent(void *a, size_t b, int c, void *d, void *e, void *f)
{
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  (void)e;
  (void)f;
}
void ena_rss_key_fill(void *key, size_t size)
{
  (void)key;
  (void)size;
}
