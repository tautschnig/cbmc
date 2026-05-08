// Symbolic-input review: Array operations on symbolic arguments.
// After the symbolic-input audit, indexOf and fill now support
// symbolic arguments via if_exprt chains (matching how Map.has
// handles symbolic keys).
const a: number[] = [10, 20, 30, 40, 50];

// Symbolic index access (always worked)
const i: number = nondet_number();
__CPROVER_assume(i >= 0 && i < 5);
const v: number = a[i];
console.assert(v === 10 || v === 20 || v === 30 || v === 40 || v === 50);

// Symbolic includes target (always worked)
const y: number = nondet_number();
__CPROVER_assume(y >= 0 && y <= 100);
const has: boolean = a.includes(y);
if (y === 10 || y === 20 || y === 30 || y === 40 || y === 50) {
  console.assert(has);
} else {
  console.assert(!has);
}

// indexOf with symbolic target — now works via if_exprt chain
const pos: number = a.indexOf(y);
if (y === 10) console.assert(pos === 0);
if (y === 20) console.assert(pos === 1);
if (y === 50) console.assert(pos === 4);
if (y !== 10 && y !== 20 && y !== 30 && y !== 40 && y !== 50) {
  console.assert(pos === -1);
}

// fill with symbolic end — now works via per-slot if_exprt
const b: number[] = [1, 2, 3, 4, 5];
const k: number = nondet_number();
__CPROVER_assume(k >= 0 && k <= 5);
b.fill(0, 0, k);
if (k >= 1) console.assert(b[0] === 0);
if (k >= 5) console.assert(b[4] === 0);
if (k <= 0) console.assert(b[0] === 1);

// slice with symbolic end — now works via symbolic result_len
const c: number[] = [10, 20, 30, 40, 50];
const sl: number[] = c.slice(0, k);
console.assert(sl.length === k);
if (k >= 1) console.assert(sl[0] === 10);
if (k >= 2) console.assert(sl[1] === 20);
