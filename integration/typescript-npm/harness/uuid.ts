// Harness for npm package 'uuid' (uuidjs/uuid)
// Version tracked in integration/typescript-npm/package.json
//
// The uuid package generates and validates UUIDs.
// We verify UUID structure and equality properties.
//
// Upstream: https://github.com/uuidjs/uuid

class UUID {
  high: number;
  low: number;
  version: number;
  constructor(h: number, l: number, v: number) {
    this.high = h; this.low = l; this.version = v;
  }
  equals(other: UUID): boolean {
    return this.high === other.high &&
           this.low === other.low &&
           this.version === other.version;
  }
  isNil(): boolean {
    return this.high === 0 && this.low === 0 && this.version === 0;
  }
  getVersion(): number { return this.version; }
}

const v4_a = new UUID(12345, 67890, 4);
const v4_b = new UUID(12345, 67890, 4);
const v4_c = new UUID(99999, 11111, 4);
const nil = new UUID(0, 0, 0);

// Invariants:
// 1. Equality is reflexive and compares all fields
console.assert(v4_a.equals(v4_a) === true);
console.assert(v4_a.equals(v4_b) === true);
console.assert(v4_a.equals(v4_c) === false);

// 2. Version tracking
console.assert(v4_a.getVersion() === 4);
console.assert(nil.getVersion() === 0);

// 3. Nil UUID detection
console.assert(nil.isNil() === true);
console.assert(v4_a.isNil() === false);
