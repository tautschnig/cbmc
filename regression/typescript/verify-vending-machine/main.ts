class Vending {
  credit: number;
  dispensed: boolean;
  constructor() { this.credit = 0; this.dispensed = false; }
  insert(amount: number): void { this.credit = this.credit + amount; }
  buy(price: number): boolean {
    if (this.credit >= price) {
      this.credit = this.credit - price;
      this.dispensed = true;
      return true;
    }
    return false;
  }
}
const v = new Vending();
v.insert(100);
console.assert(v.buy(50) === true);
console.assert(v.credit === 50);
console.assert(v.dispensed === true);
