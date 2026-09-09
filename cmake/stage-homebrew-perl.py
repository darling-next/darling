#!/usr/bin/env python3
"""Stage Perl's normal CMake install into the opt-in Homebrew component."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--manifest-input", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    stage = build / "rootless-homebrew-perl"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    environment = os.environ.copy()
    # An outer packaging DESTDIR must not redirect this private build component.
    environment.pop("DESTDIR", None)
    subprocess.run(
        [args.cmake, "--install", str(build / "src/external/perl"),
         "--prefix", str(stage)],
        env=environment,
        check=True,
    )
    manifest = json.loads(args.manifest_input.read_text())
    seen = {entry["guest_path"] for key in ("entrypoints", "resources")
            for entry in manifest[key]}
    root = stage / "libexec/darling"
    count = 0
    # Runtime payload only; manpage aliases are not runtime prerequisites.
    for directory in ("System/Library/Perl", "Library/Perl", "usr/bin"):
        source_root = root / directory
        if not source_root.is_dir():
            raise RuntimeError(f"Perl install omitted {directory}")
        for source in sorted(source_root.rglob("*")):
            if source.is_dir():
                continue
            resolved = source.resolve(strict=True)
            if not resolved.is_relative_to(root) or not resolved.is_file():
                raise RuntimeError(f"unsafe Perl runtime resource: {source}")
            guest = "/" + source.relative_to(root).as_posix()
            if guest in seen:
                continue
            manifest["resources"].append({
                "target": "homebrew-perl:" + guest,
                "guest_path": guest,
                "host_path": str(resolved),
            })
            seen.add(guest)
            count += 1
    temporary = args.manifest_output.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(args.manifest_output)
    print(f"HOMEbrew component: staged {count} Perl runtime files", flush=True)


if __name__ == "__main__":
    main()
