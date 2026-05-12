# Wave 8 stub smoke test: import-only tests of
# sqlalchemy, click, pytest. Full API exercise is avoided
# because the stubs include Python-level code (class
# definitions, helper functions) that our frontend
# processes eagerly and can surface tight symex on e.g.
# list.append in helper closures.

from sqlalchemy import Engine, create_engine, MetaData
from click import echo, ClickException
from pytest import fixture
