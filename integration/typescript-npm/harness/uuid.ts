// Harness for npm package 'uuid' (uuidjs/uuid)
// Version tracked in integration/typescript-npm/package.json
//
// Verifies UUID equality and structural invariants over SYMBOLIC values.
// If the frontend miscompiles struct comparison or field access,
// these properties fail.
//
// Upstream: https://github.com/uuidjs/uuid

class UUID {
  high: number;
  low: number;
  version: number;
  constructor(h: number, l: number, v: number) {
    this.high = h;
    this.low = l;
    this.version = v;
  }
  equals(other: UUID): boolean {
    return this.high === other.high &&
           this.low === other.low &&
           this.version === other.version;
  }
  isNil(): boolean {
    return this.high === 0 && this.low === 0 && this.version === 0;
  }
}

// Property 1: equality is reflexive over symbolic values.
// For any nondet UUID components, an UUID equals itself.
// (Excluding NaN: NaN !== NaN per IEEE 754, which the JS spec respects.)
const h: number = nondet_number();
const l: number = nondet_number();
const v: number = nondet_number();
__CPROVER_assume(!Number.isNaN(h) && !Number.isNaN(l) && !Number.isNaN(v));
__CPROVER_assume(v >= 1 && v <= 5);

const u1: UUID = new UUID(h, l, v);
console.assert(u1.equals(u1));

// Property 2: equality is symmetric.
const u2: UUID = new UUID(h, l, v);
console.assert(u1.equals(u2) === u2.equals(u1));
// For identical components, they must be equal.
console.assert(u1.equals(u2));

// Property 3: difference in any field breaks equality.
// Under the assumption h != 0, the version-bumped uuid must differ.
__CPROVER_assume(v < 5); // leave room to increment
const u3: UUID = new UUID(h, l, v + 1);
console.assert(!u1.equals(u3));

// Property 4: nil detection is correct.
const nilU: UUID = new UUID(0, 0, 0);
console.assert(nilU.isNil());

// Non-nil detection over a symbolic non-zero value:
const nz: number = nondet_number();
__CPROVER_assume(nz !== 0);
const nonNil: UUID = new UUID(nz, 0, 0);
console.assert(!nonNil.isNil());
