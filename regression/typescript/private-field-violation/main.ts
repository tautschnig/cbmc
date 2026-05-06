class Account {
  private balance: number;
  constructor(b: number) { this.balance = b; }
}
const a = new Account(100);
const x: number = a.balance;
