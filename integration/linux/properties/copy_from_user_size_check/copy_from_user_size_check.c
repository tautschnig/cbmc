/// \file
/// copy_from_user_size_check.c — reference impl.

#include "copy_from_user_size_check.h"

int copy_len_safe(size_t dst_capacity, size_t len)
{
  return len <= dst_capacity ? 1 : 0;
}
