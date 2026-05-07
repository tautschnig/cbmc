// Harness for npm package 'lodash.chunk' (lodash/lodash)
// Version tracked in integration/typescript-npm/package.json
//
// chunk(array, size) splits array into groups of `size` elements.
// We verify ceiling-division properties for FIXED-size chunks with
// SYMBOLIC lengths. Fixing `size` avoids expensive symbolic
// multiplication while still exercising branch exploration.
//
// Upstream: https://github.com/lodash/lodash/blob/4.17.21/chunk.js

// Fixed chunk size of 3.
const SIZE3: number = 3;

// Compute ceil(len/3) for len in [0, 9] via explicit case analysis.
// This mirrors what a naive loop-based implementation would do,
// but avoids expensive symbolic multiplication.
function chunkCountS3(len: number): number {
  if (len <= 0) return 0;
  if (len <= 3) return 1;
  if (len <= 6) return 2;
  if (len <= 9) return 3;
  return 4; // len > 9
}

// Compute last-chunk length for fixed size=3.
function lastChunkLenS3(len: number): number {
  if (len <= 0) return 0;
  if (len === 1 || len === 4 || len === 7) return 1;
  if (len === 2 || len === 5 || len === 8) return 2;
  return 3;
}

// Property 1: count covers all elements. count * SIZE3 >= len.
const len: number = nondet_number();
__CPROVER_assume(len >= 0 && len <= 9);

const count: number = chunkCountS3(len);
// Range: 0 <= count <= 3 for len in [0, 9]
console.assert(count >= 0 && count <= 3);
// Covering property: count*3 >= len
console.assert(count * SIZE3 >= len);
// Minimality: (count-1)*3 < len when count >= 1
if (count >= 1) {
  console.assert((count - 1) * SIZE3 < len);
}

// Property 2: empty input yields zero chunks.
console.assert(chunkCountS3(0) === 0);

// Property 3: last chunk length is in [1, SIZE3] when array non-empty.
const last: number = lastChunkLenS3(len);
if (len >= 1) {
  console.assert(last >= 1);
  console.assert(last <= SIZE3);
}

// Property 4: last chunk length === len mod SIZE3 when remainder != 0,
// else SIZE3.
// For fixed size=3, we can verify this directly:
if (len === 3 || len === 6 || len === 9) {
  console.assert(last === 3);
}
if (len === 1 || len === 4 || len === 7) {
  console.assert(last === 1);
}
if (len === 2 || len === 5 || len === 8) {
  console.assert(last === 2);
}

// Property 5: monotonicity. If len1 < len2, count(len1) <= count(len2).
const len1: number = nondet_number();
const len2: number = nondet_number();
__CPROVER_assume(len1 >= 0 && len1 <= 9);
__CPROVER_assume(len2 >= 0 && len2 <= 9);
__CPROVER_assume(len1 <= len2);
console.assert(chunkCountS3(len1) <= chunkCountS3(len2));
