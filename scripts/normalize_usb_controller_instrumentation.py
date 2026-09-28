"""Normalize the staged hub instrumentation source layout."""


def normalize(source: str) -> str:
    lines = source.splitlines(keepends=True)
    if len(lines) <= 11:
        raise ValueError("unexpected staged source layout")
    pieces = lines[11].rstrip("\n").split("  ")
    if len(pieces) != 3:
        raise ValueError("unexpected staged source layout")
    suffix = "; } while (0)"
    if not pieces[2].endswith(suffix):
        raise ValueError("unexpected staged source layout")
    slash = chr(92)
    lines[11:12] = [
        pieces[0] + " " + slash + "\n",
        "    " + pieces[1] + " " + slash + "\n",
        "    " + pieces[2][:-len(suffix)] + "; " + slash + "\n",
        "} while (0)\n",
    ]
    return "".join(lines)
