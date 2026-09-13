#!/usr/bin/env python3
"""Strict Sphinx build from an allowlist of public documentation only."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / "build-docs" / "html"
    if output.parent.is_symlink():
        raise ValueError("Refusing a symlinked documentation build directory")
    output.parent.mkdir(exist_ok=True)
    # A fresh stage prevents deleted documents or private checkout files from
    # leaking into the artifact. Only this temporary directory is removed.
    with tempfile.TemporaryDirectory(prefix="sphinx-source-", dir=output.parent) as directory:
        stage = Path(directory)
        sources = [ROOT / name for name in ("README.md", "USER_MANUAL.md", "AGENTS.md")]
        sources += sorted((ROOT / "docs").glob("*.md"))
        for source in sources:
            if source.is_symlink():
                raise ValueError(f"Documentation source must not be a symlink: {source}")
            target = stage / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        shutil.copyfile(ROOT / "docs/index.rst", stage / "index.rst")
        # Fresh HTML prevents orphaned pages from surviving a rename/removal.
        with tempfile.TemporaryDirectory(prefix="sphinx-html-", dir=output.parent) as html:
            subprocess.run([sys.executable, "-m", "sphinx", "-b", "html", "-n", "-W",
                            "--keep-going", "-E", "-a", "-d", str(stage / ".doctrees"),
                            "-c", str(ROOT / "docs"),
                            str(stage), html], check=True)
            if output.exists():
                if output.is_symlink():
                    raise ValueError("Refusing to replace a symlinked documentation output")
                shutil.rmtree(output)  # fixed, generated build-docs/html only
            shutil.copytree(html, output)
    print(f"Documentation: {output / 'index.html'}")


if __name__ == "__main__":
    main()
