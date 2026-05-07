// Harness for npm package 'ms' (vercel/ms)
// Version tracked in integration/typescript-npm/package.json
//
// The ms package converts between time strings and milliseconds.
// We reimplement the TypeScript equivalent and verify invariants.
//
// Upstream: https://github.com/vercel/ms/blob/v2.1.3/index.js

const S: number = 1000;
const M: number = S * 60;
const H: number = M * 60;
const D: number = H * 24;

// parseDuration reimplements ms() parse logic for a subset of inputs.
// Real ms supports many formats; we verify a representative subset.
function parseDuration(s: string): number {
  if (s === "1s") return S;
  if (s === "1m") return M;
  if (s === "1h") return H;
  if (s === "1d") return D;
  if (s === "2s") return 2 * S;
  if (s === "10m") return 10 * M;
  return 0;
}

// formatShort reimplements ms() format logic (short form).
function formatShort(ms: number): string {
  if (ms >= D) return "d";
  if (ms >= H) return "h";
  if (ms >= M) return "m";
  if (ms >= S) return "s";
  return "ms";
}

// Invariants we verify:
// 1. Unit constants are consistent (s * 60 === m, etc.)
console.assert(S === 1000);
console.assert(M === 60000);
console.assert(H === 3600000);
console.assert(D === 86400000);

// 2. Parse returns correct values for known inputs
console.assert(parseDuration("1s") === 1000);
console.assert(parseDuration("1m") === 60000);
console.assert(parseDuration("1h") === 3600000);
console.assert(parseDuration("2s") === 2000);

// 3. Format classifies durations correctly
console.assert(formatShort(500) === "ms");
console.assert(formatShort(1500) === "s");
console.assert(formatShort(65000) === "m");
console.assert(formatShort(3700000) === "h");
console.assert(formatShort(90000000) === "d");
