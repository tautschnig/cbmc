// ES2024 §22.1.3.23: startsWith on a symbolic receiver with a
// constrained content is now content-precise (via the refined-
// string solver). A single solver call per receiver works; tests
// with multiple solver calls on the same receiver remain KNOWNBUG
// (see string-startswith-symbolic).
const s: string = nondet_string();
__CPROVER_assume(s === "hello world");
console.assert(s.startsWith("hello"));
