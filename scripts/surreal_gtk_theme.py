#!/usr/bin/env python3
"""
Shared High-Contrast Black & Gold / Yellow GTK3 Theme for AION Surreal 3D GUIs.
Overrides system light-theme gradients (e.g. Adwaita white buttons/comboboxes)
using explicit `background-image: none;` and child `label` color rules at
`Gtk.STYLE_PROVIDER_PRIORITY_USER`.
"""

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, Gtk

PALETTE = {
    "bg_window": "#070709",
    "bg_card": "#0e0d09",
    "bg_control": "#14120a",
    "bg_hover": "#2a220b",
    "bg_entry": "#0a0a0d",
    "border_subtle": "#5e4914",
    "border_gold": "#d4a61c",
    "gold": "#ffd700",
    "yellow": "#ffe875",
    "ivory": "#f7f0d4",
    "amber_soft": "#e5ca6b",
    "green_ok": "#5cff8d",
    "red_err": "#ff6b6b",
    "warn_orange": "#ffb347",
    "black_ink": "#070709",
}

CSS_DATA = b"""
window,
window.background,
dialog,
dialog.background,
messagedialog,
filechooserdialog {
    background-color: #070709;
    background-image: none;
    color: #f7f0d4;
    font-family: 'Monospace', 'DejaVu Sans Mono', sans-serif;
    font-size: 13px;
}

window:backdrop,
dialog:backdrop {
    background-color: #070709;
    color: #f7f0d4;
}

scrolledwindow,
scrolledwindow viewport,
viewport {
    background-color: #070709;
    background-image: none;
    border: none;
}

headerbar,
headerbar.titlebar {
    background-color: #050507;
    background-image: none;
    border-bottom: 2px solid #ffd700;
    color: #ffd700;
}

label {
    color: #f7f0d4;
}

.cyber-card {
    background-color: #0e0d09;
    background-image: none;
    border: 1px solid #6b5316;
    border-radius: 6px;
    padding: 10px 14px;
    margin-bottom: 6px;
}

.cyber-title {
    color: #ffd700;
    font-weight: bold;
    font-size: 12px;
    margin-bottom: 4px;
}

/* Base button rule: override Adwaita white gradient everywhere */
button {
    background-image: none;
    background-color: #14120a;
    color: #ffe875;
    border: 1px solid #d4a61c;
    border-radius: 4px;
    box-shadow: none;
    text-shadow: none;
    -gtk-icon-shadow: none;
}

button label {
    color: #ffe875;
}

button:hover {
    background-image: none;
    background-color: #2a220b;
    color: #ffffff;
    border-color: #ffd700;
}

button:hover label {
    color: #ffffff;
}

button:active,
button:checked {
    background-image: none;
    background-color: #ffd700;
    color: #070709;
    border-color: #ffe875;
}

button:active label,
button:checked label {
    color: #070709;
}

button:disabled {
    background-image: none;
    background-color: #100f0b;
    color: #8c7634;
    border-color: #4a3d17;
}

button:disabled label {
    color: #8c7634;
}

/* Primary Gold CTA */
button.btn-generate {
    background-image: linear-gradient(135deg, #ffd700 0%, #f0b400 100%);
    background-color: #ffd700;
    color: #070709;
    border: 2px solid #fff099;
    border-radius: 6px;
    padding: 12px 18px;
    font-weight: bold;
    font-size: 14px;
}

button.btn-generate label {
    color: #070709;
    font-weight: bold;
}

button.btn-generate:hover {
    background-image: linear-gradient(135deg, #ffea61 0%, #ffd700 100%);
    border-color: #ffffff;
    color: #000000;
}

button.btn-generate:hover label {
    color: #000000;
}

button.btn-generate:disabled {
    background-image: none;
    background-color: #3a300e;
    border-color: #705b1b;
    color: #a88f44;
}

button.btn-generate:disabled label {
    color: #a88f44;
}

/* Secondary Black-and-Gold Highlight Buttons */
button.btn-ai-isolate,
button.btn-auto-balance {
    background-image: linear-gradient(135deg, #1b1506 0%, #3a2d08 100%);
    background-color: #241c07;
    color: #ffe875;
    border: 1.5px solid #ffd700;
    border-radius: 5px;
    padding: 9px 14px;
    font-size: 13px;
    font-weight: bold;
}

button.btn-ai-isolate label,
button.btn-auto-balance label {
    color: #ffe875;
    font-weight: bold;
}

button.btn-ai-isolate:hover,
button.btn-auto-balance:hover {
    background-image: linear-gradient(135deg, #3a2d08 0%, #5c470b 100%);
    border-color: #fff2a6;
    color: #ffffff;
}

button.btn-ai-isolate:hover label,
button.btn-auto-balance:hover label {
    color: #ffffff;
}

button.btn-pick,
button.btn-preset {
    background-image: none;
    background-color: #16130a;
    color: #ffe875;
    border: 1px solid #d4a61c;
    border-radius: 4px;
    padding: 6px 12px;
    font-size: 11px;
    font-weight: bold;
}

button.btn-pick label,
button.btn-preset label {
    color: #ffe875;
}

button.btn-pick:hover,
button.btn-preset:hover {
    background-image: none;
    background-color: #ffd700;
    color: #070709;
    border-color: #fff099;
}

button.btn-pick:hover label,
button.btn-preset:hover label {
    color: #070709;
}

button.btn-cancel,
button.btn-reset,
button.btn-close {
    background-image: none;
    background-color: #14120b;
    color: #ffe066;
    border: 1px solid #b58c17;
    border-radius: 4px;
    padding: 7px 14px;
    font-weight: bold;
}

button.btn-cancel label,
button.btn-reset label,
button.btn-close label {
    color: #ffe066;
}

button.btn-cancel:hover,
button.btn-reset:hover {
    background-image: none;
    background-color: #2b1616;
    color: #ff8a8a;
    border-color: #ff6b6b;
}

button.btn-cancel:hover label,
button.btn-reset:hover label {
    color: #ff8a8a;
}

button.btn-close:hover {
    background-image: none;
    background-color: #ffd700;
    color: #070709;
    border-color: #fff099;
}

button.btn-close:hover label {
    color: #070709;
}

/* ComboBoxes & Popup Dropdown Menus (kills Adwaita white backgrounds) */
combobox,
combobox > box,
combobox box.linked {
    background-image: none;
    background-color: transparent;
    border: none;
    padding: 0;
    box-shadow: none;
}

combobox button,
combobox button.combo {
    background-image: none;
    background-color: #14120a;
    color: #ffe875;
    border: 1px solid #d4a61c;
    border-radius: 4px;
    padding: 5px 10px;
    box-shadow: none;
    text-shadow: none;
}

combobox button:hover,
combobox button.combo:hover {
    background-image: none;
    background-color: #2a220b;
    border-color: #ffd700;
    color: #ffffff;
}

combobox button label,
combobox cellview,
combobox arrow {
    color: #ffe875;
}

combobox button:hover label,
combobox button:hover cellview,
combobox button:hover arrow {
    color: #ffffff;
}

menu,
.menu,
.context-menu,
combobox window.popup,
combobox window.popup > menu,
popover,
popover.background {
    background-image: none;
    background-color: #0e0d08;
    color: #ffe875;
    border: 1px solid #d4a61c;
}

menu arrow,
menu > arrow,
.menu arrow,
combobox window.popup arrow,
popover arrow {
    background-image: none;
    background-color: #14120a;
    color: #ffd700;
    border: none;
    min-height: 16px;
}

menuitem {
    background-image: none;
    background-color: #0e0d08;
    color: #ffe875;
    padding: 6px 10px;
    border: 1px solid transparent;
}

menuitem *,
menuitem label,
menuitem cellview {
    color: #ffe875;
}

menuitem:hover,
menuitem:focus,
menuitem:selected {
    background-image: none;
    background-color: #2e2408;
    color: #ffffff;
    border: 1px solid #ffd700;
}

menuitem:hover *,
menuitem:hover label,
menuitem:hover cellview,
menuitem:focus *,
menuitem:focus label,
menuitem:selected *,
menuitem:selected label {
    color: #ffffff;
    font-weight: bold;
}

/* Token / API Key Entries */
entry {
    background-image: none;
    background-color: #09090b;
    color: #ffe875;
    caret-color: #ffd700;
    border: 1px solid #8c6b15;
    border-radius: 4px;
    padding: 5px 8px;
    box-shadow: none;
}

entry:focus {
    background-color: #12100a;
    border-color: #ffd700;
    color: #fff4b8;
}

entry selection {
    background-color: #ffd700;
    color: #070709;
}

/* Links */
button.link,
button.link:visited,
link,
*:link {
    background-image: none;
    background-color: #14120a;
    color: #ffd700;
    border: 1px solid #6b5316;
    border-radius: 4px;
    padding: 4px 8px;
    box-shadow: none;
    text-shadow: none;
}

button.link label {
    color: #ffd700;
    text-decoration: underline;
}

button.link:hover {
    background-image: none;
    background-color: #2a220b;
    border-color: #ffd700;
    color: #fff4b8;
}

button.link:hover label {
    color: #fff4b8;
}

/* Expander */
expander title {
    color: #ffd700;
    font-weight: bold;
    background-image: none;
    border-radius: 4px;
    padding: 3px 4px;
}

expander title label,
expander arrow {
    color: #ffd700;
}

expander title:hover {
    background-color: #1e190a;
    color: #fff4b8;
}

/* Scales / Sliders */
scale {
    margin: 2px 0px;
}

scale trough {
    background-image: none;
    background-color: #1a170d;
    border: 1px solid #5e4914;
    border-radius: 4px;
    min-height: 6px;
}

scale highlight {
    background-image: none;
    background-color: #ffd700;
    border-radius: 4px;
}

scale slider {
    background-image: none;
    background-color: #ffe875;
    min-width: 16px;
    min-height: 16px;
    border-radius: 8px;
    border: 2px solid #070709;
    box-shadow: 0 0 0 1px #ffd700;
}

scale slider:hover {
    background-color: #ffffff;
}

/* Checkbuttons */
checkbutton {
    color: #f7f0d4;
    font-size: 12px;
    padding: 3px 0;
}

checkbutton label {
    color: #f7f0d4;
}

checkbutton check {
    background-image: none;
    background-color: #12100a;
    border: 1.5px solid #d4a61c;
    border-radius: 4px;
    color: #070709;
    min-width: 15px;
    min-height: 15px;
    -gtk-icon-shadow: none;
}

checkbutton check:checked {
    background-image: none;
    background-color: #ffd700;
    border-color: #ffe875;
    color: #070709;
}

/* ProgressBar, Separators, Scrollbars, FileChooser Lists */
progressbar trough {
    background-image: none;
    background-color: #12100a;
    border: 1px solid #6b5316;
    border-radius: 4px;
    min-height: 14px;
}

progressbar progress {
    background-image: linear-gradient(to right, #d49b00, #ffd700, #ffe875);
    background-color: #ffd700;
    border-radius: 4px;
}

separator {
    background-color: #4d3c10;
    min-height: 1px;
}

scrollbar,
scrollbar trough {
    background-image: none;
    background-color: #0a0907;
}

scrollbar slider {
    background-image: none;
    background-color: #7d6116;
    border-radius: 4px;
    min-width: 8px;
}

scrollbar slider:hover {
    background-color: #ffd700;
}

treeview.view,
list,
list row,
placessidebar row {
    background-image: none;
    background-color: #0c0b08;
    color: #f7f0d4;
}

treeview.view:selected,
list row:selected,
placessidebar row:selected {
    background-image: none;
    background-color: #ffd700;
    color: #070709;
}

tooltip,
tooltip.background {
    background-image: none;
    background-color: #14120a;
    color: #ffe875;
    border: 1px solid #ffd700;
}
"""


def apply_black_gold_theme():
    """Force dark GTK theme preference and install high-priority Black & Gold CSS."""
    gtk_settings = Gtk.Settings.get_default()
    if gtk_settings is not None:
        try:
            gtk_settings.set_property("gtk-application-prefer-dark-theme", True)
        except Exception:
            pass

    css_provider = Gtk.CssProvider()
    css_provider.load_from_data(CSS_DATA)
    screen = Gdk.Screen.get_default()
    if screen is not None:
        Gtk.StyleContext.add_provider_for_screen(
            screen,
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_USER,
        )
    return css_provider
