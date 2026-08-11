# Regression for string_expr.h:160 to_string_expr precondition violation
# in simplify_string_contains/equalsIgnoreCase/etc.
# Triggered when the Python frontend hands the simplifier a single
# character that hasn't been wrapped as a refined_string_exprt.

caracteres_especiais_invalidos = '!@#$%^&*()_+=-[]{}|;:,.<>?/~`'

def validate_title(title: str):
    last = title[len(title) - 1]
    assert not last.isdigit()
    assert last not in caracteres_especiais_invalidos

def main() -> None:
    title = "Livro"
    validate_title(title)

main()
