class Vehicle {
  speed: number;
  constructor(s: number) { this.speed = s; }
  isFast(): boolean { return this.speed > 100; }
}
class Car extends Vehicle {
  doors: number;
  constructor(s: number, d: number) { super(s); this.doors = d; }
}
const c = new Car(120, 4);
console.assert(c.isFast() === true);
console.assert(c.doors === 4);
console.assert(c.speed === 120);
