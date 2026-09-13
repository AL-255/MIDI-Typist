"""Build the repository's Markdown; do not import firmware or device tools."""
import os
from pathlib import Path
import re
from urllib.parse import quote, unquote, urlsplit

REPO = Path(__file__).resolve().parents[1]
SOURCE_URL = "https://github.com/AL-255/MIDI-Typist"
project = "MIDI-Typist"
author = "MIDI-Typist contributors"
copyright = "MIDI-Typist contributors"
release = re.search(r"project\([^)]*VERSION\s+([\d.]+)",
                    (REPO / "CMakeLists.txt").read_text(), re.I).group(1)
extensions = ["myst_parser", "sphinx.ext.githubpages"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
root_doc = "index"
language = "en"
nitpicky = True
myst_heading_anchors = 6
html_theme = "furo"
html_title = "MIDI-Typist documentation"
html_copy_source = False
html_show_sourcelink = False
html_baseurl = os.environ.get("DOCS_BASE_URL", "https://al-255.github.io/MIDI-Typist/")
html_theme_options = {
    "light_css_variables": {"color-brand-primary": "#156b57", "color-brand-content": "#156b57"},
    "dark_css_variables": {"color-brand-primary": "#6fdbb5", "color-brand-content": "#6fdbb5"},
}


def source_links(app, docname, source):
    """Keep Markdown document links local; link code/licenses to GitHub.

    The staged source contains only public documentation. Non-document links
    must resolve inside the repository before they can become external links;
    never copy referenced firmware, private dumps or SDK trees into the site.
    """
    if docname == "index":
        return
    def link(match):
        url = match.group(1)
        parts = urlsplit(url)
        if parts.scheme or parts.netloc or not parts.path:
            return match.group(0)
        relative = (Path(docname).parent / unquote(parts.path))
        target = (REPO / relative).resolve()
        if not target.is_relative_to(REPO) or not target.exists():
            raise ValueError(f"{docname}: missing/outside-repository link {url}")
        if (Path(app.srcdir) / relative).is_file():
            return match.group(0)
        path = target.relative_to(REPO).as_posix()
        if path.startswith(("device-dumps/", "build")) or (path.startswith(".") and not path.startswith(".github/")):
            raise ValueError(f"{docname}: private/generated link {url}")
        kind = "tree" if target.is_dir() else "blob"
        suffix = "#" + parts.fragment if parts.fragment else ""
        return "](" + SOURCE_URL + f"/{kind}/main/" + quote(path) + suffix + ")"
    source[0] = re.sub(r"\]\(([^\s)]+)\)", link, source[0])


def setup(app):
    app.connect("source-read", source_links)
    return {"parallel_read_safe": True, "parallel_write_safe": True}
