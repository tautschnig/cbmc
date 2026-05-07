class Queue {
  a: number; b: number; c: number; d: number;
  head: number; tail: number;
  constructor() { this.a = 0; this.b = 0; this.c = 0; this.d = 0; this.head = 0; this.tail = 0; }
  push(v: number): void {
    if (this.tail === 0) this.a = v;
    else if (this.tail === 1) this.b = v;
    else if (this.tail === 2) this.c = v;
    else this.d = v;
    this.tail = this.tail + 1;
  }
  size(): number { return this.tail - this.head; }
}
const q = new Queue();
q.push(10); q.push(20); q.push(30);
console.assert(q.size() >= 0);
console.assert(q.size() === 3);
console.assert(q.a === 10); // first pushed
console.assert(q.c === 30); // last pushed
