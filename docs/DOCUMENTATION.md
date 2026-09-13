# Build and publish documentation

The [documentation website](https://al-255.github.io/MIDI-Typist/) renders the
repository's Markdown with Sphinx, MyST and Furo. Markdown remains readable on
GitHub; there is no separate copy to maintain.

## Local build

Use Python 3.13 for the pinned documentation toolchain; this is separate from
the firmware tools' Python 3.10+ requirement.

```sh
python3.13 -m venv build-docs/venv
build-docs/venv/bin/python -m pip install -r docs/requirements.txt
build-docs/venv/bin/python tools/build_docs.py
build-docs/venv/bin/python tools/test_docs.py
python3 -m http.server 8000 --directory build-docs/html
```

Open `http://localhost:8000`. The build uses fresh staging/output directories
and fails on warnings, missing documents and unresolved heading references.
The artifact check follows generated HTML links/anchors, verifies navigation
coverage and rejects private/binary/cache files in the published output.
Only README, USER_MANUAL, AGENTS and `docs/*.md` are staged; firmware, private
dumps, submodules and generated binaries are never copied into the site.
Code/license links point to their source on GitHub instead of downloading it.
Build outputs live in Git-ignored `build-docs/`.

## GitHub Pages CI

[The workflow](../.github/workflows/docs.yml) builds every pull request and
push to `main`, and supports manual dispatch. Pull requests have read-only
permissions and never deploy. A successful main-branch build uploads only
`build-docs/html`; a separate protected `github-pages` job deploys it with
`pages: write` and `id-token: write`. Actions are pinned to commit IDs.

In repository **Settings → Pages → Build and deployment**, select **GitHub
Actions** as the source. This repository uses that setting. No `gh-pages`
branch, personal token in CI, firmware build, SDK download or device access
is required. Forks must enable Pages themselves and adjust the repository/site
URLs in `docs/conf.py` before deployment. See
[GitHub's custom-workflow guide](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## Editing rules

- Describe the latest build only. Remove obsolete status, hashes and session
  narratives; preserve useful design rationale and supported factory behavior.
- Enforce the [current-only implementation rule](../AGENTS.md#latest-implementation-only):
  one current custom firmware and matching GUI, without superseded variants or
  protocol/profile fallbacks. CI checks this rule before building the site.
- Use the manual for operation, Building for commands, Telemetry for byte
  layouts, Device storage for persistence, and Validation for evidence limits.
  Link to these rather than duplicating tables or test reports.
- Use relative Markdown links, including heading anchors. Put every new page
  in `docs/index.rst`; the strict build catches unlisted documents.
- Do not modify upstream SDK documents/licenses for editorial consistency.
- Run the local build before committing. Review desktop/mobile navigation,
  tables and fixed-width keyboard illustrations when changing their layout.

The site has built-in search, dark/light themes and a grouped navigation tree.
The [MyST reference](https://myst-parser.readthedocs.io/en/latest/syntax/cross-referencing.html)
explains Markdown cross-references; contributor rules are in [AGENTS](../AGENTS.md).
