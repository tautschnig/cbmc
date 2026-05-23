// Coccinelle instrumentation rule for copy_from_user_size_check.
//
// Inserts __assert_copy_safe(sizeof(*dst), len) just before
// each copy_from_user(dst, src, len) call.

@@
expression dst, src, len;
@@

+ __assert_copy_safe(sizeof(*dst), len);
copy_from_user(dst, src, len)
