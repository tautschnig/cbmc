// UUID-like structure verification
class UUID {
  high: number;
  low: number;
  constructor(h: number, l: number) { this.high = h; this.low = l; }
  equals(other: UUID): boolean {
    return this.high === other.high && this.low === other.low;
  }
  isNil(): boolean {
    return this.high === 0 && this.low === 0;
  }
}
const id1 = new UUID(12345, 67890);
const id2 = new UUID(12345, 67890);
const nil = new UUID(0, 0);
console.assert(id1.equals(id2) === true);
console.assert(nil.isNil() === true);
console.assert(id1.isNil() === false);
