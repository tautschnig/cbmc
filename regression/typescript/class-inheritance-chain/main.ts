class Vehicle {
  speed: number;
  constructor(s: number) { this.speed = s; }
  getSpeed(): number { return this.speed; }
}
class Car extends Vehicle {
  doors: number;
  constructor(s: number, d: number) { super(s); this.doors = d; }
  getDoors(): number { return this.doors; }
}
const c = new Car(100, 4);
console.assert(c.getSpeed() === 100);
console.assert(c.getDoors() === 4);
