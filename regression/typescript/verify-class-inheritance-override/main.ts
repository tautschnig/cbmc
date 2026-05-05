class Animal {
  legs: number;
  constructor(l: number) { this.legs = l; }
  canFly(): boolean { return false; }
}
class Bird extends Animal {
  constructor() { super(2); }
  canFly(): boolean { return true; }
}
const b = new Bird();
console.assert(b.legs === 2);
console.assert(b.canFly() === true);
