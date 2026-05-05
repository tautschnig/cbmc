class Animal { legs: number; constructor(l: number) { this.legs = l; } }
class Dog extends Animal { constructor() { super(4); } }
const d = new Dog();
console.assert(d instanceof Dog === true);
console.assert(d instanceof Animal === true);
console.assert(d.legs === 4);
