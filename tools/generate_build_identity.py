"""Build-time Git provenance; no timestamps and no device access."""
import argparse
from pathlib import Path
import re
import subprocess


def identity(source):
    source = Path(source).resolve()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args],
                                       stderr=subprocess.DEVNULL).decode().strip()
    try:
        # Do not accidentally identify an enclosing repository for an export.
        if Path(git('rev-parse', '--show-toplevel')).resolve() != source:
            return 'unknown', 'unknown'
        commit = git('rev-parse', '--verify', 'HEAD')
        if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', commit):
            return 'unknown', 'unknown'
        dirty = git('status', '--porcelain', '--untracked-files=normal',
                    '--ignore-submodules=none')
        return commit, 'dirty' if dirty else 'clean'
    except (OSError, subprocess.CalledProcessError, UnicodeError):
        return 'unknown', 'unknown'


def generate(source, output):
    commit, state = identity(source)
    text = ('/* Generated at build time; do not edit. */\n'
            '#ifndef MT_GIT_IDENTITY_H\n#define MT_GIT_IDENTITY_H\n'
            f'#define MT_GIT_COMMIT "{commit}"\n'
            f'#define MT_GIT_STATE "{state}"\n#endif\n')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.source, args.output)
