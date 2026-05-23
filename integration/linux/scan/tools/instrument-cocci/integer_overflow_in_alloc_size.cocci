// Coccinelle instrumentation rule for integer_overflow_in_alloc_size.
//
// Inserts __assert_size_safe(n, sizeof(*x)) just before each
// kmalloc(n * sizeof(*x), ...) call.

@@
expression n, flags;
type T;
@@

+ __assert_size_safe(n, sizeof(T));
\( kmalloc(n * sizeof(T), flags)
\| kzalloc(n * sizeof(T), flags)
\)
