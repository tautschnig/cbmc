class Queue {
  a: number; b: number; c: number; d: number;
  head: number; tail: number;
  constructor() { this.a = 0; this.b = 0; this.c = 0; this.d = 0; this.head = 0; this.tail = 0; }
  enqueue(v: number): void {
    if (this.tail === 0) this.a = v;
    else if (this.tail === 1) this.b = v;
    else if (this.tail === 2) this.c = v;
    else this.d = v;
    this.tail = this.tail + 1;
  }
  size(): number { return this.tail - this.head; }
}
const q = new Queue();
q.enqueue(10); q.enqueue(20); q.enqueue(30);
console.assert(q.size() === 3);
console.assert(q.a === 10);
console.assert(q.b === 20);
console.assert(q.c === 30);
