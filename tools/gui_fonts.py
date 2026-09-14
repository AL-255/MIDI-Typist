"""Portable Tk typography for the configuration GUI.

Tk never substitutes a missing family: on X11 it silently falls back to the
core bitmap font `fixed`, which is why a hard-coded ``('sans', 10)`` renders
pixelated. Two rendering worlds are supported here, and the widgets always
ask this module for their fonts.

**Fontconfig/Xft or a platform font stack** (Windows, macOS, a normal Linux
Tk): the first available UI family is used -- Segoe UI, the macOS system face,
or Cantarell/Adwaita/Ubuntu/Noto/DejaVu -- in point sizes, antialiased.

**X11 core fonts only** (a Tk built without fontconfig, e.g. some conda
packages): nothing can be antialiased there, so the goal is crispness. Only a
face's native pixel size is pixel-perfect, so the family and the sizes are
chosen together and text is requested in pixels (negative Tk sizes) at sizes
the face really has. Proportional UI text uses Lucida, with Helvetica and the
misc-fixed family as fallbacks; numbers and other fixed columns use Terminus,
which is drawn for the screen and covers the arrows, bullets, dashes and
accents this GUI shows.
"""
from tkinter import font as tkfont

# Families served by fontconfig/Xft or the platform font stack: antialiased.
NATIVE_SANS_FAMILIES = (
    'Inter', 'Segoe UI Variable Text', 'Segoe UI', 'SF Pro Text', 'Helvetica Neue',
    'Cantarell', 'Adwaita Sans', 'Ubuntu', 'Noto Sans', 'DejaVu Sans',
    'Liberation Sans', 'Arial', 'FreeSans', 'Verdana', 'Tahoma',
)
NATIVE_MONO_FAMILIES = (
    'Cascadia Mono', 'Consolas', 'SF Mono', 'Menlo', 'JetBrains Mono', 'Iosevka',
    'Ubuntu Mono', 'Noto Sans Mono', 'DejaVu Sans Mono', 'Liberation Mono',
    'Courier New', 'FreeMono',
)
# X11 core bitmap faces in preference order. They have no antialiasing, so
# they are used only at their native pixel sizes (see X11_PIXEL_SIZES).
X11_SANS_FAMILIES = ('Lucida', 'Helvetica', 'New Century Schoolbook L', 'fixed')
X11_MONO_FAMILIES = ('Terminus', 'Lucida Typewriter', 'fixed', 'Courier')
X11_PIXEL_SIZES = (12, 13, 14, 15, 16, 18, 20, 24)
X11_BASE_PIXELS = 14     # labels and buttons
X11_TITLE_PIXELS = 18    # section titles

WIDGET_SIZE = 10         # point size for the antialiased path
TITLE_SIZE = 17


def resolves(family, size=WIDGET_SIZE, probe=None, weight='normal'):
    """True when this Tk build really has `family` with an upright `weight` face.

    Tk silently substitutes a missing family and, worse, a family that exists
    without the requested face: Latin Modern Sans on a core X11 font path has
    no upright bold, so a bold request is answered with its oblique face. Both
    are rejected here. `size` may be negative to mean pixels.

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


def bitmap_sizes(family, probe, bold=False):
    """Native pixel sizes of an X11 bitmap face (bold must exist when asked)."""
    sizes = []
    for pixels in X11_PIXEL_SIZES:
        if not resolves(family, -pixels, probe):
            continue
        if bold and not resolves(family, -pixels, probe, 'bold'):
            continue
        sizes.append(pixels)
    return sizes


def pick_bitmap(families, probe, bold=False):
    """First X11 bitmap face with usable native sizes, else (None, [])."""
    for family in families:
        sizes = bitmap_sizes(family, probe, bold)
        if sizes:
            return family, sizes
    return None, []


class Typography:
    """Resolved families plus the font tuples the widgets should use."""

    def __init__(self, root, widget_size=WIDGET_SIZE, title_size=TITLE_SIZE):
        self.root = root
        self.widget = widget_size
        self.title = title_size
        self._probe = tkfont.Font(family='TkDefaultFont', size=widget_size)
        self.antialiased = pick_with_bold(NATIVE_SANS_FAMILIES, widget_size, self._probe) is not None
        self._sans_sizes = self._mono_sizes = None
        self.base_pixels = self.title_pixels = None
        if self.antialiased:
            self.sans = (pick_with_bold(NATIVE_SANS_FAMILIES, widget_size, self._probe) or
                         pick(NATIVE_SANS_FAMILIES, widget_size, self._probe) or
                         tkfont.nametofont('TkDefaultFont').actual()['family'])
            self.mono = (pick(NATIVE_MONO_FAMILIES, widget_size, self._probe) or
                         tkfont.nametofont('TkFixedFont').actual()['family'])
        else:
            self.sans, self._sans_sizes = pick_bitmap(X11_SANS_FAMILIES, self._probe, bold=True)
            self.mono, self._mono_sizes = pick_bitmap(X11_MONO_FAMILIES, self._probe)
            self.sans = self.sans or tkfont.nametofont('TkDefaultFont').actual()['family']
            self.mono = self.mono or tkfont.nametofont('TkFixedFont').actual()['family']
            if self._sans_sizes:
                self.base_pixels = self._nearest(self._sans_sizes, X11_BASE_PIXELS)
                self.title_pixels = self._nearest(self._sans_sizes, X11_TITLE_PIXELS)

    @staticmethod
    def _nearest(sizes, wanted):
        return min(sizes, key=lambda px: (abs(px - wanted), px))

    def _size(self, size, sizes):
        """Map a layout point size to a native pixel size in the X11 world."""
        if sizes is None:
            return self.widget if size is None else size
        wanted = self.base_pixels if size is None else size * self.base_pixels / float(self.widget)
        return -self._nearest(sizes, int(round(wanted)))

    def sans_font(self, size=None, weight='normal'):
        return (self.sans, self._size(size, self._sans_sizes), weight)

    def mono_font(self, size=None, weight='normal'):
        return (self.mono, self._size(size, self._mono_sizes), weight)

    def title_font(self):
        """Section titles: the largest sensible native size, or points."""
        if self._sans_sizes:
            return (self.sans, -self.title_pixels, 'bold')
        return (self.sans, self.title, 'bold')

    def measure(self, text, size=None, mono=False):
        """Pixel width of `text` in the resolved face (layout, not guessing)."""
        font = tkfont.Font(font=self.mono_font(size) if mono else self.sans_font(size))
        return font.measure(text)

    def linespace(self, size=None, mono=True):
        """Row height of the resolved face, for collision-free tick spacing."""
        font = tkfont.Font(font=self.mono_font(size) if mono else self.sans_font(size))
        return font.metrics('linespace')

    def configure_named(self):
        """Keep ttk internals, menus and dialogs in the same face."""
        small = max(9, self.widget - 1)
        settings = {'TkDefaultFont': self.sans_font(),
                    'TkTextFont': self.sans_font(),
                    'TkHeadingFont': self.sans_font(weight='bold'),
                    'TkMenuFont': self.sans_font(),
                    'TkIconFont': self.sans_font(),
                    'TkCaptionFont': self.sans_font(small, 'bold'),
                    'TkTooltipFont': self.sans_font(small),
                    'TkSmallCaptionFont': self.sans_font(small),
                    'TkFixedFont': self.mono_font()}
        for name, spec in settings.items():
            try:
                family, size, weight = spec
                tkfont.nametofont(name).configure(family=family, size=size, weight=weight)
            except Exception:  # a stripped Tk may not define every named font
                continue

    def configure_styles(self, style):
        """Explicit fonts for the ttk styles this GUI uses."""
        for name in ('TLabel', 'TButton', 'TCheckbutton', 'TRadiobutton', 'TEntry',
                     'TCombobox', 'TMenubutton', 'TLabelframe.Label', 'TNotebook.Tab'):
            style.configure(name, font=self.sans_font())
        style.configure('Title.TLabel', font=self.title_font())
        return style


def apply(root, style=None, widget_size=WIDGET_SIZE, title_size=TITLE_SIZE):
    """Resolve the families, install them, and return the Typography."""
    typography = Typography(root, widget_size, title_size)
    typography.configure_named()
    if style is not None:
        typography.configure_styles(style)
    return typography
