#!/usr/bin/env python3
"""Check public-document coverage, source links and the generated Pages artifact."""
from html.parser import HTMLParser
import importlib.util
from pathlib import Path
import re
from types import SimpleNamespace
import unittest
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("docs_config", ROOT / "docs/conf.py")
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__()
        self.ids, self.links = set(), []
        self.feed(path.read_text())

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        for key in ("href", "src"):
            if key in attrs:
                self.links.append(attrs[key])


class DocumentationTests(unittest.TestCase):
    def test_every_public_page_in_navigation(self):
        toc = (ROOT / "docs/index.rst").read_text().splitlines()
        entries = {line.strip().split("<")[-1].rstrip(">")
                   for line in toc if line.startswith("   ")}
        pages = {"README", "USER_MANUAL", "AGENTS"}
        pages.update("docs/" + p.stem for p in (ROOT / "docs").glob("*.md"))
        self.assertTrue(pages <= entries, sorted(pages - entries))

    def test_code_links_are_external_and_missing_links_fail(self):
        app = SimpleNamespace(srcdir=ROOT / "build-docs/nonexistent-stage")
        source = ["[source](../firmware/app/include/keyboard_app.h)"]
        config.source_links(app, "docs/PORTING", source)
        self.assertIn("https://github.com/AL-255/MIDI-Typist/blob/main/", source[0])
        for link in ("../missing-source.c", "../../outside-repo", "../build-docs/html/index.html"):
            with self.subTest(link=link), self.assertRaises(ValueError):
                config.source_links(app, "docs/PORTING", [f"[invalid]({link})"])

    def test_html_links_and_public_artifact(self):
        output = ROOT / "build-docs/html"
        self.assertTrue((output / "index.html").is_file(), "Build docs first")
        self.assertTrue((output / ".nojekyll").is_file())
        self.assertTrue((output / "searchindex.js").is_file())
        pages = {path.resolve(): Page(path) for path in output.rglob("*.html")}
        overview = pages[(output / "README.html").resolve()]
        for name in ("RZ03-0499.png", "MG-M1V5TMR.png"):
            self.assertTrue(any(link.endswith(name) and not urlsplit(link).scheme
                                for link in overview.links),
                            f"README sprite must be served by the documentation site: {name}")
        for path in output.rglob("*"):
            self.assertFalse(path.is_symlink(), str(path))
            self.assertNotIn(path.suffix, (".bin", ".exe", ".pickle", ".doctree"))
            self.assertNotIn(path.name, ("device-dumps", "third_party", ".doctrees"))
            if path.name == "_sources":
                self.assertFalse(list(path.iterdir()), "Source copies must be disabled")
        for path, page in pages.items():
            for url in page.links:
                parts = urlsplit(url)
                if parts.scheme or parts.netloc:
                    continue
                target = (path.parent / unquote(parts.path)).resolve() if parts.path else path
                if target.is_dir():
                    target /= "index.html"
                with self.subTest(page=path.name, link=url):
                    self.assertTrue(target.is_relative_to(output.resolve()))
                    self.assertTrue(target.exists(), f"Missing {target}")
                    if parts.fragment and target in pages:
                        self.assertIn(unquote(parts.fragment), pages[target].ids)

    def test_actions_are_pinned(self):
        workflow = (ROOT / ".github/workflows/docs.yml").read_text()
        actions = re.findall(r"uses:\s+([^\s]+)", workflow)
        self.assertTrue(actions)
        self.assertTrue(all(re.fullmatch(r"actions/[\w-]+@[a-f0-9]{40}", a) for a in actions))


if __name__ == "__main__":
    unittest.main()
