# ll.py does `from md import Foo, Bar`; importing ll must transitively
# resolve md so `Foo(...)`/`Bar(...)` inside ll.create have bodies.
# Module-qualified `isinstance(x, ll.Bar)` must resolve the class too.
import ll

b = ll.create("Bar")
assert isinstance(b, ll.Bar)
