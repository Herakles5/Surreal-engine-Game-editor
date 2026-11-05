#!/usr/bin/env python3
"""
AION SURREAL 3D ENGINE - LIVE GRAPHICS & ENGINE SETTINGS POPUP (Native Linux GTK3)
Provides instant real-time tuning for Lighting, Shaders, Shadows, FOV, and Hardware Auto-Balance (30-60 FPS).
Communicates bi-directionally with Surreal Studio via /tmp/surreal_runtime_settings.json
"""

import sys
import os
import json
import time

# Set display if missing
if "DISPLAY" not in os.environ:
    os.environ["DISPLAY"] = ":0"
os.environ["GTK_A11Y"] = "none"

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
from gi.repository import Gtk, Gdk, GLib

SETTINGS_FILE = "/tmp/surreal_runtime_settings.json"

DEFAULT_SETTINGS = {
    "backend": "GPU_OPENGL",
    "sky_profile": 0,
    "shader_quality": 1,        # 0: Fast (30-60+ FPS), 1: Balanced, 2: Ultra
    "point_light_count": 4,     # 0, 2, 4, 8
    "sun_intensity": 1.0,       # 0.0 - 3.0
    "ambient_intensity": 1.0,   # 0.0 - 2.5
    "point_intensity": 1.0,     # 0.0 - 3.0
    "wetness": 0.75,            # 0.0 - 1.0
    "glow_intensity": 1.30,     # 0.2 - 2.5
    "fov": 65.0,                # 45.0 - 105.0 degrees
    "shadows": True,
    "shadow_opacity": 0.85,     # 0.1 - 1.0
    "current_fps": 30.0,
    "action": ""
}

CSS_DATA = b"""
window {
    background-color: #070b14;
    color: #dce3ed;
    font-family: 'Monospace', 'DejaVu Sans Mono', sans-serif;
    font-size: 13px;
}
headerbar {
    background: #04070e;
    border-bottom: 2px solid #00ffcc;
    color: #00ffcc;
}
.cyber-card {
    background-color: #0b1322;
    border: 1px solid #16263e;
    border-radius: 6px;
    padding: 10px 14px;
    margin-bottom: 6px;
}
.cyber-title {
    color: #00ffcc;
    font-weight: bold;
    font-size: 12px;
    margin-bottom: 4px;
}
.btn-auto-balance {
    background: linear-gradient(135deg, #0b2f38 0%, #1a1538 100%);
    color: #00ffcc;
    border: 2px solid #00ffcc;
    border-radius: 6px;
    padding: 10px 16px;
    font-weight: bold;
    font-size: 14px;
}
.btn-auto-balance:hover {
    background: linear-gradient(135deg, #124d5c 0%, #2e2463 100%);
    border-color: #ff007f;
    color: #ffffff;
}
.btn-preset {
    background-color: #111a2a;
    color: #88bbdd;
    border: 1px solid #223854;
    border-radius: 4px;
    padding: 5px 10px;
    font-size: 11px;
}
.btn-preset:hover {
    background-color: #1a2c48;
    color: #00ffcc;
    border-color: #00ffcc;
}
scale {
    margin: 2px 0px;
}
scale highlight {
    background-color: #00ffcc;
}
scale slider {
    background-color: #ff007f;
    min-width: 16px;
    min-height: 16px;
    border-radius: 8px;
    border: 1px solid #ffffff;
}
combobox button {
    background-color: #0f1c2e;
    color: #00ffcc;
    border: 1px solid #00ffcc;
    border-radius: 4px;
    padding: 3px 8px;
}
combobox button:hover {
    background-color: #182d4a;
    border-color: #ff007f;
    color: #ff007f;
}
checkbutton check {
    background-color: #0f1c2e;
    border: 1px solid #00ffcc;
    border-radius: 3px;
    min-width: 15px;
    min-height: 15px;
}
checkbutton check:checked {
    background-color: #00ffcc;
}
.btn-reset {
    background-color: #1c1420;
    color: #ffaa00;
    border: 1px solid #ffaa00;
    border-radius: 4px;
    padding: 6px 14px;
    font-weight: bold;
}
.btn-reset:hover {
    background-color: #331e3d;
    border-color: #ff007f;
    color: #ff007f;
}
.btn-close {
    background-color: #0e222d;
    color: #00ffcc;
    border: 1px solid #00ffcc;
    border-radius: 4px;
    padding: 6px 16px;
    font-weight: bold;
}
.btn-close:hover {
    background-color: #183e54;
    border-color: #ffffff;
    color: #ffffff;
}
"""

class SurrealSettingsWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="⚙️ AION Surreal 3D // Graphics, Lighting & Hardware Balancer")
        self.set_default_size(560, 720)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_keep_above(True)
        self.set_border_width(12)

        # Apply CSS Styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(CSS_DATA)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        self.settings = self.load_settings()
        self.is_updating_ui = True
        self.last_file_mtime = 0

        # Scrollable container for all controls
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.add(scroller)

        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        scroller.add(main_box)

        # Header Title
        header_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_title = Gtk.Label()
        lbl_title.set_markup("<span size='large' weight='bold' color='#00ffcc'>⚙️ AION SURREAL 3D // GRAPHICS &amp; ENGINE SETTINGS</span>")
        header_box.pack_start(lbl_title, False, False, 0)
        main_box.pack_start(header_box, False, False, 0)

        # -------------------------------------------------------------
        # CARD 0: 🚀 AUTO-BALANCE (30 - 60 FPS) & HARDWARE PRESETS
        # -------------------------------------------------------------
        card_auto = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_auto.get_style_context().add_class("cyber-card")

        ca_title = Gtk.Label()
        ca_title.set_markup("<span class='cyber-title'>[ 🚀 HARDWARE OPTIMIZATION &amp; AUTO-BALANCE ]</span>")
        ca_title.set_xalign(0.0)
        card_auto.pack_start(ca_title, False, False, 0)

        self.btn_auto = Gtk.Button(label="🚀 AUTO-BALANCE: 30 - 60 FPS (Hardware Tuning)")
        self.btn_auto.get_style_context().add_class("btn-auto-balance")
        self.btn_auto.connect("clicked", self.on_auto_balance_clicked)
        card_auto.pack_start(self.btn_auto, False, False, 0)

        # Preset Buttons Row
        preset_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        btn_p_max = Gtk.Button(label="🏎️ Max FPS (60+ FPS)")
        btn_p_max.get_style_context().add_class("btn-preset")
        btn_p_max.connect("clicked", lambda b: self.apply_preset("max_fps"))
        preset_box.pack_start(btn_p_max, True, True, 0)

        btn_p_bal = Gtk.Button(label="⚖️ Balanced (Default)")
        btn_p_bal.get_style_context().add_class("btn-preset")
        btn_p_bal.connect("clicked", lambda b: self.apply_preset("balanced"))
        preset_box.pack_start(btn_p_bal, True, True, 0)

        btn_p_ult = Gtk.Button(label="💎 Ultra Cinematic")
        btn_p_ult.get_style_context().add_class("btn-preset")
        btn_p_ult.connect("clicked", lambda b: self.apply_preset("ultra"))
        preset_box.pack_start(btn_p_ult, True, True, 0)

        card_auto.pack_start(preset_box, False, False, 2)

        # Live FPS Telemetry line
        self.lbl_fps_telemetry = Gtk.Label()
        fps_val = self.settings.get("current_fps", 30.0)
        self.lbl_fps_telemetry.set_markup(f"<span size='small' color='#00ff88'>● Live Framerate: <b>{fps_val:.1f} FPS</b></span> | <span size='small' color='#88aacc'>Target Range: 30 - 60 FPS</span>")
        self.lbl_fps_telemetry.set_xalign(0.0)
        card_auto.pack_start(self.lbl_fps_telemetry, False, False, 0)

        main_box.pack_start(card_auto, False, False, 0)

        # -------------------------------------------------------------
        # CARD 1: 💡 LIGHTING & SUN / AMBIENT CONTROLS
        # -------------------------------------------------------------
        card_light = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_light.get_style_context().add_class("cyber-card")

        cl_title = Gtk.Label()
        cl_title.set_markup("<span class='cyber-title'>[ 💡 LIGHTING &amp; SUN / AMBIENT CONTROLS ]</span>")
        cl_title.set_xalign(0.0)
        card_light.pack_start(cl_title, False, False, 0)

        # 1. Sun / Moon Direct Light
        row_sun = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_sun = Gtk.Label(label="Direct Sun / Moon Light (Intensity):")
        lbl_sun.set_xalign(0.0)
        row_sun.pack_start(lbl_sun, True, True, 0)
        self.lbl_sun_val = Gtk.Label()
        sun_pct = int(self.settings.get("sun_intensity", 1.0) * 100)
        self.lbl_sun_val.set_markup(f"<span color='#ffcc00'>{sun_pct}%</span>")
        row_sun.pack_end(self.lbl_sun_val, False, False, 0)
        card_light.pack_start(row_sun, False, False, 0)

        self.scale_sun = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.0, 300.0, 5.0)
        self.scale_sun.set_value(self.settings.get("sun_intensity", 1.0) * 100.0)
        self.scale_sun.set_draw_value(False)
        self.scale_sun.connect("value-changed", self.on_sun_changed)
        card_light.pack_start(self.scale_sun, False, False, 0)

        # 2. Ambient Light
        row_amb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_amb = Gtk.Label(label="Ambient Fill Light (Sky Brightness):")
        lbl_amb.set_xalign(0.0)
        row_amb.pack_start(lbl_amb, True, True, 0)
        self.lbl_amb_val = Gtk.Label()
        amb_pct = int(self.settings.get("ambient_intensity", 1.0) * 100)
        self.lbl_amb_val.set_markup(f"<span color='#00e5ff'>{amb_pct}%</span>")
        row_amb.pack_end(self.lbl_amb_val, False, False, 0)
        card_light.pack_start(row_amb, False, False, 0)

        self.scale_amb = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.0, 250.0, 5.0)
        self.scale_amb.set_value(self.settings.get("ambient_intensity", 1.0) * 100.0)
        self.scale_amb.set_draw_value(False)
        self.scale_amb.connect("value-changed", self.on_amb_changed)
        card_light.pack_start(self.scale_amb, False, False, 0)

        # 3. Active Point Lights Count & Intensity
        row_pl = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_pl = Gtk.Label(label="Active Streetlights &amp; Neon Sources:")
        lbl_pl.set_xalign(0.0)
        row_pl.pack_start(lbl_pl, True, True, 0)

        self.cb_point_count = Gtk.ComboBoxText()
        self.cb_point_count.append("0", "0 Lights (Max FPS)")
        self.cb_point_count.append("2", "2 Lights (Fast)")
        self.cb_point_count.append("4", "4 Lights (Default)")
        self.cb_point_count.append("8", "8 Lights (Maximum)")
        cur_plc = str(self.settings.get("point_light_count", 4))
        self.cb_point_count.set_active_id(cur_plc)
        self.cb_point_count.connect("changed", self.on_setting_changed)
        row_pl.pack_end(self.cb_point_count, False, False, 0)
        card_light.pack_start(row_pl, False, False, 0)

        main_box.pack_start(card_light, False, False, 0)

        # -------------------------------------------------------------
        # CARD 2: 🔮 SHADER & PBR EFFECTS
        # -------------------------------------------------------------
        card_shader = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_shader.get_style_context().add_class("cyber-card")

        cs_title = Gtk.Label()
        cs_title.set_markup("<span class='cyber-title'>[ 🔮 SHADER &amp; PBR EFFECTS ]</span>")
        cs_title.set_xalign(0.0)
        card_shader.pack_start(cs_title, False, False, 0)

        # Shader Detail Level
        row_sq = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_sq = Gtk.Label(label="PBR Shader Quality:")
        lbl_sq.set_xalign(0.0)
        row_sq.pack_start(lbl_sq, True, True, 0)

        self.cb_shader_qual = Gtk.ComboBoxText()
        self.cb_shader_qual.append("0", "⚡ Fast (Max FPS)")
        self.cb_shader_qual.append("1", "⚖️ Balanced PBR")
        self.cb_shader_qual.append("2", "💎 Ultra (Cinematic Waves)")
        cur_sq = str(self.settings.get("shader_quality", 1))
        self.cb_shader_qual.set_active_id(cur_sq)
        self.cb_shader_qual.connect("changed", self.on_setting_changed)
        row_sq.pack_end(self.cb_shader_qual, False, False, 0)
        card_shader.pack_start(row_sq, False, False, 0)

        # Wetness Slider
        wet_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_wet = Gtk.Label(label="Puddle Reflections &amp; Wetness:")
        lbl_wet.set_xalign(0.0)
        wet_box.pack_start(lbl_wet, True, True, 0)
        self.lbl_wet_val = Gtk.Label()
        wet_pct = int(self.settings.get("wetness", 0.75) * 100)
        self.lbl_wet_val.set_markup(f"<span color='#00ffcc'>{wet_pct}%</span>")
        wet_box.pack_end(self.lbl_wet_val, False, False, 0)
        card_shader.pack_start(wet_box, False, False, 0)

        self.scale_wet = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.0, 100.0, 1.0)
        self.scale_wet.set_value(self.settings.get("wetness", 0.75) * 100.0)
        self.scale_wet.set_draw_value(False)
        self.scale_wet.connect("value-changed", self.on_wetness_changed)
        card_shader.pack_start(self.scale_wet, False, False, 0)

        # Neon Glow Slider
        glow_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_glow = Gtk.Label(label="Neon &amp; Window Glow Intensity:")
        lbl_glow.set_xalign(0.0)
        glow_box.pack_start(lbl_glow, True, True, 0)
        self.lbl_glow_val = Gtk.Label()
        glow_pct = int(self.settings.get("glow_intensity", 1.30) * 100)
        self.lbl_glow_val.set_markup(f"<span color='#ff007f'>{glow_pct}%</span>")
        glow_box.pack_end(self.lbl_glow_val, False, False, 0)
        card_shader.pack_start(glow_box, False, False, 0)

        self.scale_glow = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 20.0, 250.0, 5.0)
        self.scale_glow.set_value(self.settings.get("glow_intensity", 1.30) * 100.0)
        self.scale_glow.set_draw_value(False)
        self.scale_glow.connect("value-changed", self.on_glow_changed)
        card_shader.pack_start(self.scale_glow, False, False, 0)

        main_box.pack_start(card_shader, False, False, 0)

        # -------------------------------------------------------------
        # CARD 3: 👥 SHADOWS
        # -------------------------------------------------------------
        card_shad = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_shad.get_style_context().add_class("cyber-card")

        csh_title = Gtk.Label()
        csh_title.set_markup("<span class='cyber-title'>[ 👥 REAL-TIME SHADOWS ]</span>")
        csh_title.set_xalign(0.0)
        card_shad.pack_start(csh_title, False, False, 0)

        self.chk_shadows = Gtk.CheckButton(label="Enable Real-Time Ground Contact Shadows")
        self.chk_shadows.set_active(self.settings.get("shadows", True))
        self.chk_shadows.connect("toggled", self.on_setting_changed)
        card_shad.pack_start(self.chk_shadows, False, False, 0)

        row_shado = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_shado = Gtk.Label(label="Shadow Opacity &amp; Depth:")
        lbl_shado.set_xalign(0.0)
        row_shado.pack_start(lbl_shado, True, True, 0)
        self.lbl_shado_val = Gtk.Label()
        so_pct = int(self.settings.get("shadow_opacity", 0.85) * 100)
        self.lbl_shado_val.set_markup(f"<span color='#00ff88'>{so_pct}%</span>")
        row_shado.pack_end(self.lbl_shado_val, False, False, 0)
        card_shad.pack_start(row_shado, False, False, 0)

        self.scale_shadow_op = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 10.0, 100.0, 5.0)
        self.scale_shadow_op.set_value(self.settings.get("shadow_opacity", 0.85) * 100.0)
        self.scale_shadow_op.set_draw_value(False)
        self.scale_shadow_op.connect("value-changed", self.on_shadow_op_changed)
        card_shad.pack_start(self.scale_shadow_op, False, False, 0)

        main_box.pack_start(card_shad, False, False, 0)

        # -------------------------------------------------------------
        # CARD 4: 🎥 CAMERA & ATMOSPHERE
        # -------------------------------------------------------------
        card_cam = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_cam.get_style_context().add_class("cyber-card")

        cc_title = Gtk.Label()
        cc_title.set_markup("<span class='cyber-title'>[ 🎥 CAMERA &amp; ATMOSPHERE ]</span>")
        cc_title.set_xalign(0.0)
        card_cam.pack_start(cc_title, False, False, 0)

        # Backend selector
        row_be = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_be = Gtk.Label(label="Engine Backend:")
        lbl_be.set_xalign(0.0)
        row_be.pack_start(lbl_be, True, True, 0)

        self.cb_backend = Gtk.ComboBoxText()
        self.cb_backend.append("GPU_OPENGL", "⚡ GPU OpenGL 4.6 (Hardware)")
        self.cb_backend.append("CPU_AVX2", "🛡️ CPU Bare-Metal (AVX2 Sovereign)")
        if self.settings.get("backend") == "CPU_AVX2":
            self.cb_backend.set_active_id("CPU_AVX2")
        else:
            self.cb_backend.set_active_id("GPU_OPENGL")
        self.cb_backend.connect("changed", self.on_setting_changed)
        row_be.pack_end(self.cb_backend, False, False, 0)
        card_cam.pack_start(row_be, False, False, 0)

        # Sky Profile
        row_sky = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_sky = Gtk.Label(label="Sky &amp; Atmosphere Profile:")
        lbl_sky.set_xalign(0.0)
        row_sky.pack_start(lbl_sky, True, True, 0)

        self.cb_sky = Gtk.ComboBoxText()
        self.cb_sky.append("0", "🌃 Cyberpunk Night")
        self.cb_sky.append("1", "🌇 Cyber Sunset")
        self.cb_sky.append("2", "🌌 Deep Space Stars")
        self.cb_sky.append("3", "🌄 Morning Dawn")
        cur_sky = str(self.settings.get("sky_profile", 0))
        self.cb_sky.set_active_id(cur_sky)
        self.cb_sky.connect("changed", self.on_setting_changed)
        row_sky.pack_end(self.cb_sky, False, False, 0)
        card_cam.pack_start(row_sky, False, False, 0)

        # FOV Slider
        fov_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_fov = Gtk.Label(label="Camera Field of View (FOV / Zoom):")
        lbl_fov.set_xalign(0.0)
        fov_box.pack_start(lbl_fov, True, True, 0)
        self.lbl_fov_val = Gtk.Label()
        self.lbl_fov_val.set_markup(f"<span color='#00e5ff'>{int(self.settings.get('fov', 65.0))}°</span>")
        fov_box.pack_end(self.lbl_fov_val, False, False, 0)
        card_cam.pack_start(fov_box, False, False, 0)

        self.scale_fov = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 45.0, 105.0, 1.0)
        self.scale_fov.set_value(self.settings.get("fov", 65.0))
        self.scale_fov.set_draw_value(False)
        self.scale_fov.connect("value-changed", self.on_fov_changed)
        card_cam.pack_start(self.scale_fov, False, False, 0)

        main_box.pack_start(card_cam, False, False, 0)

        # -------------------------------------------------------------
        # FOOTER ACTION BUTTONS
        # -------------------------------------------------------------
        btn_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)

        btn_reset = Gtk.Button(label="↺ Restore Defaults")
        btn_reset.get_style_context().add_class("btn-reset")
        btn_reset.connect("clicked", self.on_reset_clicked)
        btn_box.pack_start(btn_reset, False, False, 0)

        btn_close = Gtk.Button(label="✓ Close")
        btn_close.get_style_context().add_class("btn-close")
        btn_close.connect("clicked", lambda b: self.close())
        btn_box.pack_end(btn_close, False, False, 0)

        main_box.pack_end(btn_box, False, False, 6)

        self.is_updating_ui = False
        self.connect("destroy", Gtk.main_quit)
        self.connect("key-press-event", self.on_key_press)

        # Polling timer for engine telemetry & settings sync
        GLib.timeout_add(350, self.poll_engine_telemetry)

    def on_key_press(self, widget, event):
        if event.keyval == Gdk.KEY_Escape:
            self.close()
            return True
        return False

    def load_settings(self):
        if os.path.exists(SETTINGS_FILE):
            try:
                with open(SETTINGS_FILE, "r") as f:
                    data = json.load(f)
                    res = {**DEFAULT_SETTINGS, **data}
                    if res.get("fov", 65.0) < 3.5:
                        res["fov"] = round(res["fov"] * 180.0 / 3.14159265, 1)
                    return res
            except Exception as e:
                print(f"[-] Error reading {SETTINGS_FILE}: {e}")
        return dict(DEFAULT_SETTINGS)

    def save_settings(self):
        tmp_file = SETTINGS_FILE + ".tmp"
        try:
            with open(tmp_file, "w") as f:
                json.dump(self.settings, f, indent=2)
            os.replace(tmp_file, SETTINGS_FILE)
            self.last_file_mtime = os.path.getmtime(SETTINGS_FILE)
        except Exception as e:
            print(f"[-] Error writing {SETTINGS_FILE}: {e}")

    def poll_engine_telemetry(self):
        if os.path.exists(SETTINGS_FILE):
            try:
                mtime = os.path.getmtime(SETTINGS_FILE)
                if mtime != self.last_file_mtime:
                    self.last_file_mtime = mtime
                    with open(SETTINGS_FILE, "r") as f:
                        data = json.load(f)
                    fps = data.get("current_fps", 0.0)
                    if fps > 0.0:
                        fps_col = "#00ff88" if fps >= 30.0 else "#ffcc00" if fps >= 20.0 else "#ff3366"
                        self.lbl_fps_telemetry.set_markup(
                            f"<span size='small' color='{fps_col}'>● Live Framerate: <b>{fps:.1f} FPS</b></span> | "
                            f"<span size='small' color='#88aacc'>Zielbereich: 30 - 60 FPS</span>"
                        )
            except Exception:
                pass
        return True

    def on_auto_balance_clicked(self, widget):
        current_fps = self.settings.get("current_fps", 20.0)
        self.is_updating_ui = True

        if current_fps < 28.0:
            self.settings["shader_quality"] = 0
            self.settings["point_light_count"] = 2
            self.settings["wetness"] = 0.35
            self.settings["shadow_opacity"] = 0.60
            self.settings["shadows"] = True
        elif current_fps < 45.0:
            self.settings["shader_quality"] = 1
            self.settings["point_light_count"] = 4
            self.settings["wetness"] = 0.70
            self.settings["shadow_opacity"] = 0.85
            self.settings["shadows"] = True
        else:
            self.settings["shader_quality"] = 1
            self.settings["point_light_count"] = 6
            self.settings["wetness"] = 0.85
            self.settings["shadow_opacity"] = 0.90
            self.settings["shadows"] = True

        self.settings["action"] = "auto_balance"
        self.update_ui_from_settings()
        self.is_updating_ui = False
        self.save_settings()

    def apply_preset(self, preset_name):
        self.is_updating_ui = True
        if preset_name == "max_fps":
            self.settings["shader_quality"] = 0
            self.settings["point_light_count"] = 1
            self.settings["wetness"] = 0.20
            self.settings["shadows"] = False
            self.settings["shadow_opacity"] = 0.40
        elif preset_name == "balanced":
            self.settings["shader_quality"] = 1
            self.settings["point_light_count"] = 4
            self.settings["wetness"] = 0.70
            self.settings["shadows"] = True
            self.settings["shadow_opacity"] = 0.85
        elif preset_name == "ultra":
            self.settings["shader_quality"] = 2
            self.settings["point_light_count"] = 8
            self.settings["wetness"] = 1.00
            self.settings["glow_intensity"] = 1.50
            self.settings["shadows"] = True
            self.settings["shadow_opacity"] = 1.00

        self.settings["action"] = "preset_" + preset_name
        self.update_ui_from_settings()
        self.is_updating_ui = False
        self.save_settings()

    def update_ui_from_settings(self):
        self.cb_shader_qual.set_active_id(str(self.settings.get("shader_quality", 1)))
        self.cb_point_count.set_active_id(str(self.settings.get("point_light_count", 4)))
        self.scale_wet.set_value(self.settings.get("wetness", 0.75) * 100.0)
        self.lbl_wet_val.set_markup(f"<span color='#00ffcc'>{int(self.settings['wetness'] * 100)}%</span>")
        self.scale_glow.set_value(self.settings.get("glow_intensity", 1.30) * 100.0)
        self.lbl_glow_val.set_markup(f"<span color='#ff007f'>{int(self.settings['glow_intensity'] * 100)}%</span>")
        self.chk_shadows.set_active(self.settings.get("shadows", True))
        self.scale_shadow_op.set_value(self.settings.get("shadow_opacity", 0.85) * 100.0)
        self.lbl_shado_val.set_markup(f"<span color='#00ff88'>{int(self.settings['shadow_opacity'] * 100)}%</span>")

    def on_sun_changed(self, widget):
        val = widget.get_value()
        self.lbl_sun_val.set_markup(f"<span color='#ffcc00'>{int(val)}%</span>")
        if not self.is_updating_ui:
            self.settings["sun_intensity"] = val / 100.0
            self.save_settings()

    def on_amb_changed(self, widget):
        val = widget.get_value()
        self.lbl_amb_val.set_markup(f"<span color='#00e5ff'>{int(val)}%</span>")
        if not self.is_updating_ui:
            self.settings["ambient_intensity"] = val / 100.0
            self.save_settings()

    def on_wetness_changed(self, widget):
        val = widget.get_value()
        self.lbl_wet_val.set_markup(f"<span color='#00ffcc'>{int(val)}%</span>")
        if not self.is_updating_ui:
            self.settings["wetness"] = val / 100.0
            self.save_settings()

    def on_glow_changed(self, widget):
        val = widget.get_value()
        self.lbl_glow_val.set_markup(f"<span color='#ff007f'>{int(val)}%</span>")
        if not self.is_updating_ui:
            self.settings["glow_intensity"] = val / 100.0
            self.save_settings()

    def on_shadow_op_changed(self, widget):
        val = widget.get_value()
        self.lbl_shado_val.set_markup(f"<span color='#00ff88'>{int(val)}%</span>")
        if not self.is_updating_ui:
            self.settings["shadow_opacity"] = val / 100.0
            self.save_settings()

    def on_fov_changed(self, widget):
        val = widget.get_value()
        self.lbl_fov_val.set_markup(f"<span color='#00e5ff'>{int(val)}°</span>")
        if not self.is_updating_ui:
            self.settings["fov"] = float(val)
            self.save_settings()

    def on_setting_changed(self, *args):
        if self.is_updating_ui:
            return
        self.settings["backend"] = self.cb_backend.get_active_id() or "GPU_OPENGL"
        try:
            self.settings["sky_profile"] = int(self.cb_sky.get_active_id() or "0")
        except ValueError:
            self.settings["sky_profile"] = 0
        try:
            self.settings["shader_quality"] = int(self.cb_shader_qual.get_active_id() or "1")
        except ValueError:
            self.settings["shader_quality"] = 1
        try:
            self.settings["point_light_count"] = int(self.cb_point_count.get_active_id() or "4")
        except ValueError:
            self.settings["point_light_count"] = 4
        self.settings["shadows"] = self.chk_shadows.get_active()
        self.save_settings()

    def on_reset_clicked(self, widget):
        self.is_updating_ui = True
        self.settings = dict(DEFAULT_SETTINGS)
        self.cb_backend.set_active_id(self.settings["backend"])
        self.cb_sky.set_active_id(str(self.settings["sky_profile"]))
        self.scale_sun.set_value(self.settings["sun_intensity"] * 100.0)
        self.lbl_sun_val.set_markup(f"<span color='#ffcc00'>{int(self.settings['sun_intensity'] * 100)}%</span>")
        self.scale_amb.set_value(self.settings["ambient_intensity"] * 100.0)
        self.lbl_amb_val.set_markup(f"<span color='#00e5ff'>{int(self.settings['ambient_intensity'] * 100)}%</span>")
        self.scale_wet.set_value(self.settings["wetness"] * 100.0)
        self.lbl_wet_val.set_markup(f"<span color='#00ffcc'>{int(self.settings['wetness'] * 100)}%</span>")
        self.scale_glow.set_value(self.settings["glow_intensity"] * 100.0)
        self.lbl_glow_val.set_markup(f"<span color='#ff007f'>{int(self.settings['glow_intensity'] * 100)}%</span>")
        self.scale_fov.set_value(self.settings["fov"])
        self.lbl_fov_val.set_markup(f"<span color='#00e5ff'>{int(self.settings['fov'])}°</span>")
        self.chk_shadows.set_active(self.settings["shadows"])
        self.scale_shadow_op.set_value(self.settings["shadow_opacity"] * 100.0)
        self.lbl_shado_val.set_markup(f"<span color='#00ff88'>{int(self.settings['shadow_opacity'] * 100)}%</span>")
        self.cb_shader_qual.set_active_id(str(self.settings["shader_quality"]))
        self.cb_point_count.set_active_id(str(self.settings["point_light_count"]))
        self.is_updating_ui = False
        self.save_settings()

def main():
    win = SurrealSettingsWindow()
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
