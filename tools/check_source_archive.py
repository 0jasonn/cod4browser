"""Check a git archive ZIP before distributing browser corresponding source."""

import argparse
from pathlib import PurePosixPath
import zipfile


def check_archive(path):
    forbidden_roots = ("deps/binklib/", "deps/msslib/", "deps/steamsdk/")
    forbidden_suffixes = {".dll", ".lib", ".exp", ".exe", ".flt", ".asi", ".so",
                          ".dylib", ".iwd", ".ff", ".bik", ".wasm"}
    required = {"LICENSE", "CMakeLists.txt", "tools/web_toolchain.json",
                "scripts/web/CMakeLists.txt"}
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            name = entry.filename
            parts = PurePosixPath(name).parts
            if name.startswith("/") or ".." in parts or "\\" in name:
                raise ValueError(f"Invalid archive path: {name}")
            if entry.is_dir():
                continue
            if (name.lower().startswith(forbidden_roots) or
                    PurePosixPath(name).suffix.lower() in forbidden_suffixes):
                raise ValueError(f"Non-distributable source entry: {name}")
            with archive.open(entry) as source:
                signature = source.read(4)
            if signature[:2] == b"MZ" or signature in {
                    b"\x7fELF", b"\x00asm", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe",
                    b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe"}:
                raise ValueError(f"Compiled binary in source archive: {name}")
            required.discard(name)
        if required:
            raise ValueError(f"Incomplete browser source archive: {sorted(required)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    args = parser.parse_args()
    check_archive(args.archive)
    print(f"Browser source archive boundary passed: {args.archive}")
