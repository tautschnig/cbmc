# Wave 9 stub smoke test: import-only tests of flask,
# fastapi, attr/attrs, tomllib/tomli, rich.
#
# Full API exercise is avoided because the stubs include
# Python-level code that our frontend processes eagerly.

from flask import Flask, Response, request, jsonify, Blueprint
from fastapi import FastAPI, APIRouter, HTTPException, Depends
from attr import define, field, Factory
from attrs import define as adefine
from tomllib import loads as tloads
from rich import Console, Table


assert True
