// KNOWNBUG: Prototype-chain methods not modeled.
// ES2024 §20.1.2.12 (getPrototypeOf), §20.1.2.21 (setPrototypeOf),
// §20.1.3.3 (isPrototypeOf). Our struct model has no prototype link;
// objects are flat structs.
class Animal { kind: string = "animal"; }
class Dog extends Animal { sound: string = "bark"; }
const rex: Dog = new Dog();
// At runtime, rex has Dog.prototype → Animal.prototype in its chain.
// This isn't modeled.
console.assert(Animal.prototype.isPrototypeOf(rex));
