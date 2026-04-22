class AppError(Exception):
    pass

class NotFoundError(AppError):
    pass

def check(x: int) -> int:
    if x < 0:
        raise NotFoundError()
    return x
