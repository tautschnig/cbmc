class BoundedQueue {
  items: number[];
  size: number;
  capacity: number;
  constructor(cap: number) { this.items = []; this.size = 0; this.capacity = cap; }
  enqueue(x: number): boolean {
    if (this.size >= this.capacity) return false;
    this.items.push(x);
    this.size = this.size + 1;
    return true;
  }
  getSize(): number { return this.size; }
}
const q = new BoundedQueue(3);
console.assert(q.enqueue(1) === true);
console.assert(q.enqueue(2) === true);
console.assert(q.enqueue(3) === true);
console.assert(q.getSize() === 3);
