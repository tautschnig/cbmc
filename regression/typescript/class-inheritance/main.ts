// ES2024 sec-class-definitions: Class inheritance
class Animal {
  name: string;
  constructor(name: string) { this.name = name; }
  speak(): string { return this.name + " makes a sound"; }
}
class Dog extends Animal {
  constructor(name: string) { super(name); }
  speak(): string { return this.name + " barks"; }
}
const d = new Dog("Rex");
console.assert(d.name === "Rex");
console.assert(d.speak() === "Rex barks");
