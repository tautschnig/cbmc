def binary_search(arr: list[int], target: int, size: int) -> int:
    __CPROVER_assume(size >= 0 and size <= 10)
    __CPROVER_assume(len(arr) == size)
    low: int = 0
    high: int = size - 1
    while low <= high:
        mid: int = low + (high - low) // 2
        if arr[mid] == target:
            return mid
        elif arr[mid] < target:
            low = mid + 1
        else:
            high = mid - 1
    return -1
