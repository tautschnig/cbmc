class BankAccount {
  balance: number;
  constructor(initial: number) { this.balance = initial; }
  deposit(amount: number): void {
    __CPROVER_assert(amount > 0);
    this.balance = this.balance + amount;
  }
  getBalance(): number { return this.balance; }
}
const acc = new BankAccount(100);
acc.deposit(50);
console.assert(acc.getBalance() === 150);
