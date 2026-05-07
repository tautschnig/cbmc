enum State { Init = 0, Sent = 1, Acked = 2, Done = 3 }
class Protocol {
  state: State;
  constructor() { this.state = State.Init; }
  send(): void { if (this.state === State.Init) this.state = State.Sent; }
  ack(): void { if (this.state === State.Sent) this.state = State.Acked; }
  finish(): void { if (this.state === State.Acked) this.state = State.Done; }
}
const p = new Protocol();
p.send();
console.assert(p.state === State.Sent);
p.ack();
console.assert(p.state === State.Acked);
p.finish();
console.assert(p.state === State.Done);

// Out-of-order: ack without send
const p2 = new Protocol();
p2.ack();
console.assert(p2.state === State.Init); // no transition
