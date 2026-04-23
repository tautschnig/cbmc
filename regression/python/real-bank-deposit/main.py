class BankAccount:
    def __init__(self, balance: int) -> None:
        self.balance = balance

    def deposit(self, amount: int) -> None:
        self.balance = self.balance + amount

def verify_deposit(balance: int, amount: int) -> None:
    __CPROVER_assume(balance >= 0)
    __CPROVER_assume(amount > 0)
    acc = BankAccount(balance)
    acc.deposit(amount)
    assert acc.balance == balance + amount
