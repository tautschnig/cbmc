// Event emitter with typed handlers
class EventEmitter {
  listener_count: number;
  event_count: number;

  constructor() {
    this.listener_count = 0;
    this.event_count = 0;
  }

  on(): void { this.listener_count = this.listener_count + 1; }
  off(): void { if (this.listener_count > 0) this.listener_count = this.listener_count - 1; }
  emit(): number {
    this.event_count = this.event_count + 1;
    return this.listener_count;
  }
}

const e = new EventEmitter();
e.on();
e.on();
e.on();
console.assert(e.listener_count === 3);
const fired: number = e.emit();
console.assert(fired === 3);
console.assert(e.event_count === 1);
e.off();
console.assert(e.listener_count === 2);
