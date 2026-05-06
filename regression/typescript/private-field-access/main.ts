class Account {
  private balance: number;
  constructor(b: number) { this.balance = b; }
  getBalance(): number { return this.balance; }
}
const a = new Account(100);
console.assert(a.getBalance() === 100);
