"""Rebuild pinned third-party data in a temporary directory and compare text."""

from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    generators = (
        ("build_full_dict.py", root / "data/core_dict.txt"),
        ("generate_hanzi_conv.py", root / "engine/include/hanzi_conv.h"),
    )
    with tempfile.TemporaryDirectory(prefix="eime-data-check-") as temporary:
        for script, checked_in in generators:
            generated = Path(temporary) / checked_in.name
            subprocess.run(
                [sys.executable, str(root / "tools" / script), "--output", str(generated)],
                check=True,
            )
            # Git checkouts may use CRLF on Windows; compare normalized text.
            if generated.read_text(encoding="utf-8") != checked_in.read_text(encoding="utf-8"):
                print(f"FAIL: {checked_in.relative_to(root)} differs from its pinned generator")
                return 1
            print(f"PASS: {checked_in.relative_to(root)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
