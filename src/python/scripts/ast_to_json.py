"""
Python Language Interface — AST-to-JSON helper script.

Minimal Python script that uses CPython's ast module to parse a .py file
and emit a JSON representation of the AST. All semantic analysis happens
in C++; this script does nothing but parse and serialize.
"""

import ast
import json
import sys


def ast_node_to_dict(node):
    """Recursively convert an ast.AST node to a JSON-serializable dict."""
    if isinstance(node, ast.AST):
        result = {"_type": node.__class__.__name__}
        for field, value in ast.iter_fields(node):
            result[field] = ast_node_to_dict(value)
        # Include line/col info where available
        for attr in ("lineno", "col_offset", "end_lineno", "end_col_offset"):
            if hasattr(node, attr):
                v = getattr(node, attr)
                if v is not None:
                    result[attr] = v
        return result
    elif isinstance(node, list):
        return [ast_node_to_dict(item) for item in node]
    else:
        # Primitive value (str, int, float, bool, None, bytes, etc.)
        return node


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.py> <output.json>", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, "r") as f:
        source = f.read()

    try:
        tree = ast.parse(source, filename=input_path)
    except SyntaxError as e:
        print(f"Syntax error in {input_path}: {e}", file=sys.stderr)
        sys.exit(1)

    result = ast_node_to_dict(tree)
    result["_filename"] = input_path

    with open(output_path, "w") as f:
        json.dump(result, f, indent=2, default=str)


if __name__ == "__main__":
    main()
