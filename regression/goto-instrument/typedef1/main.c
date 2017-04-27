typedef long int off_t;
typedef signed char smallint;

typedef struct chain_s {
    struct node_s *first;
    struct node_s *last;
    const char *programname;
} chain;

typedef struct node_s { 
    struct node_s *n;
} node;

typedef struct dumper_t_x
{
  node n; //chaning this line to 'struct node_s n' removes some bugs
  off_t dump_skip;
  signed int dump_length;
  smallint dump_vflag;
} dumper_t;

typedef struct FS_x
{
  struct FS *nextfs;
  signed int bcnt;
} FS;


dumper_t * alloc_dumper(void)
{
  return (void*) 0;
}

int main() {
    node n;
    chain c;
    dumper_t a;
    dumper_t b[3];
	alloc_dumper();
	return 0;
}
