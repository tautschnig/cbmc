// Synthetic fixture for the "page-cache write via in-place crypto over
// user-iov-extracted pages" oracle (pagecache_write_inplace.ql).
//
// Inspired by the copyfail LPE class (the CVE this whole effort started
// from): splice/vmsplice can deliver references to READ-ONLY page-cache
// pages (e.g. a setuid binary) into a crypto request's scatterlist; an
// IN-PLACE cipher operation then WRITES the result back into those pages,
// overwriting the on-disk-backed file content in the page cache.
//
// The dangerous shape: pages are obtained from a user iov_iter (a
// provenance that may alias read-only page-cache pages), then the SAME
// scatterlist is used as both the source and the destination of a crypto
// write, with no copy into a private buffer / writability check.

struct scatterlist;
struct iov_iter;
struct aead_request;
struct sg_table
{
  struct scatterlist *sgl;
};

extern long extract_iter_to_sg(struct iov_iter *iter, unsigned long maxsize,
                               struct sg_table *sgt, unsigned int sg_max,
                               int extraction_flags);
extern void aead_request_set_crypt(struct aead_request *req,
                                   struct scatterlist *src,
                                   struct scatterlist *dst, unsigned int len,
                                   void *iv);
// a "sanitizer": copy the extracted data into a freshly-allocated private
// kernel scatterlist before the crypto write
extern void sg_copy_to_private(struct sg_table *dst, struct sg_table *src);

// (1) BUGGY: extracted user-iov pages used in-place (src == dst) as the
// crypto destination -> writes into possibly read-only page-cache pages.
int aead_inplace_buggy(struct iov_iter *iter, struct aead_request *req,
                       struct sg_table *t, void *iv)
{
  extract_iter_to_sg(iter, 4096, t, 8, 0);
  aead_request_set_crypt(req, t->sgl, t->sgl, 4096, iv); // in-place WRITE
  return 0;
}

// (1') FIXED: copy into a private destination sglist first; the crypto
// write lands in `priv`, never in the user/page-cache pages.
int aead_inplace_fixed(struct iov_iter *iter, struct aead_request *req,
                       struct sg_table *src, struct sg_table *priv, void *iv)
{
  extract_iter_to_sg(iter, 4096, src, 8, 0);
  sg_copy_to_private(priv, src);
  aead_request_set_crypt(req, src->sgl, priv->sgl, 4096, iv); // src != dst
  return 0;
}

// (2) NEGATIVE control: in-place crypto, but the source is NOT from a user
// iov (a kernel-private buffer) -> not a page-cache-write primitive.
extern void alloc_kernel_sg(struct sg_table *t);
int aead_inplace_kernel_buf(struct aead_request *req, struct sg_table *t,
                            void *iv)
{
  alloc_kernel_sg(t);
  aead_request_set_crypt(req, t->sgl, t->sgl, 4096, iv); // in-place, but safe
  return 0;
}
