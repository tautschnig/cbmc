class Stack {
  items: number[];
  capacity: number;
  size: number;
  constructor(cap: number) { this.items = []; this.capacity = cap; this.size = 0; }
  push(x: number): boolean {
    if (this.size >= this.capacity) return false;
    this.items.push(x);
    this.size = this.size + 1;
    return true;
  }
  isFull(): boolean { return this.size >= this.capacity; }
}
const s = new Stack(2);
console.assert(s.push(1) === true);
console.assert(s.push(2) === true);
console.assert(s.isFull() === true);
console.assert(s.push(3) === false);
