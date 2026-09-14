"""Small portable Tk containers shared by the GUI pages.

`ScrollArea` is a vertically scrolling region whose scrollbar appears only
when the content is taller than the window. Wheel handling covers X11
(Button-4/5), Windows and macOS (MouseWheel with a 120-unit delta), and the
arrow/page keys work while the region has focus, so the same code behaves
correctly on every platform Tk supports.
"""
import tkinter as tk
from tkinter import ttk


class ScrollArea(ttk.Frame):
    """A frame that scrolls vertically; the inner `body` holds the content."""

    def __init__(self, parent, width=380, background='#101820', increment=24, **kwargs):
        super().__init__(parent, **kwargs)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        self.canvas = tk.Canvas(self, width=width, bg=background, highlightthickness=0,
                                takefocus=True, yscrollincrement=increment)
        self.scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self._sync_bar)
        self.canvas.grid(row=0, column=0, sticky='nsew')
        self.body = ttk.Frame(self.canvas)
        self._window = self.canvas.create_window((0, 0), window=self.body, anchor='nw')
        self.body.bind('<Configure>', self._content_changed)
        self.canvas.bind('<Configure>', self._viewport_changed)
        self.canvas.bind('<Button-1>', lambda _: self.canvas.focus_set())
        for sequence, units in (('<Up>', -1), ('<Down>', 1),
                                ('<Prior>', -6), ('<Next>', 6)):
            self.canvas.bind(sequence, lambda _, u=units: self._key_scroll(u))
        self.canvas.bind('<Home>', lambda _: self._key_scroll(0.0))
        self.canvas.bind('<End>', lambda _: self._key_scroll(1.0))
        # One application-wide wheel binding; each area scrolls only when the
        # pointer is really inside it, so nested areas stay independent.
        top = self.winfo_toplevel()
        for sequence in ('<MouseWheel>', '<Button-4>', '<Button-5>'):
            top.bind_all(sequence, self._wheel, add='+')

    # -- geometry ---------------------------------------------------------
    def _sync_bar(self, first, last):
        """ttk scrollbar placement; hidden while everything fits."""
        if float(first) <= 0.0 and float(last) >= 1.0:
            self.scrollbar.grid_remove()
        else:
            self.scrollbar.grid(row=0, column=1, sticky='ns')
        self.scrollbar.set(first, last)

    def _content_changed(self, _=None):
        self.canvas.configure(scrollregion=self.canvas.bbox('all'))

    def _viewport_changed(self, event):
        self.canvas.itemconfigure(self._window, width=max(1, event.width))
        self._content_changed()
        # Re-evaluate the scrollbar against the new viewport height: resizing
        # does not always make the canvas report its scroll position again.
        self._sync_bar(*self.canvas.yview())

    # -- scrolling --------------------------------------------------------
    def scroll_units(self, units):
        self.canvas.yview_scroll(units, 'units')

    def reveal(self, widget):
        """Scroll the minimum distance that brings `widget` into view."""
        increment = max(1, int(self.canvas.cget('yscrollincrement')))
        top = widget.winfo_rooty() - self.canvas.winfo_rooty()
        view = self.viewport_height()
        if 0 <= top < view:
            return False
        steps = int(top / increment)
        self.scroll_units(steps if steps else (1 if top > 0 else -1))
        return True

    def _key_scroll(self, units):
        if isinstance(units, float):
            self.canvas.yview_moveto(units)   # Home/End
        else:
            self.scroll_units(units)
        return 'break'

    def _wheel(self, event):
        if not self._pointer_inside(event):
            return None
        number = getattr(event, 'num', None)
        if number in (4, 5):
            steps = -1 if number == 4 else 1
        else:
            delta = getattr(event, 'delta', 0)
            if not delta:
                return None
            steps = -1 if delta > 0 else 1
            if abs(delta) >= 120:  # Windows reports multiples of 120
                steps *= max(1, abs(delta) // 120)
        self.scroll_units(steps * 3)
        return 'break'

    def _pointer_inside(self, event):
        try:
            widget = self.winfo_containing(event.x_root, event.y_root)
        except tk.TclError:  # this area belongs to a window that is gone
            return False
        while widget is not None:
            if widget is self:
                return True
            widget = getattr(widget, 'master', None)
        return False

    # -- inspection (also used by the Tk test) ----------------------------
    def content_height(self):
        box = self.canvas.bbox('all')
        return 0 if box is None else box[3] - box[1]

    def viewport_height(self):
        return self.canvas.winfo_height()

    def overflowing(self):
        return self.content_height() > self.viewport_height()

    def showing_scrollbar(self):
        return bool(self.scrollbar.winfo_ismapped())

    def visible(self, widget):
        """True when any part of the widget is inside the viewport."""
        top = widget.winfo_rooty() - self.canvas.winfo_rooty()
        return top + widget.winfo_height() > 0 and top < self.viewport_height()
