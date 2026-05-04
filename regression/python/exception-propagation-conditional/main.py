# Division by zero in conditional branch not detected
x: int = 1
y: int = 0
if x == 1:
    z = x / y  # Should raise ZeroDivisionError
