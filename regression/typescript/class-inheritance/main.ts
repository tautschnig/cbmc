// ES2024 sec-class-definitions: Class inheritance
class Animal {
  legs: number;
  constructor(legs: number) { this.legs = legs; }
  getLegs(): number { return this.legs; }
}
class Dog extends Animal {
  constructor() { super(4); }
  getLegs(): number { return this.legs; }
}
const d = new Dog();
console.assert(d.legs === 4);
console.assert(d.getLegs() === 4);
