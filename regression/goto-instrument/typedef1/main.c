typedef long int off_t;
typedef signed char smallint;

typedef struct dumper_t dumper_t;
typedef struct FS FS;

dumper_t * alloc_dumper(void);

typedef struct dumper_t
{
  off_t dump_skip;
  signed int dump_length;
  smallint dump_vflag;
  FS *fshead;
} dumper_t;


typedef struct FS
{
  struct FS *nextfs;
  signed int bcnt;
} FS;

dumper_t * alloc_dumper(void)
{
  return (void*) 0;
}

int main() {
	alloc_dumper();
	return 0;
}
