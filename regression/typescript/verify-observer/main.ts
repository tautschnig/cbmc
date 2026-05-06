class Subject {
  value: number;
  observer_count: number;
  constructor() { this.value = 0; this.observer_count = 0; }
  update(v: number): void { this.value = v; this.observer_count = this.observer_count + 1; }
}
const s = new Subject();
s.update(5);
s.update(10);
console.assert(s.value === 10);
console.assert(s.observer_count === 2);
