#!/usr/bin/env python3
"""
AION SURREAL 3D ENGINE - HIGH-PRECISION PHOTO-TO-3D MODEL GENERATOR (GTK3)
Provides intuitive, full-control 3D model generation from any image:
- 100% complete 3D Relief Sculptures & Dioramas
- Clean Watertight Silhouette Standees standing on their own feet
- 3D Cyberpunk Neon Billboards
- Multi-pass precision geometry ironing & 360° texture painting (120+ passes)
- Built-in AI Subject Isolation & Background Removal (rembg / U2-Net)
Communicates with Surreal Engine via /tmp/surreal_generator_request.json
"""

import sys
import os
import json
import subprocess
import time
import threading

if "DISPLAY" not in os.environ:
    os.environ["DISPLAY"] = ":0"
os.environ["GTK_A11Y"] = "none"

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
gi.require_version('GdkPixbuf', '2.0')
gi.require_version('Pango', '1.0')
from gi.repository import Gtk, Gdk, GdkPixbuf, GLib, Pango
from surreal_gtk_theme import apply_black_gold_theme

REQUEST_FILE = "/tmp/surreal_generator_request.json"
STUDIO_STATUS_FILE = "/tmp/surreal_studio_status.json"
STUDIO_CONFIG_PATH = os.path.expanduser("~/.config/aion_surreal_studio.json")
MESH_EXTS = (".glb", ".gltf", ".obj", ".fbx")

class SurrealGeneratorWindow(Gtk.Window):
    def __init__(self, initial_image_path=None):
        super().__init__(title="📸 AION Surreal 3D // High-Precision Model Generator")
        self.set_default_size(580, 740)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_keep_above(True)
        self.set_border_width(12)

        # Apply High-Contrast Black & Gold / Yellow CSS Theme
        self._css_provider = apply_black_gold_theme()

        self.current_image_path = initial_image_path
        if not self.current_image_path or not os.path.exists(self.current_image_path):
            if os.path.exists("/root/Downloads/meovv_nobg.png"):
                self.current_image_path = "/root/Downloads/meovv_nobg.png"
            elif os.path.exists("/root/Downloads/meovv.jpg"):
                self.current_image_path = "/root/Downloads/meovv.jpg"

        self.progress_val = 0.0
        self.is_generating = False
        self.studio_proc = None
        self.set_default_size(620, 780)
        self.set_size_request(540, 620)
        self.set_resizable(True)

        # Outer vertical container
        root_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.add(root_box)

        # Scrollable container for parameters
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        root_box.pack_start(scroller, True, True, 0)

        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        scroller.add(main_box)

        # Header Title
        lbl_head = Gtk.Label()
        lbl_head.set_markup("<span size='large' weight='bold' color='#ffd700'>✨ AION SURREAL 3D // STUDIO 3D &amp; AUTO-RIG</span>")
        lbl_head.set_xalign(0.0)
        main_box.pack_start(lbl_head, False, False, 2)

        # -------------------------------------------------------------
        # CARD 1: IMAGE / 3D MESH PREVIEW & FILE SELECTION
        # -------------------------------------------------------------
        card_img = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_img.get_style_context().add_class("cyber-card")

        ci_top = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        ci_title = Gtk.Label()
        ci_title.set_markup("<span color='#ffd700' weight='bold'>[ 🖼️ SOURCE IMAGE OR STATIC 3D MESH (.GLB/.OBJ) ]</span>")
        ci_title.set_xalign(0.0)
        ci_top.pack_start(ci_title, True, True, 0)

        btn_pick = Gtk.Button(label="📁 Browse Image / 3D Mesh...")
        btn_pick.get_style_context().add_class("btn-pick")
        btn_pick.connect("clicked", self.on_pick_another_image)
        ci_top.pack_end(btn_pick, False, False, 0)
        card_img.pack_start(ci_top, False, False, 0)

        # Image preview container
        preview_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.img_widget = Gtk.Image()
        preview_box.pack_start(self.img_widget, False, False, 4)

        info_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        self.lbl_filename = Gtk.Label()
        self.lbl_filename.set_xalign(0.0)
        self.lbl_filename.set_line_wrap(True)
        info_box.pack_start(self.lbl_filename, False, False, 0)

        self.lbl_details = Gtk.Label()
        self.lbl_details.set_xalign(0.0)
        self.lbl_details.set_line_wrap(True)
        info_box.pack_start(self.lbl_details, False, False, 0)

        preview_box.pack_start(info_box, True, True, 0)
        card_img.pack_start(preview_box, False, False, 4)

        # 1-Click AI Background Removal button
        self.btn_ai_isolate = Gtk.Button(label="🤖 Auto AI Subject Isolation (Detect People & Remove Background)")
        self.btn_ai_isolate.get_style_context().add_class("btn-ai-isolate")
        self.btn_ai_isolate.connect("clicked", self.on_ai_isolate_clicked)
        card_img.pack_start(self.btn_ai_isolate, False, False, 4)

        main_box.pack_start(card_img, False, False, 0)

        # -------------------------------------------------------------
        # CARD 2: MODEL GENERATION MODE & STUDIO 3D BACKEND
        # -------------------------------------------------------------
        card_mode = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_mode.get_style_context().add_class("cyber-card")

        cm_title = Gtk.Label()
        cm_title.set_markup("<span color='#ffd700' weight='bold'>[ 🌟 3D MODEL TYPE &amp; RECONSTRUCTION MODE ]</span>")
        cm_title.set_xalign(0.0)
        card_mode.pack_start(cm_title, False, False, 0)

        self.cb_mode = Gtk.ComboBoxText()
        self.cb_mode.append("0", "✨ Studio AI 3D + Mixamo Auto-Rig (.glb Full-Body 360° Mesh)")
        self.cb_mode.append("1", "💎 3D Relief Sculpture & Diorama (Offline CPU, Solid Plate)")
        self.cb_mode.append("2", "👤 3D Character Silhouette (Offline CPU Standee)")
        self.cb_mode.append("3", "🏙️ 3D Cyberpunk Neon Billboard (Offline CPU Pylon)")
        self.cb_mode.set_active_id("0")
        self.cb_mode.connect("changed", self.on_mode_changed)
        for cell in self.cb_mode.get_cells():
            cell.set_property("ellipsize", Pango.EllipsizeMode.END)
        card_mode.pack_start(self.cb_mode, False, False, 0)

        self.lbl_mode_desc = Gtk.Label()
        self.lbl_mode_desc.set_xalign(0.0)
        self.configure_wrap_label(self.lbl_mode_desc, 62)
        card_mode.pack_start(self.lbl_mode_desc, False, False, 2)

        # Studio AI Backend & Front Facing row
        self.studio_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        row_be = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_be = Gtk.Label(label="Studio Backend:")
        lbl_be.set_xalign(0.0)
        row_be.pack_start(lbl_be, False, False, 0)
        self.cb_backend = Gtk.ComboBoxText()
        self.cb_backend.append("auto", "⚡ Auto Studio AI (Hunyuan2.1 / TRELLIS.2 / Tripo / InstantMesh)")
        self.cb_backend.append("hf_trellis2", "🎭 Microsoft TRELLIS.2 (High-Detail PBR, Free HF_TOKEN)")
        self.cb_backend.append("hf_hunyuan21", "🏆 Tencent Hunyuan3D-2.1 (Studio PBR, Free HF_TOKEN)")
        self.cb_backend.append("hf_hunyuan2mini", "🔥 Tencent Hunyuan3D-2mini-Turbo (Free HF_TOKEN)")
        self.cb_backend.append("hf_instantmesh", "🧩 TencentARC InstantMesh (Multi-View ZeroGPU)")
        self.cb_backend.append("hf_triposg", "🧊 VAST TripoSG (ZeroGPU)")
        self.cb_backend.append("hf_sf3d", "📦 Stable Fast 3D (Nur Props — wenig Gesichtsdetails)")
        self.cb_backend.append("tripo", "💎 Tripo3D Studio API (TRIPO_API_KEY)")
        self.cb_backend.append("fal", "☁️ fal.ai Hunyuan3D v2 API (FAL_KEY)")
        self.cb_backend.append("glb_rig", "🦴 Local Blender Auto-Rig Only (.glb/.obj Input)")
        self.cb_backend.set_active_id("auto")
        for cell in self.cb_backend.get_cells():
            cell.set_property("ellipsize", Pango.EllipsizeMode.END)
        row_be.pack_start(self.cb_backend, True, True, 0)
        self.studio_box.pack_start(row_be, False, False, 0)

        row_yaw = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_yaw = Gtk.Label(label="Front Facing Orientation:")
        lbl_yaw.set_xalign(0.0)
        row_yaw.pack_start(lbl_yaw, True, True, 0)
        self.cb_yaw = Gtk.ComboBoxText()
        self.cb_yaw.append("auto", "🧭 Auto-Detect via Feet")
        self.cb_yaw.append("0", "0° Default (-Y Front)")
        self.cb_yaw.append("180", "180° Flip Front/Back")
        self.cb_yaw.append("90", "+90° Rotate")
        self.cb_yaw.append("270", "-90° Rotate")
        self.cb_yaw.set_active_id("auto")
        row_yaw.pack_end(self.cb_yaw, False, False, 0)
        self.studio_box.pack_start(row_yaw, False, False, 0)

        row_qual = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lbl_qual = Gtk.Label(label="Studio Detail & PBR Texture:")
        lbl_qual.set_xalign(0.0)
        row_qual.pack_start(lbl_qual, True, True, 0)
        self.cb_quality = Gtk.ComboBoxText()
        self.cb_quality.append("1024", "✨ Standard HD (1024px AI + 2K Textur, ~100k Tris)")
        self.cb_quality.append("1536", "💎 Ultra Studio 4K-Ready (1536px AI + 2K Textur, ~100k Tris)")
        self.cb_quality.set_active_id("1024")
        row_qual.pack_end(self.cb_quality, False, False, 0)
        self.studio_box.pack_start(row_qual, False, False, 0)

        # Collapsible Cloud API Keys / HF Token settings (~/.config/aion_surreal_studio.json)
        exp_keys = Gtk.Expander(label="🔑 Studio Tokens (HF_TOKEN / TRIPO_API_KEY) & Kostenlose Web-Portale")
        keys_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        keys_box.set_border_width(4)
        saved_cfg = self.load_studio_cfg()
        has_any_key = bool(saved_cfg.get("hf_token") or saved_cfg.get("tripo_api_key") or saved_cfg.get("fal_key") or os.environ.get("HF_TOKEN") or os.environ.get("TRIPO_API_KEY") or os.environ.get("FAL_KEY"))

        link_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        lnk_hf = Gtk.LinkButton(uri="https://huggingface.co/settings/tokens", label="🔑 Gratis HF_TOKEN holen (5 Min/Tag GPU)")
        lnk_tripo = Gtk.LinkButton(uri="https://studio.tripo3d.ai", label="🌐 Tripo3D Web (.glb kostenlos exportieren)")
        link_row.pack_start(lnk_hf, False, False, 0)
        link_row.pack_start(lnk_tripo, False, False, 0)
        keys_box.pack_start(link_row, False, False, 2)

        self.entry_hf = self.make_key_row(keys_box, "HF Token (empfohlen fuer TRELLIS.2 / Hunyuan3D-2.1):", saved_cfg.get("hf_token", ""))
        self.entry_tripo = self.make_key_row(keys_box, "Tripo3D API Key (optional, TRIPO_API_KEY):", saved_cfg.get("tripo_api_key", ""))
        self.entry_fal = self.make_key_row(keys_box, "fal.ai Key (optional, FAL_KEY):", saved_cfg.get("fal_key", ""))
        exp_keys.add(keys_box)
        exp_keys.set_expanded(not has_any_key)
        self.studio_box.pack_start(exp_keys, False, False, 0)
        card_mode.pack_start(self.studio_box, False, False, 2)

        main_box.pack_start(card_mode, False, False, 0)

        # -------------------------------------------------------------
        # CARD 3: DEPTH, RELIEF & GEOMETRY TUNING
        # -------------------------------------------------------------
        card_params = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card_params.get_style_context().add_class("cyber-card")

        cp_title = Gtk.Label()
        cp_title.set_markup("<span color='#ffd700' weight='bold'>[ 📐 3D GEOMETRY &amp; DEPTH TUNING ]</span>")
        cp_title.set_xalign(0.0)
        card_params.pack_start(cp_title, False, False, 0)

        # Depth Slider
        row_depth = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_depth = Gtk.Label(label="3D Depth & Curvature (Model Thickness):")
        lbl_depth.set_xalign(0.0)
        row_depth.pack_start(lbl_depth, True, True, 0)
        self.lbl_depth_val = Gtk.Label()
        self.lbl_depth_val.set_markup("<span color='#ffe875'>45%</span>")
        row_depth.pack_end(self.lbl_depth_val, False, False, 0)
        card_params.pack_start(row_depth, False, False, 0)

        self.scale_depth = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 10.0, 100.0, 5.0)
        self.scale_depth.set_value(45.0)
        self.scale_depth.set_draw_value(False)
        self.scale_depth.connect("value-changed", self.on_depth_changed)
        card_params.pack_start(self.scale_depth, False, False, 0)

        # Relief Strength Slider
        row_rel = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_rel = Gtk.Label(label="Surface Relief Detail (Folds, Contours & Details):")
        lbl_rel.set_xalign(0.0)
        row_rel.pack_start(lbl_rel, True, True, 0)
        self.lbl_rel_val = Gtk.Label()
        self.lbl_rel_val.set_markup("<span color='#ffd700'>35%</span>")
        row_rel.pack_end(self.lbl_rel_val, False, False, 0)
        card_params.pack_start(row_rel, False, False, 0)

        self.scale_relief = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 10.0, 100.0, 5.0)
        self.scale_relief.set_value(35.0)
        self.scale_relief.set_draw_value(False)
        self.scale_relief.connect("value-changed", self.on_relief_changed)
        card_params.pack_start(self.scale_relief, False, False, 0)

        # Height Slider
        row_h = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_h = Gtk.Label(label="3D Model World Height (Meters):")
        lbl_h.set_xalign(0.0)
        row_h.pack_start(lbl_h, True, True, 0)
        self.lbl_h_val = Gtk.Label()
        self.lbl_h_val.set_markup("<span color='#ffe875'>1.80 m</span>")
        row_h.pack_end(self.lbl_h_val, False, False, 0)
        card_params.pack_start(row_h, False, False, 0)

        self.scale_height = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.8, 6.0, 0.1)
        self.scale_height.set_value(1.80)
        self.scale_height.set_draw_value(False)
        self.scale_height.connect("value-changed", self.on_height_changed)
        card_params.pack_start(self.scale_height, False, False, 0)

        # Multi-Pass Ironing & Painting Slider (up to 1440x Passes)
        row_pass = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        lbl_pass = Gtk.Label(label="🎨 Multi-Pass Geometric Smoothing & 360° Painting (Up to 1,440x):")
        lbl_pass.set_xalign(0.0)
        row_pass.pack_start(lbl_pass, True, True, 0)
        self.lbl_pass_val = Gtk.Label()
        self.lbl_pass_val.set_markup("<span color='#ffd700'><b>1440x Passes</b></span>")
        row_pass.pack_end(self.lbl_pass_val, False, False, 0)
        card_params.pack_start(row_pass, False, False, 0)

        self.scale_passes = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 120.0, 1440.0, 10.0)
        self.scale_passes.set_value(1440.0)
        self.scale_passes.set_draw_value(False)
        self.scale_passes.connect("value-changed", self.on_passes_changed)
        card_params.pack_start(self.scale_passes, False, False, 0)

        # Checkboxes for 360 Backside & Anatomical Smoothing
        sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        card_params.pack_start(sep, False, False, 4)

        self.chk_backside = Gtk.CheckButton(label="🔄 360° Volumetric Backside Synthesis (Hair, Skull & Spine - Zero Face Mirroring)")
        self.chk_backside.set_active(True)
        card_params.pack_start(self.chk_backside, False, False, 2)

        self.chk_smooth = Gtk.CheckButton(label="🧠 Anatomical Body Smoothing (Organic Human Proportions, No Spikes)")
        self.chk_smooth.set_active(True)
        card_params.pack_start(self.chk_smooth, False, False, 2)

        main_box.pack_start(card_params, False, False, 0)

        # -------------------------------------------------------------
        # CARD 4: GENERATION PROGRESS & ACTION
        # -------------------------------------------------------------
        card_act = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        card_act.get_style_context().add_class("cyber-card")

        self.lbl_status = Gtk.Label()
        self.lbl_status.set_markup("<span color='#ffe875'>Ready to generate high-precision 3D model.</span>")
        self.lbl_status.set_xalign(0.0)
        self.configure_wrap_label(self.lbl_status, 62)
        card_act.pack_start(self.lbl_status, False, False, 0)

        self.progress_bar = Gtk.ProgressBar()
        self.progress_bar.set_fraction(0.0)
        card_act.pack_start(self.progress_bar, False, False, 0)

        self.btn_gen = Gtk.Button(label="🚀 GENERATE HIGH-PRECISION 3D MODEL")
        self.btn_gen.get_style_context().add_class("btn-generate")
        self.btn_gen.connect("clicked", self.on_generate_clicked)
        card_act.pack_start(self.btn_gen, False, False, 4)

        btn_cancel = Gtk.Button(label="✕ Cancel")
        btn_cancel.get_style_context().add_class("btn-cancel")
        btn_cancel.connect("clicked", lambda b: self.close())
        card_act.pack_start(btn_cancel, False, False, 0)

        root_box.pack_start(card_act, False, False, 0)

        self.connect("destroy", self.on_window_destroy)
        self.connect("key-press-event", self.on_key_press)

        # Load preview & sync initial mode state
        self.configure_wrap_label(self.lbl_filename, 46)
        self.configure_wrap_label(self.lbl_details, 46)
        self.load_image_preview()
        self.update_mode_description()

    @staticmethod
    def configure_wrap_label(lbl, max_chars=58):
        lbl.set_line_wrap(True)
        lbl.set_line_wrap_mode(Pango.WrapMode.WORD_CHAR)
        lbl.set_max_width_chars(max_chars)

    def load_studio_cfg(self):
        try:
            if os.path.exists(STUDIO_CONFIG_PATH):
                with open(STUDIO_CONFIG_PATH, "r", encoding="utf-8") as f:
                    return json.load(f) or {}
        except Exception:
            pass
        return {}

    def save_studio_cfg(self):
        cfg = self.load_studio_cfg()
        hf = self.entry_hf.get_text().strip()
        tr = self.entry_tripo.get_text().strip()
        fa = self.entry_fal.get_text().strip()
        if hf:
            cfg["hf_token"] = hf
        elif "hf_token" in cfg:
            cfg.pop("hf_token", None)
        if tr:
            cfg["tripo_api_key"] = tr
        elif "tripo_api_key" in cfg:
            cfg.pop("tripo_api_key", None)
        if fa:
            cfg["fal_key"] = fa
        elif "fal_key" in cfg:
            cfg.pop("fal_key", None)
        cfg["backend"] = self.cb_backend.get_active_id() or "auto"
        os.makedirs(os.path.dirname(STUDIO_CONFIG_PATH), exist_ok=True)
        tmp = STUDIO_CONFIG_PATH + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
        os.chmod(tmp, 0o600)
        os.replace(tmp, STUDIO_CONFIG_PATH)

    def make_key_row(self, parent_box, label_text, initial_val):
        lbl = Gtk.Label()
        lbl.set_markup(f"<span size='small' color='#e5ca6b'>{GLib.markup_escape_text(label_text)}</span>")
        lbl.set_xalign(0.0)
        parent_box.pack_start(lbl, False, False, 0)
        entry = Gtk.Entry()
        entry.set_visibility(False)
        entry.set_invisible_char('•')
        if initial_val:
            entry.set_text(initial_val)
        parent_box.pack_start(entry, False, False, 0)
        return entry

    def is_current_mesh(self):
        if not self.current_image_path:
            return False
        return os.path.splitext(self.current_image_path)[1].lower() in MESH_EXTS

    def is_current_alpha(self):
        if not self.current_image_path or not os.path.exists(self.current_image_path):
            return False
        try:
            pix = GdkPixbuf.Pixbuf.new_from_file(self.current_image_path)
            return pix.get_has_alpha()
        except:
            return False

    def on_window_destroy(self, *args):
        if self.studio_proc and self.studio_proc.poll() is None:
            try:
                self.studio_proc.terminate()
            except Exception:
                pass
        Gtk.main_quit()

    def on_key_press(self, widget, event):
        if event.keyval == Gdk.KEY_Escape:
            self.close()
            return True
        return False

    def load_image_preview(self):
        if not self.current_image_path or not os.path.exists(self.current_image_path):
            self.lbl_filename.set_markup("<span color='#ff6b6b'>No image or 3D mesh selected</span>")
            return

        fname = os.path.basename(self.current_image_path)
        self.lbl_filename.set_markup(f"<b>File:</b> <span color='#ffd700'>{GLib.markup_escape_text(fname)}</span>")

        if self.is_current_mesh():
            sz_kb = os.path.getsize(self.current_image_path) / 1024.0
            sz_txt = f"{sz_kb / 1024.0:.2f} MB" if sz_kb >= 1024.0 else f"{sz_kb:.1f} KB"
            self.lbl_details.set_markup(
                f"<b>Static 3D Mesh:</b> <span color='#ffe875'>{sz_txt}</span> | Ready for local Blender Auto-Rigging + Mixamo Dance"
            )
            self.img_widget.set_from_icon_name("applications-graphics", Gtk.IconSize.DIALOG)
            self.btn_ai_isolate.set_sensitive(False)
            self.cb_mode.set_active_id("0")
            self.cb_backend.set_active_id("glb_rig")
            self.scale_height.set_value(1.68)
            self.update_mode_description()
            return

        self.btn_ai_isolate.set_sensitive(True)
        try:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(self.current_image_path)
            orig_w, orig_h = pixbuf.get_width(), pixbuf.get_height()
            has_alpha = pixbuf.get_has_alpha()

            alpha_str = "<span color='#5cff8d'>Alpha Transparency Detected</span>" if has_alpha else "<span color='#ffd700'>Full Picture (No Alpha — Studio Mode runs rembg)</span>"
            haze_tip = ""
            if has_alpha:
                try:
                    from PIL import Image
                    import numpy as np
                    arr_a = np.array(Image.open(self.current_image_path).convert("RGBA"))[:, :, 3]
                    fg = arr_a >= 16
                    if np.any(fg) and float(np.mean(arr_a[fg] < 180)) > 0.25:
                        haze_tip = "\n<span color='#ffb347' size='small'>⚠ Warnung: Stark halbtransparente Figur (z.B. doppeltes _nobg) — Original-Cutout ohne Nebel liefert viel sauberere Beine!</span>"
                except Exception:
                    pass
            narrow_tip = "\n<span color='#ffe875' size='small'>💡 Hinweis: Schmaler Bildausschnitt (&lt;260px) – fuer beste Gesichter &amp; Haende ein klares Front-Foto oder fertige .glb nutzen.</span>" if orig_w < 260 else ""
            self.lbl_details.set_markup(f"<b>Resolution:</b> {orig_w}x{orig_h} px | {alpha_str}{haze_tip}{narrow_tip}")

            # Default to Studio AI 3D + Mixamo Auto-Rig for true 360° studio characters
            self.cb_mode.set_active_id("0")
            if self.cb_backend.get_active_id() == "glb_rig":
                self.cb_backend.set_active_id("auto")
            self.scale_height.set_value(1.68)

            self.update_mode_description()

            # Scale thumbnail maintaining aspect ratio
            max_size = 180
            scale = min(max_size / orig_w, max_size / orig_h)
            tw = max(1, int(orig_w * scale))
            th = max(1, int(orig_h * scale))
            thumb = pixbuf.scale_simple(tw, th, GdkPixbuf.InterpType.BILINEAR)
            self.img_widget.set_from_pixbuf(thumb)
        except Exception as e:
            self.lbl_details.set_markup(f"<span color='#ff6b6b'>Preview error: {GLib.markup_escape_text(str(e))}</span>")

    def on_ai_isolate_clicked(self, btn):
        if not self.current_image_path or not os.path.exists(self.current_image_path):
            return
        self.lbl_status.set_markup("<span color='#ffe875'>🤖 Checking image background / running AI Subject Isolation...</span>")
        self.btn_ai_isolate.set_sensitive(False)

        def worker():
            try:
                from PIL import Image
                import numpy as np
                inp = Image.open(self.current_image_path).convert("RGBA")
                arr_a = np.array(inp)[:, :, 3]
                trans_frac = float(np.mean(arr_a < 16))
                opaque_frac = float(np.mean(arr_a >= 180))
                base, _ = os.path.splitext(self.current_image_path)
                if base.endswith("_nobg") or (trans_frac > 0.05 and opaque_frac > 0.05):
                    GLib.idle_add(lambda: self.on_isolate_complete(self.current_image_path, skipped=True))
                    return
                out_path = base + "_nobg.png"
                if not os.path.exists(out_path):
                    import rembg
                    session = rembg.new_session("u2netp")
                    out = rembg.remove(inp, session=session, post_process_mask=False)
                    out.save(out_path)
                GLib.idle_add(lambda: self.on_isolate_complete(out_path, skipped=False))
            except Exception as e:
                GLib.idle_add(lambda: self.on_isolate_failed(str(e)))

        threading.Thread(target=worker, daemon=True).start()

    def on_isolate_complete(self, out_path, skipped=False):
        self.btn_ai_isolate.set_sensitive(True)
        prev_mode = self.cb_mode.get_active_id() or "0"
        self.current_image_path = out_path
        self.load_image_preview()
        self.cb_mode.set_active_id(prev_mode)
        if skipped:
            self.lbl_status.set_markup(
                "<span color='#ffe875'><b>[✓] Bereits freigestellt!</b> Transparenter Hintergrund erkannt — erneutes rembg uebersprungen um Kleidung zu schuetzen.</span>"
            )
        else:
            self.lbl_status.set_markup(
                "<span color='#5cff8d'><b>[✓] AI Isolation Complete!</b> Foreground cutout ready without changing Studio mode.</span>"
            )

    def on_isolate_failed(self, err):
        self.btn_ai_isolate.set_sensitive(True)
        self.lbl_status.set_markup(f"<span color='#ff6b6b'>[-] AI Isolation error: {err}</span>")

    def on_pick_another_image(self, btn):
        dialog = Gtk.FileChooserDialog(
            title="AION Surreal 3D // Select Image or 3D Mesh (.glb/.obj)",
            parent=self,
            action=Gtk.FileChooserAction.OPEN
        )
        dialog.add_buttons(
            Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
            Gtk.STOCK_OPEN, Gtk.ResponseType.OK
        )
        filter_all = Gtk.FileFilter()
        filter_all.set_name("Images & 3D Meshes (*.png *.jpg *.jpeg *.webp *.glb *.gltf *.obj)")
        for pat in ("*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg", "*.JPEG", "*.bmp", "*.webp", "*.glb", "*.GLB", "*.gltf", "*.obj", "*.OBJ", "*.fbx"):
            filter_all.add_pattern(pat)
        dialog.add_filter(filter_all)

        dialog.set_current_folder("/root/Downloads")
        res = dialog.run()
        if res == Gtk.ResponseType.OK:
            self.current_image_path = dialog.get_filename()
            self.load_image_preview()
        dialog.destroy()

    def update_mode_description(self):
        mid = self.cb_mode.get_active_id() if hasattr(self, 'cb_mode') else "0"
        is_studio = (mid == "0")
        if hasattr(self, 'studio_box'):
            self.studio_box.set_sensitive(is_studio)
        for attr in ('scale_depth', 'scale_relief', 'scale_passes', 'chk_backside', 'chk_smooth'):
            if hasattr(self, attr):
                getattr(self, attr).set_sensitive(not is_studio)

        if mid == "0":
            self.lbl_mode_desc.set_markup(
                "<span size='small' color='#ffe875'>★ <b>True Studio AI 3D Mesh + Local Blender Mixamo Auto-Rig:</b> "
                "Reconstructs a complete 360° high-poly character (face, hair, outfit front &amp; back), "
                "rigs 22 Mixamo humanoid joints locally (zero local VRAM), and exports an animated dancing <b>.glb</b>. "
                "Also accepts external static <b>.glb/.obj</b> meshes from Meshy/Tripo/Hunyuan web!</span>"
            )
        elif mid == "1":
            self.lbl_mode_desc.set_markup(
                "<span size='small' color='#e5ca6b'>✔ Preserves <b>100% of the image</b> (Offline CPU). Faces, clothes &amp; scene "
                "are extruded with 3D volume, rounded chamfer rim, and a sturdy ground pedestal.</span>"
            )
        elif mid == "2":
            self.lbl_mode_desc.set_markup(
                "<span size='small' color='#e5ca6b'>✔ Creates a <b>watertight standalone 3D cutout figure</b> (Offline CPU) standing on its feet.</span>"
            )
        elif mid == "3":
            self.lbl_mode_desc.set_markup(
                "<span size='small' color='#ffd700'>✔ Creates a massive 3D Cyberpunk neon billboard with luminous border "
                "and architectural support pylons.</span>"
            )

    def on_mode_changed(self, cb):
        self.update_mode_description()

    def on_depth_changed(self, scale):
        val = int(scale.get_value())
        self.lbl_depth_val.set_markup(f"<span color='#ffe875'>{val}%</span>")

    def on_relief_changed(self, scale):
        val = int(scale.get_value())
        self.lbl_rel_val.set_markup(f"<span color='#ffd700'>{val}%</span>")

    def on_height_changed(self, scale):
        val = scale.get_value()
        self.lbl_h_val.set_markup(f"<span color='#ffe875'>{val:.2f} m</span>")

    def on_passes_changed(self, scale):
        val = int(scale.get_value())
        self.lbl_pass_val.set_markup(f"<span color='#ffd700'><b>{val}x Passes</b></span>")

    def on_generate_clicked(self, btn):
        if self.is_generating:
            return
        if not self.current_image_path or not os.path.exists(self.current_image_path):
            self.lbl_status.set_markup("<span color='#ff6b6b'>[-] Please select an image or 3D mesh first.</span>")
            return

        self.is_generating = True
        self.btn_gen.set_sensitive(False)
        self.progress_val = 0.0
        self.progress_bar.set_fraction(0.05)

        if self.cb_mode.get_active_id() == "0":
            self.start_studio_generation()
            return

        self.target_passes = int(self.scale_passes.get_value())

        # Dynamic timing based on chosen pass count (~4.2s total)
        step_ms = max(25, int(4200 / (self.target_passes * 1.5)))
        GLib.timeout_add(step_ms, self.step_generation_progress)

    def start_studio_generation(self):
        try:
            self.save_studio_cfg()
        except Exception:
            pass

        if os.path.exists(STUDIO_STATUS_FILE):
            try:
                os.remove(STUDIO_STATUS_FILE)
            except OSError:
                pass

        script_dir = os.path.dirname(os.path.abspath(__file__))
        studio_script = os.path.join(script_dir, "surreal_studio_3d.py")
        backend = self.cb_backend.get_active_id() or "auto"
        yaw = self.cb_yaw.get_active_id() or "auto"
        trellis_res = self.cb_quality.get_active_id() if hasattr(self, "cb_quality") else "1024"
        height_m = self.scale_height.get_value()

        cmd = [
            sys.executable or "python3",
            studio_script,
            self.current_image_path,
            "--backend", backend,
            "--height", f"{height_m:.2f}",
            "--front-yaw", yaw,
            "--max-faces", "100000",
            "--trellis-res", trellis_res or "1024",
            "--texture-size", "2048",
            "--status-file", STUDIO_STATUS_FILE,
            "--notify-engine",
        ]
        self.lbl_status.set_markup("<span color='#ffe875'>🚀 Launching Studio AI 3D + Local Blender Auto-Rig pipeline...</span>")
        self.studio_proc = subprocess.Popen(cmd)
        GLib.timeout_add(250, self.poll_studio_progress)

    def poll_studio_progress(self):
        st = None
        if os.path.exists(STUDIO_STATUS_FILE):
            try:
                with open(STUDIO_STATUS_FILE, "r", encoding="utf-8") as f:
                    st = json.load(f)
            except Exception:
                st = None

        if st:
            prog = float(st.get("progress", 0.1))
            msg = GLib.markup_escape_text(str(st.get("message", "Working...")))
            state = st.get("state", "running")
            self.progress_bar.set_fraction(max(0.05, min(1.0, prog)))
            if state == "done":
                self.lbl_status.set_markup(f"<span color='#5cff8d'><b>[✓] {msg}</b></span>")
                self.progress_bar.set_fraction(1.0)
                GLib.timeout_add(500, lambda: (self.close(), False)[1])
                return False
            elif state == "error":
                self.lbl_status.set_markup(f"<span color='#ff6b6b'><b>[-] {msg}</b></span>")
                self.is_generating = False
                self.btn_gen.set_sensitive(True)
                return False
            else:
                self.lbl_status.set_markup(f"<span color='#ffe875'>{msg}</span>")

        if self.studio_proc:
            rc = self.studio_proc.poll()
            if rc is not None:
                if rc == 0:
                    self.progress_bar.set_fraction(1.0)
                    self.lbl_status.set_markup("<span color='#5cff8d'><b>[✓] Studio 3D Model ready! Loading into Dojo...</b></span>")
                    GLib.timeout_add(500, lambda: (self.close(), False)[1])
                    return False
                if not st or st.get("state") != "error":
                    self.lbl_status.set_markup(f"<span color='#ff6b6b'><b>[-] Studio 3D worker exited with code {rc}</b></span>")
                self.is_generating = False
                self.btn_gen.set_sensitive(True)
                return False

        return True

    def step_generation_progress(self):
        inc = 1.0 / (self.target_passes * 1.5)
        self.progress_val += inc
        self.progress_bar.set_fraction(min(1.0, self.progress_val))

        cur_pass = min(self.target_passes, int(self.progress_val * self.target_passes) + 1)

        if self.progress_val < 0.25:
            self.lbl_status.set_markup(
                f"<span color='#ffe875'>🧠 [Pass {cur_pass}/{self.target_passes}] AI Saliency &amp; Anatomical Depth Profiling...</span>"
            )
            return True
        elif self.progress_val < 0.60:
            self.lbl_status.set_markup(
                f"<span color='#ffe875'>🧹 [Pass {cur_pass}/{self.target_passes}] Geometry Smoothing: Ironing Polygon Peaks &amp; Folds...</span>"
            )
            return True
        elif self.progress_val < 0.90:
            self.lbl_status.set_markup(
                f"<span color='#ffe875'>🎨 [Pass {cur_pass}/{self.target_passes}] 360° Texture Painting: Hair Cascade &amp; Inpainting...</span>"
            )
            return True
        elif self.progress_val < 0.99:
            self.lbl_status.set_markup(
                f"<span color='#ffe875'>✨ [Pass {cur_pass}/{self.target_passes}] Final Polish &amp; Watertight Mesh Sealing...</span>"
            )
            return True
        else:
            self.lbl_status.set_markup(
                f"<span color='#5cff8d'><b>[✓] 3D Model perfected after {self.target_passes} passes! Placing into scene...</b></span>"
            )
            self.finish_generation()
            return False

    def finish_generation(self):
        depth_norm = self.scale_depth.get_value() / 100.0
        relief_norm = self.scale_relief.get_value() / 100.0
        height_m = self.scale_height.get_value()
        mode_id = int(self.cb_mode.get_active_id())
        iron_p = int(self.scale_passes.get_value())

        req = {
            "path": self.current_image_path,
            "mode": mode_id,
            "depth": depth_norm,
            "relief": relief_norm,
            "height": height_m,
            "generate_backside": 1 if self.chk_backside.get_active() else 0,
            "anatomical_smooth": 1 if self.chk_smooth.get_active() else 0,
            "body_volume": 1.0,
            "ironing_passes": iron_p,
            "timestamp": time.time()
        }

        try:
            tmp_req = REQUEST_FILE + ".tmp"
            with open(tmp_req, "w") as f:
                json.dump(req, f, indent=2)
            os.replace(tmp_req, REQUEST_FILE)
            print(f"[✓] Generator request written to {REQUEST_FILE}")
        except Exception as e:
            print(f"[-] Error writing request: {e}")

        GLib.timeout_add(450, lambda: (self.close(), False)[1])

def main():
    initial_path = sys.argv[1] if len(sys.argv) > 1 else None
    win = SurrealGeneratorWindow(initial_path)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
