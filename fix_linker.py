from pathlib import Path
import re

p = Path("Makefile")
s = p.read_text(encoding="utf-8")

lines = s.splitlines()

for i, line in enumerate(lines):
    if "$(LD)" in line:
        lines[i] = re.sub(
            r"build/idt\.o\s*build/timer\.o",
            "build/idt.o build/timer.o",
            line
        )

p.write_text("\n".join(lines) + "\n", encoding="utf-8")

for line in lines:
    if "$(LD)" in line:
        print("BARIS LINKER:")
        print(repr(line))
