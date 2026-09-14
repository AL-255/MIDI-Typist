"""Portable Tk typography for the configuration GUI.

Tk never substitutes a missing family: on X11 it silently falls back to the
core bitmap font `fixed`, which is why a hard-coded ``('sans', 10)`` renders
pixelated. The candidates below are therefore resolved against the families
this Tk build can really use, in preference order, so the same code selects
Segoe UI on Windows, the system UI face on macOS and the desktop's outline
family on Linux -- including the scalable X11 core families (URW Nimbus,
Latin Modern) that remain available when Tk runs without fontconfig/Xft. A
candidate must also provide an upright bold face, because Tk answers a bold
request for a family that lacks one with its oblique face.

Only the standard library is used, and every size is a point size so Tk's
reported DPI scaling applies on any platform. When no outline family exists
at all, the module keeps the best family Tk offers and reports it through
`Typography.advisory` instead of pretending the text is smooth.
"""
from tkinter import font as tkfont

# Windows, macOS and fontconfig-based Linux desktops first.
SANS_FAMILIES = (
    'Inter', 'Segoe UI Variable Text', 'Segoe UI', 'SF Pro Text', 'Helvetica Neue',
    'Cantarell', 'Adwaita Sans', 'Ubuntu', 'Noto Sans', 'DejaVu Sans',
    'Liberation Sans', 'Arial', 'FreeSans', 'Verdana', 'Tahoma',
    # Scalable families served by the X11 core font path (no Xft/fontconfig).
    'Latin Modern Sans', 'Nimbus Sans L', 'URW Gothic L',
)
MONO_FAMILIES = (
    'Cascadia Mono', 'Consolas', 'SF Mono', 'Menlo', 'JetBrains Mono', 'Iosevka',
    'Ubuntu Mono', 'Noto Sans Mono', 'DejaVu Sans Mono', 'Liberation Mono',
    'Courier New', 'FreeMono',
    'Nimbus Mono L', 'Courier 10 Pitch', 'Latin Modern Typewriter',
)
# Last resort: core bitmap families that are still shaped better than `fixed`.
BITMAP_SANS_FAMILIES = ('Clean', 'Lucida', 'Helvetica')
BITMAP_MONO_FAMILIES = ('Lucida Typewriter', 'Courier')

WIDGET_SIZE = 10
TITLE_SIZE = 17


def resolves(family, size=WIDGET_SIZE, probe=None, weight='normal'):
    """True when this Tk build really has `family` with an upright `weight` face.

    Tk silently substitutes a missing family and, worse, a family that exists
    without the requested face: Latin Modern Sans on a core X11 font path has
    no upright bold, so a bold request is answered with its oblique face. Both
    are rejected here.

    `probe` is a reusable `tkfont.Font`; without one a temporary font is used,
    which Tcl keeps until the interpreter exits.
    """
    if probe is None:
        probe = tkfont.Font(family=family, size=size, weight=weight, slant='roman')
    else:
        probe.configure(family=family, size=size, weight=weight, slant='roman')
    actual = probe.actual()
    return (actual['family'].lower() == family.lower() and
            actual['weight'] == weight and actual['slant'] == 'roman')


def pick(candidates, size=WIDGET_SIZE, probe=None, weight='normal'):
    """First candidate family this Tk build can use, else None."""
    for family in candidates:
        if resolves(family, size, probe, weight):
            return family
    return None


def pick_with_bold(candidates, size=WIDGET_SIZE, probe=None):
    """First candidate that can render upright regular *and* bold text."""
    for family in candidates:
        if resolves(family, size, probe) and resolves(family, size, probe, 'bold'):
            return family
    return None


class Typography:
    """Resolved families plus the font tuples the widgets should use."""

    def __init__(self, root, widget_size=WIDGET_SIZE, title_size=TITLE_SIZE):
        self.root = root
        self.widget = widget_size
        self.title = title_size
        self._probe = tkfont.Font(family='TkDefaultFont', size=widget_size)
        # Bold labels and the title are part of the design, so a family that
        # can only render regular text is a second choice.
        self.sans = (pick_with_bold(SANS_FAMILIES, widget_size, self._probe) or
                     pick(SANS_FAMILIES, widget_size, self._probe) or
                     pick(BITMAP_SANS_FAMILIES, widget_size, self._probe) or
                     tkfont.nametofont('TkDefaultFont').actual()['family'])
        self.mono = (pick(MONO_FAMILIES, widget_size, self._probe) or
                     pick(BITMAP_MONO_FAMILIES, widget_size, self._probe) or
                     tkfont.nametofont('TkFixedFont').actual()['family'])
        self.outline = self.sans in SANS_FAMILIES

    def sans_font(self, size=None, weight='normal'):
        return (self.sans, self.widget if size is None else size, weight)

    def mono_font(self, size=None, weight='normal'):
        return (self.mono, self.widget if size is None else size, weight)

    def measure(self, text, size=None, mono=False):
        """Pixel width of `text` in the resolved face (layout, not guessing)."""
        font = tkfont.Font(font=self.mono_font(size) if mono else self.sans_font(size))
        return font.measure(text)

    @property
    def advisory(self):
        """Empty when outline text is available; otherwise what to tell the user."""
        if self.outline:
            return ''
        return (f'Tk can only reach bitmap fonts here (using "{self.sans}"). This Python build '
                'has no fontconfig/X11 outline families; on Linux the distribution python3 with '
                'python3-tk renders vector text.')

    def configure_named(self):
        """Keep ttk internals, menus and dialogs in the same face."""
        settings = {'TkDefaultFont': (self.sans, self.widget, 'normal'),
                    'TkTextFont': (self.sans, self.widget, 'normal'),
                    'TkHeadingFont': (self.sans, self.widget, 'bold'),
                    'TkMenuFont': (self.sans, self.widget, 'normal'),
                    'TkIconFont': (self.sans, self.widget, 'normal'),
                    'TkCaptionFont': (self.sans, self.title - 6, 'bold'),
                    'TkTooltipFont': (self.sans, self.widget - 1, 'normal'),
                    'TkSmallCaptionFont': (self.sans, self.widget - 1, 'normal'),
                    'TkFixedFont': (self.mono, self.widget, 'normal')}
        for name, (family, size, weight) in settings.items():
            try:
                tkfont.nametofont(name).configure(family=family, size=size, weight=weight)
            except Exception:  # a stripped Tk may not define every named font
                continue

    def configure_styles(self, style):
        """Explicit fonts for the ttk styles this GUI uses."""
        for name in ('TLabel', 'TButton', 'TCheckbutton', 'TRadiobutton', 'TEntry',
                     'TCombobox', 'TMenubutton', 'TLabelframe.Label', 'TNotebook.Tab'):
            style.configure(name, font=self.sans_font())
        style.configure('Title.TLabel', font=self.sans_font(self.title, 'bold'))
        return style


def apply(root, style=None, widget_size=WIDGET_SIZE, title_size=TITLE_SIZE):
    """Resolve the families, install them, and return the Typography."""
    typography = Typography(root, widget_size, title_size)
    typography.configure_named()
    if style is not None:
        typography.configure_styles(style)
    return typography
