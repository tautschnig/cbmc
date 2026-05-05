class Node {
  value: number;
  next: number; // index of next node (-1 = null)
  constructor(v: number) { this.value = v; this.next = -1; }
}
const n1 = new Node(10);
const n2 = new Node(20);
n1.next = 1; // points to n2
console.assert(n1.value === 10);
console.assert(n2.value === 20);
console.assert(n1.next === 1);
