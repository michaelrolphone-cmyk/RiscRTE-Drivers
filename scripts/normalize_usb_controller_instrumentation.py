"""Normalize the pinned USB hub instrumentation to the upstream release source layout."""

def normalize(source: str) -> str:
    lines = source.splitlines(keepends=True)
    for index in (12, 13):
        if index >= len(lines) or not lines[index].startswith(" ") or lines[index].startswith("    "):
            raise ValueError("unexpected instrumented hub declaration layout")
        lines[index] = "   " + lines[index]
    return "".join(lines)
