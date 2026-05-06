// Shopping cart with inventory tracking
interface Item {
  id: number;
  price: number;
}

class Cart {
  total: number;
  itemCount: number;
  
  constructor() {
    this.total = 0;
    this.itemCount = 0;
  }
  
  add(item: Item): void {
    this.total = this.total + item.price;
    this.itemCount = this.itemCount + 1;
  }
  
  applyDiscount(percent: number): void {
    if (percent > 0 && percent <= 100) {
      this.total = this.total - (this.total * percent / 100);
    }
  }
  
  isEmpty(): boolean {
    return this.itemCount === 0;
  }
}

const cart = new Cart();
console.assert(cart.isEmpty() === true);
cart.add({ id: 1, price: 100 });
cart.add({ id: 2, price: 200 });
console.assert(cart.total === 300);
console.assert(cart.itemCount === 2);
cart.applyDiscount(10);
console.assert(cart.total === 270);
