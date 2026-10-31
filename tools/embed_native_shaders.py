"""Embed native MSL source, keeping shader assets relocatable."""
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_text(
    "#pragma once\ninline constexpr unsigned char native_msl[] = {\n" +
    ",\n".join(",".join(str(x) for x in data[i:i + 24]) for i in range(0, len(data), 24)) +
    "\n};\n")
