// Harness for npm package 'ms' (vercel/ms)
// Version tracked in integration/typescript-npm/package.json
//
// Verifies properties of the ms package's classification logic over
// SYMBOLIC (nondet) inputs. If the harness's logic or our TypeScript
// frontend has a bug in arithmetic / comparisons, these properties
// will fail under symbolic analysis.
//
// Upstream: https://github.com/vercel/ms/blob/v2.1.3/index.js

const S: number = 1000;
const M: number = S * 60;
const H: number = M * 60;
const D: number = H * 24;

function formatShort(ms: number): string {
  if (ms >= D) return "d";
  if (ms >= H) return "h";
  if (ms >= M) return "m";
  if (ms >= S) return "s";
  return "ms";
}

// Property: for any symbolic ms in a bounded non-negative range,
// the classification is consistent with the unit boundaries.
// If the frontend miscompiles >= or if constants are wrong, this fails.
const ms: number = nondet_number();
__CPROVER_assume(ms >= 0 && ms < D * 2); // 2 days worth

const cls: string = formatShort(ms);

// If classified as "ms", ms must be < 1000
if (cls === "ms") {
  console.assert(ms < S);
}
// If classified as "s", ms must be in [1000, 60000)
if (cls === "s") {
  console.assert(ms >= S);
  console.assert(ms < M);
}
// If classified as "m", ms must be in [60000, 3600000)
if (cls === "m") {
  console.assert(ms >= M);
  console.assert(ms < H);
}
// If classified as "h", ms must be in [3600000, 86400000)
if (cls === "h") {
  console.assert(ms >= H);
  console.assert(ms < D);
}
// If classified as "d", ms must be >= 86400000
if (cls === "d") {
  console.assert(ms >= D);
}

// Property: the classification is total — ms is ALWAYS one of the 5 categories.
console.assert(
  cls === "ms" || cls === "s" || cls === "m" || cls === "h" || cls === "d"
);
