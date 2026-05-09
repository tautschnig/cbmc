// ES2024 §22.1.3.17: String.prototype.padStart with symbolic n.
// We now emit length = max(src.length, n).
const n: number = nondet_number();
__CPROVER_assume(n >= 5 && n <= 10);
const s: string = "x".padStart(n, "0");
console.assert(s.length === n);

// padEnd also works symbolically
const m: number = nondet_number();
__CPROVER_assume(m >= 3 && m <= 8);
const s2: string = "ab".padEnd(m, "-");
console.assert(s2.length === m);

// n smaller than source: keep source length
const k: number = nondet_number();
__CPROVER_assume(k >= 0 && k <= 2);
const s3: string = "hello".padStart(k, "0");
console.assert(s3.length === 5);
