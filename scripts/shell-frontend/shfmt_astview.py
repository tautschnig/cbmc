#!/usr/bin/env python3
"""Compact tree view of `shfmt --to-json` output (reads JSON on stdin)."""
import json
import sys

# Position-only fields: show line:col for the node instead.
POS_KEYS = {"Pos", "End", "ValuePos", "ValueEnd", "OpPos", "Dollar", "Rbrace",
            "Lbrace", "Left", "Right", "IfPos", "ThenPos", "FiPos", "ElsePos",
            "Semicolon", "Lparen", "Rparen", "Position", "WhilePos", "DoPos",
            "DonePos", "ForPos", "InPos", "Case", "Esac", "In", "Hash",
            "Dollar", "TokPos", "Last", "Colon", "Lbrack", "Rbrack"}


def pos(n):
    p = n.get("Pos")
    return f"@{p['Line']}:{p['Col']}" if isinstance(p, dict) and "Line" in p else ""


def show(n, d=0, key=""):
    if isinstance(n, dict):
        t = n.get("Type", "")
        scalars = {k: v for k, v in n.items()
                   if isinstance(v, (str, int, bool)) and k != "Type"
                   and v not in (False, 0, "")}
        print("  " * d + (key + ": " if key else "") + t + pos(n) + " "
              + (json.dumps(scalars) if scalars else ""))
        for k, v in n.items():
            if isinstance(v, (dict, list)) and k not in POS_KEYS:
                show(v, d + 1, k)
    elif isinstance(n, list):
        for i, x in enumerate(n):
            show(x, d, f"{key}[{i}]")


show(json.load(sys.stdin))
