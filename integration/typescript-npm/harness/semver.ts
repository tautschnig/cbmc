// Harness for npm package 'semver' (npm/node-semver)
// Version tracked in integration/typescript-npm/package.json
//
// semver provides parsing and comparison of semantic versions.
// We verify the comparison function satisfies total-order axioms
// over SYMBOLIC version components. This is non-trivial: comparison
// of a.b.c is lexicographic over three fields.
//
// Upstream: https://github.com/npm/node-semver/blob/v7.7.2/functions/compare.js

// Reimplementation of semver's `compare(a, b)` for release versions:
//   -1 if a < b, 1 if a > b, 0 if equal.
// Prerelease/build metadata not modeled here.
function cmp(
  aMajor: number, aMinor: number, aPatch: number,
  bMajor: number, bMinor: number, bPatch: number
): number {
  if (aMajor < bMajor) return -1;
  if (aMajor > bMajor) return 1;
  if (aMinor < bMinor) return -1;
  if (aMinor > bMinor) return 1;
  if (aPatch < bPatch) return -1;
  if (aPatch > bPatch) return 1;
  return 0;
}

// Symbolic version components, bounded to keep the search tractable.
function nondetVersion(): number[] {
  const v: number[] = [nondet_number(), nondet_number(), nondet_number()];
  __CPROVER_assume(
    v[0] >= 0 && v[0] <= 10 &&
    v[1] >= 0 && v[1] <= 10 &&
    v[2] >= 0 && v[2] <= 10
  );
  return v;
}

const a: number[] = nondetVersion();
const b: number[] = nondetVersion();
const c: number[] = nondetVersion();

// Property 1: reflexivity. cmp(a, a) === 0.
console.assert(cmp(a[0], a[1], a[2], a[0], a[1], a[2]) === 0);

// Property 2: antisymmetry. cmp(a, b) === -cmp(b, a).
const ab: number = cmp(a[0], a[1], a[2], b[0], b[1], b[2]);
const ba: number = cmp(b[0], b[1], b[2], a[0], a[1], a[2]);
console.assert(ab === -ba);

// Property 3: comparison range. Only returns -1, 0, or 1.
console.assert(ab === -1 || ab === 0 || ab === 1);

// Property 4: equality iff all components equal.
if (ab === 0) {
  console.assert(a[0] === b[0]);
  console.assert(a[1] === b[1]);
  console.assert(a[2] === b[2]);
}

// Property 5: transitivity of strict ordering. If a < b < c, then a < c.
const bc: number = cmp(b[0], b[1], b[2], c[0], c[1], c[2]);
const ac: number = cmp(a[0], a[1], a[2], c[0], c[1], c[2]);
if (ab === -1 && bc === -1) {
  console.assert(ac === -1);
}
if (ab === 1 && bc === 1) {
  console.assert(ac === 1);
}

// Property 6: major version dominates. If a.major < b.major, then a < b
// regardless of other components.
if (a[0] < b[0]) {
  console.assert(cmp(a[0], a[1], a[2], b[0], b[1], b[2]) === -1);
}
