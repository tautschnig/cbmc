# --python-check-typeddict-fields: at PEP 448 dict-spread call
# sites, verify that each spread value's static AST-derived
# category matches the corresponding TypedDict field's declared
# category. None passed where 'str' is declared is a TypeError.
#
# The category is taken from the original Python AST at
# assignment time (before any safe_typecast unification erases
# the original type).

from stub import Service


client = Service()

# Bug: Description is annotated str but None passed.
config: dict[str, object] = {
    'Name': 'demo',
    'Description': None,
    'Count': 7,
}
client.create(**config)
