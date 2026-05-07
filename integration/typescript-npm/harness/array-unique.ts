// Harness for npm package 'array-unique' (jonschlinkert/array-unique)
// Version tracked in integration/typescript-npm/package.json
//
// unique(arr) returns a new array with duplicates removed, preserving
// first-occurrence order. We verify key invariants with concrete
// scenarios and small symbolic inputs — deep symbolic arrays with
// loops are expensive for SAT to handle.
//
// Upstream: https://github.com/jonschlinkert/array-unique/blob/0.3.2/index.js

// Fixed-size reimplementation for arrays of length 3.
// Returns a length value and the (unique) elements.
class Uniq3 {
  len: number;
  v0: number;
  v1: number;
  v2: number;
  constructor(a: number, b: number, c: number) {
    this.len = 0;
    this.v0 = 0; this.v1 = 0; this.v2 = 0;
    // First: always kept
    this.v0 = a;
    this.len = 1;
    // Second: keep if not equal to first
    if (b !== a) {
      this.v1 = b;
      this.len = 2;
    }
    // Third: keep if not equal to any kept
    if (c !== a && (this.len === 1 || c !== this.v1)) {
      if (this.len === 1) this.v1 = c;
      else this.v2 = c;
      this.len = this.len + 1;
    }
  }
}

// Property 1: all-distinct input produces length 3.
const u1 = new Uniq3(1, 2, 3);
console.assert(u1.len === 3);
console.assert(u1.v0 === 1);
console.assert(u1.v1 === 2);
console.assert(u1.v2 === 3);

// Property 2: all-same input produces length 1.
const u2 = new Uniq3(5, 5, 5);
console.assert(u2.len === 1);
console.assert(u2.v0 === 5);

// Property 3: two distinct produces length 2.
const u3 = new Uniq3(1, 1, 2);
console.assert(u3.len === 2);
console.assert(u3.v0 === 1);
console.assert(u3.v1 === 2);

const u4 = new Uniq3(1, 2, 2);
console.assert(u4.len === 2);
console.assert(u4.v0 === 1);
console.assert(u4.v1 === 2);

// Property 4: result length is at most input length, at least 1 (non-empty input).
const a: number = nondet_number();
const b: number = nondet_number();
const c: number = nondet_number();
__CPROVER_assume(!Number.isNaN(a) && !Number.isNaN(b) && !Number.isNaN(c));
__CPROVER_assume(a >= 0 && a <= 2 && b >= 0 && b <= 2 && c >= 0 && c <= 2);
const u5 = new Uniq3(a, b, c);
console.assert(u5.len >= 1 && u5.len <= 3);

// Property 5: no duplicates in output.
if (u5.len === 2) {
  console.assert(u5.v0 !== u5.v1);
}
if (u5.len === 3) {
  console.assert(u5.v0 !== u5.v1);
  console.assert(u5.v0 !== u5.v2);
  console.assert(u5.v1 !== u5.v2);
}

// Property 6: idempotence. unique(unique(x)) === unique(x).
// With 3 distinct inputs, calling Uniq3 on the unique result is a no-op.
const u6 = new Uniq3(u1.v0, u1.v1, u1.v2);
console.assert(u6.len === u1.len);
console.assert(u6.v0 === u1.v0);
console.assert(u6.v1 === u1.v1);
console.assert(u6.v2 === u1.v2);
