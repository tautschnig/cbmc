// A const-qualified struct of static lifetime is kept whole by field
// sensitivity (no symbol per field), and its constant value is still
// propagated: reads of its fields simplify to constants.
#include <assert.h>

struct config
{
  int width;
  int height;
  const char *name;
};

static const struct config cfg = {640, 480, "vga"};
static struct config mutable_cfg = {640, 480, "vga"};

int main(void)
{
  int area = cfg.width * cfg.height;
  assert(area == 307200);
  assert(cfg.name[1] == 'g');
  assert(mutable_cfg.height == 480);
  return 0;
}
