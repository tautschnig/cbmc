from stub import Client

c = Client()
# Missing required 'size' kwarg — should be detected as
# a property violation because 'size' is Required in the
# CreateThingInput TypedDict.
c.create_thing(name="foo")
