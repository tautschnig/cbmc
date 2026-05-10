"""
Verification model of the `abc` module.

ABC (abstract base class) machinery is modelled as no-ops:
``ABC``/``ABCMeta`` are identity metaclasses, and
``abstractmethod``/``abstractproperty``/``abstractclassmethod``/
``abstractstaticmethod`` return the decorated object unchanged.
This makes code that inherits from ``ABC`` or uses ``@abstractmethod``
parse and verify correctly — the abstract-ness is a runtime
behaviour that the frontend doesn't model.
"""


class ABCMeta(type):
    def __new__(mcs, name, bases, namespace, **kwargs):
        return super().__new__(mcs, name, bases, namespace)

    def register(cls, subclass):
        return subclass


class ABC(metaclass=ABCMeta):
    pass


def abstractmethod(func):
    return func


def abstractproperty(func):
    return property(func)


def abstractclassmethod(func):
    return classmethod(func)


def abstractstaticmethod(func):
    return staticmethod(func)


def get_cache_token():
    return 0


def update_abstractmethods(cls):
    return cls
