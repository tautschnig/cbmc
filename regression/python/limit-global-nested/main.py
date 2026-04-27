# PLR §7.12: global statement
counter: int = 0
def increment() -> None:
    global counter
    counter += 1

increment()
increment()
assert counter == 2
