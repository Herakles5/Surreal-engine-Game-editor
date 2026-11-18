#!/usr/bin/env python3
"""
AION SURREAL 3D STUDIO - AI IMAGE-TO-3D + BLENDER AUTO-RIGGER PIPELINE
======================================================================
Acquires high-poly, 360-degree textured studio 3D meshes (without local VRAM
or multi-GB disk bloat) and locally auto-rigs them onto Mixamo dance skeletons
using Blender 4.0.2 (`surreal_blender_rig.py`).

Supported Backends:
  - glb_rig        : Auto-rig any existing static .glb/.gltf/.obj (0 internet)
  - hf_sf3d        : Stable Fast 3D on HuggingFace ZeroGPU (~5-15s)
  - hf_trellis2    : Microsoft TRELLIS.2 on HuggingFace ZeroGPU (~30-90s)
  - hf_hunyuan2mini: Tencent Hunyuan3D-2mini-Turbo on ZeroGPU
  - hf_hunyuan21   : Tencent Hunyuan3D-2.1 on ZeroGPU (needs HF_TOKEN / Pro)
  - hf_instantmesh : TencentARC InstantMesh multi-view -> 3D GLB
  - hf_triposg     : VAST-AI TripoSG segmentation -> mesh -> texture GLB
  - tripo          : Tripo3D Official OpenAPI (TRIPO_API_KEY)
  - fal            : fal.ai Hunyuan3D v2 API (FAL_KEY)
  - auto           : Smart cascade based on configured keys & free ZeroGPU
"""

import argparse
from collections import deque
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid

import numpy as np
from PIL import Image, ImageFilter
import requests


CONFIG_PATH = os.path.expanduser("~/.config/aion_surreal_studio.json")
DEFAULT_STATUS_FILE = "/tmp/surreal_studio_status.json"
STUDIO_READY_FILE = "/tmp/surreal_studio_ready.json"


def load_studio_config() -> dict:
    cfg = {}
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                cfg = json.load(f) or {}
        except Exception:
            cfg = {}
    # Env overrides file config
    env_hf = os.environ.get("HF_TOKEN") or os.environ.get("HUGGINGFACE_TOKEN") or os.environ.get("HUGGING_FACE_HUB_TOKEN")
    if env_hf:
        cfg["hf_token"] = env_hf.strip()
    if os.environ.get("TRIPO_API_KEY"):
        cfg["tripo_api_key"] = os.environ["TRIPO_API_KEY"].strip()
    if os.environ.get("FAL_KEY"):
        cfg["fal_key"] = os.environ["FAL_KEY"].strip()
    return cfg


def save_studio_config(cfg: dict):
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    tmp = CONFIG_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    os.chmod(tmp, 0o600)
    os.replace(tmp, CONFIG_PATH)


def write_status(status_file: str, state: str, progress: float, message: str, output_glb: str = "", error: str = ""):
    if not status_file:
        return
    payload = {
        "state": state,          # running | done | error
        "progress": round(max(0.0, min(1.0, progress)), 3),
        "message": message,
        "output_glb": output_glb,
        "error": error,
        "timestamp": time.time(),
    }
    try:
        tmp = status_file + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        os.replace(tmp, status_file)
    except Exception:
        pass
    print(f"[Studio3D] [{int(progress * 100):3d}%] {message}", flush=True)


def sanitize_stem(path: str) -> str:
    stem = os.path.splitext(os.path.basename(path))[0]
    stem = re.sub(r"[^A-Za-z0-9_\-]+", "_", stem).strip("_")
    return stem or "studio_model"


def default_output_dir(script_dir: str) -> str:
    engine_dir = os.path.abspath(os.path.join(script_dir, ".."))
    out_dir = os.path.join(engine_dir, "assets", "models", "studio3d")
    os.makedirs(out_dir, exist_ok=True)
    return out_dir


def _seal_interior_pinholes(arr: np.ndarray, rescue_haze: bool = False) -> np.ndarray:
    """Seals enclosed interior transparency pinholes without filling true between-leg gaps."""
    h, w = arr.shape[:2]
    alpha = arr[:, :, 3]
    fg_count = int(np.sum(alpha >= 24))
    if fg_count < 64:
        return arr

    # 1. Resolidify semi-transparent interior pixels on hazy double-rembg cutouts
    if rescue_haze:
        haze_mask = (alpha >= 28) & (alpha < 215)
        if np.any(haze_mask):
            arr[haze_mask, 3] = np.maximum(alpha[haze_mask], 235)
            alpha = arr[:, :, 3]

    # 2. Flood-fill background from canvas edges (4-connected)
    empty = alpha < 24
    border_reach = np.zeros((h, w), dtype=bool)
    q = deque()
    for x in range(w):
        if empty[0, x]:
            border_reach[0, x] = True
            q.append((0, x))
        if h > 1 and empty[h - 1, x]:
            border_reach[h - 1, x] = True
            q.append((h - 1, x))
    for y in range(1, h - 1):
        if empty[y, 0]:
            border_reach[y, 0] = True
            q.append((y, 0))
        if w > 1 and empty[y, w - 1]:
            border_reach[y, w - 1] = True
            q.append((y, w - 1))

    while q:
        cy, cx = q.popleft()
        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < h and 0 <= nx < w and empty[ny, nx] and not border_reach[ny, nx]:
                border_reach[ny, nx] = True
                q.append((ny, nx))

    enclosed = empty & (~border_reach)
    if not np.any(enclosed):
        return arr

    # Keep only small/medium enclosed pinholes (<= 1.2% of foreground area) so arm/leg gaps stay open
    max_hole_area = max(160, int(0.012 * fg_count))
    visited = np.zeros((h, w), dtype=bool)
    seal_mask = np.zeros((h, w), dtype=bool)
    ey, ex = np.where(enclosed)
    for sy, sx in zip(ey.tolist(), ex.tolist()):
        if visited[sy, sx]:
            continue
        comp = [(sy, sx)]
        visited[sy, sx] = True
        head = 0
        while head < len(comp):
            cy, cx = comp[head]
            head += 1
            for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                if 0 <= ny < h and 0 <= nx < w and enclosed[ny, nx] and not visited[ny, nx]:
                    visited[ny, nx] = True
                    comp.append((ny, nx))
        if len(comp) <= max_hole_area:
            for cy, cx in comp:
                seal_mask[cy, cx] = True

    n_sealed = int(np.sum(seal_mask))
    if n_sealed == 0:
        return arr

    # Propagate nearest opaque RGB into sealed pinhole pixels
    out = arr.copy()
    opaque = alpha >= 100
    cur_mask = seal_mask.copy()
    for _ in range(8):
        if not np.any(cur_mask):
            break
        py, px = np.where(cur_mask)
        progressed = False
        for cy, cx in zip(py.tolist(), px.tolist()):
            y0, y1 = max(0, cy - 1), min(h, cy + 2)
            x0, x1 = max(0, cx - 1), min(w, cx + 2)
            patch_op = opaque[y0:y1, x0:x1]
            if np.any(patch_op):
                rgb = out[y0:y1, x0:x1, :3][patch_op].mean(axis=0)
                out[cy, cx, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
                out[cy, cx, 3] = 255
                opaque[cy, cx] = True
                cur_mask[cy, cx] = False
                progressed = True
        if not progressed:
            break
    if np.any(cur_mask):
        # Fallback average foreground color for deep center of sealed micro-holes
        fg_rgb = out[opaque, :3].mean(axis=0) if np.any(opaque) else np.array([128, 128, 128])
        out[cur_mask, :3] = np.clip(fg_rgb, 0, 255).astype(np.uint8)
        out[cur_mask, 3] = 255

    print(f"[Studio3D] Sealed {n_sealed} enclosed interior cutout pinhole pixels.")
    return out


def prepare_studio_image(input_path: str, work_dir: str, skip_rembg: bool = False) -> str:
    """Ensures crisp 1024px RGBA foreground-isolated cutout with sealed pinholes."""
    img = Image.open(input_path).convert("RGBA")
    arr = np.array(img)
    alpha = arr[:, :, 3]
    transparent_frac = float(np.mean(alpha < 16))

    if not skip_rembg and transparent_frac < 0.03:
        try:
            from rembg import remove
            img = remove(img)
            arr = np.array(img)
            alpha = arr[:, :, 3]
        except Exception as exc:
            print(f"[Studio3D] Note: rembg skipped ({exc}); using source image.")

    # Zero only true background transparency dust (<12 alpha); never punch holes in translucent garments
    if np.any(alpha >= 200):
        arr[alpha < 12, 3] = 0
        alpha = arr[:, :, 3]

    rescue_haze = False
    fg_pixels = alpha >= 16
    if np.any(fg_pixels):
        semi_frac = float(np.mean(alpha[fg_pixels] < 180))
        if semi_frac > 0.25:
            rescue_haze = True
            print(
                f"[Studio3D] Warning: {semi_frac * 100:.0f}% of subject pixels are semi-transparent haze "
                "(likely repeated rembg pass); stabilizing interior alpha without hole punching."
            )

    arr = _seal_interior_pinholes(arr, rescue_haze=rescue_haze)
    img = Image.fromarray(arr, "RGBA")
    alpha = arr[:, :, 3]

    # Tight-crop to non-transparent subject and frame tightly onto 1024x1024 transparent canvas
    ys, xs = np.where(alpha >= 24)
    if len(xs) < 32 or len(ys) < 32:
        ys, xs = np.where(alpha >= 12)
    if len(xs) > 32 and len(ys) > 32:
        x0, x1 = int(xs.min()), int(xs.max()) + 1
        y0, y1 = int(ys.min()), int(ys.max()) + 1
        img = img.crop((x0, y0, x1, y1))

    w, h = img.size
    target = 1024
    scale = (target * 0.92) / max(1, max(w, h))
    nw, nh = max(16, int(round(w * scale))), max(16, int(round(h * scale)))
    img = img.resize((nw, nh), Image.Resampling.LANCZOS)

    # Gently sharpen narrow/low-res cutouts after upscaling so eyes/lips/seams stay crisp for AI 3D
    if w < 380 or h < 640:
        rgb_sharp = img.convert("RGB").filter(ImageFilter.UnsharpMask(radius=1.1, percent=110, threshold=2))
        r, g, b = rgb_sharp.split()
        a_ch = img.split()[3]
        img = Image.merge("RGBA", (r, g, b, a_ch))

    canvas = Image.new("RGBA", (target, target), (0, 0, 0, 0))
    canvas.paste(img, ((target - nw) // 2, (target - nh) // 2), img)
    out_png = os.path.join(work_dir, "prepared_cutout.png")
    canvas.save(out_png, "PNG")
    return out_png


def find_glb_urls(obj, base_url: str = "") -> list:
    urls = []
    if isinstance(obj, dict):
        u = obj.get("url")
        p = obj.get("path")
        if isinstance(u, str) and (".glb" in u.lower() or ".gltf" in u.lower()):
            urls.append(u)
        elif isinstance(p, str) and (".glb" in p.lower() or ".gltf" in p.lower()) and base_url:
            urls.append(f"{base_url}/file={p}")
        for v in obj.values():
            urls.extend(find_glb_urls(v, base_url))
    elif isinstance(obj, list):
        for it in obj:
            urls.extend(find_glb_urls(it, base_url))
    return urls


def pick_textured_glb_url(out: dict, base_url: str = "") -> str:
    """Prefers textured Hunyuan/Gradio output slot over white untextured base shape GLB."""
    urls = find_glb_urls(out, base_url)
    if not urls:
        return ""
    for u in urls:
        low = u.lower()
        if "textured" in low or "_tex" in low or "pbr" in low:
            return u
    data = out.get("data") if isinstance(out, dict) else None
    if isinstance(data, list) and len(data) >= 2:
        slot1_urls = find_glb_urls(data[1], base_url)
        if slot1_urls:
            return slot1_urls[-1]
    return urls[-1]


def hf_headers(token: str) -> dict:
    if token:
        return {"Authorization": f"Bearer {token}"}
    return {}


def gradio_upload(base_url: str, pfx: str, file_path: str, headers: dict) -> dict:
    with open(file_path, "rb") as f:
        r = requests.post(
            f"{base_url}{pfx}/upload",
            files={"files": (os.path.basename(file_path), f, "image/png")},
            headers=headers,
            timeout=30,
        )
    r.raise_for_status()
    up = r.json()[0]
    return {
        "path": up,
        "url": f"{base_url}{pfx}/file={up}",
        "orig_name": os.path.basename(file_path),
        "mime_type": "image/png",
        "meta": {"_type": "gradio.FileData"},
    }


def gradio_queue_call(base_url: str, pfx: str, fn_index: int, data: list, session_hash: str,
                      headers: dict, status_cb=None, timeout_s: int = 240) -> dict:
    join = requests.post(
        f"{base_url}{pfx}/queue/join",
        json={"data": data, "event_data": None, "fn_index": fn_index, "trigger_id": 1, "session_hash": session_hash},
        headers=headers,
        timeout=30,
    )
    join.raise_for_status()

    with requests.get(f"{base_url}{pfx}/queue/data?session_hash={session_hash}", headers=headers, stream=True, timeout=timeout_s) as s:
        s.raise_for_status()
        for line in s.iter_lines(decode_unicode=True):
            if not line or not line.startswith("data: "):
                continue
            try:
                msg = json.loads(line[6:])
            except Exception:
                continue
            mtype = msg.get("msg")
            if mtype == "estimation" and status_cb:
                rank = msg.get("rank", 0)
                eta = msg.get("rank_eta")
                eta_txt = f" (~{int(eta)}s)" if isinstance(eta, (int, float)) else ""
                status_cb(f"Queue position #{rank + 1}{eta_txt}...")
            elif mtype == "process_starts" and status_cb:
                eta = msg.get("eta")
                eta_txt = f" (~{int(eta)}s)" if isinstance(eta, (int, float)) else ""
                status_cb(f"GPU sculpting 3D mesh & textures{eta_txt}...")
            elif mtype == "process_completed":
                if not msg.get("success", False):
                    err = (msg.get("output") or {}).get("error") or msg.get("title") or "Gradio worker failed"
                    raise RuntimeError(str(err))
                return msg.get("output") or {}
    raise RuntimeError("Gradio stream closed without result")


def download_binary(url: str, dst_path: str, headers: dict):
    with requests.get(url, headers=headers, stream=True, timeout=120) as r:
        r.raise_for_status()
        with open(dst_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=65536):
                if chunk:
                    f.write(chunk)


def run_hf_sf3d(img_path: str, out_raw_glb: str, token: str, status_cb=None):
    base = "https://adil57-openforge-sf3d.hf.space"
    hdrs = hf_headers(token)
    cfg = requests.get(f"{base}/config", headers=hdrs, timeout=15).json()
    pfx = cfg.get("api_prefix") or ""
    fn_idx = next(d["id"] for d in cfg["dependencies"] if d.get("api_name") == "run_button")
    fd = gradio_upload(base, pfx, img_path, hdrs)
    sh = uuid.uuid4().hex[:12]
    out = gradio_queue_call(base, pfx, fn_idx, [fd, None, 0.85, "Triangle", 22000, 1024], sh, hdrs, status_cb)
    urls = find_glb_urls(out, f"{base}{pfx}")
    if not urls:
        raise RuntimeError("SF3D returned no GLB URL")
    download_binary(urls[0], out_raw_glb, hdrs)


def run_hf_hunyuan(img_path: str, out_raw_glb: str, token: str, version: str = "2mini", status_cb=None):
    base = "https://tencent-hunyuan3d-2-1.hf.space" if version == "21" else "https://tencent-hunyuan3d-2mini-turbo.hf.space"
    hdrs = hf_headers(token)
    cfg = requests.get(f"{base}/config", headers=hdrs, timeout=15).json()
    pfx = cfg.get("api_prefix") or ""
    dep = next(d for d in cfg["dependencies"] if d.get("api_name") == "generation_all")
    fn_idx = dep["id"]
    fd = gradio_upload(base, pfx, img_path, hdrs)
    sh = uuid.uuid4().hex[:12]
    # 13 inputs: [state/caption, image, front, back, left, right, steps, guidance, seed, octree, rembg, chunks, rand_seed]
    steps = 30 if version == "21" else 12
    octree = 384 if version == "21" else 256
    chunks = 10000 if version == "21" else 8000
    data = [None, fd, None, None, None, None, steps, 5.0, 1234, octree, False, chunks, True]
    out = gradio_queue_call(base, pfx, fn_idx, data, sh, hdrs, status_cb, timeout_s=300)
    url = pick_textured_glb_url(out, f"{base}{pfx}")
    if not url:
        raise RuntimeError(f"Hunyuan3D-{version} returned no GLB URL")
    download_binary(url, out_raw_glb, hdrs)


def run_hf_trellis2(img_path: str, out_raw_glb: str, token: str, status_cb=None,
                    trellis_res: str = "1024", texture_size: int = 2048):
    base = "https://microsoft-trellis-2.hf.space"
    hdrs = hf_headers(token)
    cfg = requests.get(f"{base}/config", headers=hdrs, timeout=15).json()
    pfx = cfg.get("api_prefix") or ""
    deps = {d.get("api_name"): d["id"] for d in cfg["dependencies"] if d.get("api_name")}
    fd = gradio_upload(base, pfx, img_path, hdrs)
    sh = uuid.uuid4().hex[:12]
    if "start_session" in deps:
        gradio_queue_call(base, pfx, deps["start_session"], [], sh, hdrs)
    res_gen = gradio_queue_call(
        base, pfx, deps["image_to_3d"],
        [fd, 42, trellis_res, 7.5, 0.7, 14, 5.0, 7.5, 0.5, 14, 3.0, 1.0, 0.0, 14, 3.0],
        sh, hdrs, status_cb, timeout_s=240,
    )
    state_val = (res_gen.get("data") or [None])[0]
    if status_cb:
        status_cb(f"TRELLIS.2 extracting {texture_size}px UV-mapped PBR GLB...")
    res_glb = gradio_queue_call(base, pfx, deps["extract_glb"], [state_val, 300000, int(texture_size)], sh, hdrs, status_cb)
    urls = find_glb_urls(res_glb, f"{base}{pfx}")
    if not urls:
        raise RuntimeError("TRELLIS.2 returned no GLB URL")
    download_binary(urls[0], out_raw_glb, hdrs)


def run_hf_instantmesh(img_path: str, out_raw_glb: str, token: str, status_cb=None):
    base = "https://tencentarc-instantmesh.hf.space"
    hdrs = hf_headers(token)
    cfg = requests.get(f"{base}/config", headers=hdrs, timeout=15).json()
    pfx = cfg.get("api_prefix") or ""
    deps = {d.get("api_name"): d["id"] for d in cfg["dependencies"] if d.get("api_name")}
    fd = gradio_upload(base, pfx, img_path, hdrs)
    sh = uuid.uuid4().hex[:12]
    mvs = gradio_queue_call(base, pfx, deps["generate_mvs"], [fd, 50, 42], sh, hdrs, status_cb)
    mv_state = (mvs.get("data") or [None])[0]
    if status_cb:
        status_cb("InstantMesh reconstructing 3D mesh...")
    res = gradio_queue_call(base, pfx, deps["make3d"], [mv_state], sh, hdrs, status_cb)
    urls = find_glb_urls(res, f"{base}{pfx}")
    if not urls:
        raise RuntimeError("InstantMesh returned no GLB URL")
    download_binary(urls[0], out_raw_glb, hdrs)


def run_hf_triposg(img_path: str, out_raw_glb: str, token: str, status_cb=None):
    base = "https://vast-ai-triposg.hf.space"
    hdrs = hf_headers(token)
    cfg = requests.get(f"{base}/config", headers=hdrs, timeout=15).json()
    pfx = cfg.get("api_prefix") or ""
    deps = {d.get("api_name"): d["id"] for d in cfg["dependencies"] if d.get("api_name")}
    fd = gradio_upload(base, pfx, img_path, hdrs)
    sh = uuid.uuid4().hex[:12]
    if "start_session" in deps:
        gradio_queue_call(base, pfx, deps["start_session"], [], sh, hdrs)
    seg = gradio_queue_call(base, pfx, deps["run_segmentation"], [fd], sh, hdrs)
    seg_img = (seg.get("data") or [fd])[0]
    mesh = gradio_queue_call(base, pfx, deps["image_to_3d"], [seg_img, 42, 32, 7.0, True, 35000], sh, hdrs, status_cb)
    raw_mesh = (mesh.get("data") or [None])[0]
    if "run_texture" in deps and raw_mesh:
        if status_cb:
            status_cb("TripoSG synthesizing 360° textures...")
        tex = gradio_queue_call(base, pfx, deps["run_texture"], [fd, raw_mesh, 42], sh, hdrs, status_cb)
        urls = find_glb_urls(tex, f"{base}{pfx}") or find_glb_urls(mesh, f"{base}{pfx}")
    else:
        urls = find_glb_urls(mesh, f"{base}{pfx}")
    if not urls:
        raise RuntimeError("TripoSG returned no GLB URL")
    download_binary(urls[0], out_raw_glb, hdrs)


def run_tripo_api(img_path: str, out_raw_glb: str, api_key: str, status_cb=None):
    if not api_key:
        raise RuntimeError("TRIPO_API_KEY not configured")
    hdrs = {"Authorization": f"Bearer {api_key}"}
    if status_cb:
        status_cb("Uploading photo to Tripo3D Studio API...")
    with open(img_path, "rb") as f:
        up = requests.post(
            "https://api.tripo3d.ai/v2/openapi/upload",
            headers=hdrs,
            files={"file": (os.path.basename(img_path), f, "image/png")},
            timeout=60,
        )
    up.raise_for_status()
    img_token = (up.json().get("data") or {}).get("image_token")
    if not img_token:
        raise RuntimeError(f"Tripo upload failed: {up.text}")

    task_resp = requests.post(
        "https://api.tripo3d.ai/v2/openapi/task",
        headers={**hdrs, "Content-Type": "application/json"},
        json={
            "type": "image_to_model",
            "file": {"type": "png", "file_token": img_token},
            "texture": True,
            "pbr": True,
        },
        timeout=30,
    )
    task_resp.raise_for_status()
    task_id = (task_resp.json().get("data") or {}).get("task_id")
    if not task_id:
        raise RuntimeError(f"Tripo task creation failed: {task_resp.text}")

    deadline = time.time() + 360
    while time.time() < deadline:
        st = requests.get(f"https://api.tripo3d.ai/v2/openapi/task/{task_id}", headers=hdrs, timeout=30)
        st.raise_for_status()
        d = st.json().get("data") or {}
        prog = d.get("progress", 0)
        status = d.get("status", "")
        if status_cb:
            status_cb(f"Tripo3D Studio generating PBR model ({prog}%)...")
        if status == "success":
            out = d.get("output") or {}
            model_url = out.get("pbr_model") or out.get("model")
            if not model_url:
                raise RuntimeError("Tripo task succeeded without model URL")
            download_binary(model_url, out_raw_glb, {})
            return
        if status in ("failed", "cancelled", "unknown"):
            raise RuntimeError(f"Tripo task failed: {status}")
        time.sleep(2.5)
    raise RuntimeError("Tripo task timed out")


def run_fal_api(img_path: str, out_raw_glb: str, fal_key: str, status_cb=None):
    if not fal_key:
        raise RuntimeError("FAL_KEY not configured")
    import base64
    with open(img_path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    data_uri = f"data:image/png;base64,{b64}"
    hdrs = {"Authorization": f"Key {fal_key}", "Content-Type": "application/json"}
    if status_cb:
        status_cb("Submitting to fal.ai Hunyuan3D v2...")
    sub = requests.post(
        "https://queue.fal.run/fal-ai/hunyuan3d/v2",
        headers=hdrs,
        json={"input_image_url": data_uri, "textured_mesh": True},
        timeout=60,
    )
    sub.raise_for_status()
    j = sub.json()
    status_url = j.get("status_url")
    resp_url = j.get("response_url")
    deadline = time.time() + 300
    while time.time() < deadline and status_url:
        st = requests.get(status_url, headers={"Authorization": f"Key {fal_key}"}, timeout=30).json()
        if st.get("status") == "COMPLETED":
            break
        if st.get("status") in ("FAILED", "CANCELLED"):
            raise RuntimeError(f"fal.ai job failed: {st}")
        if status_cb:
            status_cb(f"fal.ai Hunyuan3D v2: {st.get('status', 'QUEUED')}...")
        time.sleep(2.5)
    res = requests.get(resp_url, headers={"Authorization": f"Key {fal_key}"}, timeout=60).json()
    urls = find_glb_urls(res)
    if not urls:
        raise RuntimeError(f"fal.ai returned no GLB: {res}")
    download_binary(urls[0], out_raw_glb, {})


def run_blender_autorig(script_dir: str, raw_mesh: str, output_glb: str, ref_anim: str,
                        height_m: float, front_yaw: str, max_faces: int):
    rig_script = os.path.join(script_dir, "surreal_blender_rig.py")
    cmd = [
        "blender", "--background", "--python", rig_script, "--",
        raw_mesh, output_glb,
        "--height", str(height_m),
        "--front-yaw", front_yaw,
        "--max-faces", str(max_faces),
    ]
    if ref_anim:
        cmd.extend(["--ref-anim", ref_anim])
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if proc.returncode != 0 or not os.path.exists(output_glb):
        tail = (proc.stderr or proc.stdout or "")[-1200:]
        raise RuntimeError(f"Blender auto-rig failed (exit {proc.returncode}): {tail}")


def main():
    parser = argparse.ArgumentParser(description="Surreal Studio AI 3D + Blender Auto-Rigger")
    parser.add_argument("input", help="Source image (.png/.jpg) or static 3D mesh (.glb/.obj)")
    parser.add_argument("--out", default="", help="Output rigged .glb path")
    parser.add_argument("--backend", default="", help="auto|glb_rig|hf_sf3d|hf_trellis2|hf_hunyuan2mini|hf_hunyuan21|hf_instantmesh|hf_triposg|tripo|fal")
    parser.add_argument("--ref-anim", default="", help="Reference Mixamo dance .glb")
    parser.add_argument("--height", type=float, default=1.68, help="Normalized height in meters")
    parser.add_argument("--front-yaw", default="auto", help="auto|0|90|180|270")
    parser.add_argument("--max-faces", type=int, default=100000, help="Polygon budget")
    parser.add_argument("--trellis-res", default="1024", choices=["1024", "1536"], help="TRELLIS.2 pipeline resolution")
    parser.add_argument("--texture-size", type=int, default=2048, help="TRELLIS.2 UV texture size")
    parser.add_argument("--skip-rembg", action="store_true", help="Skip background removal")
    parser.add_argument("--no-rig", action="store_true", help="Keep unrigged raw studio .glb")
    parser.add_argument("--status-file", default=DEFAULT_STATUS_FILE, help="Live JSON progress path")
    parser.add_argument("--notify-engine", action="store_true", help="Notify running Surreal Engine via /tmp/surreal_studio_ready.json")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    cfg = load_studio_config()
    in_path = os.path.abspath(args.input)
    if not os.path.exists(in_path):
        write_status(args.status_file, "error", 0.0, f"Input not found: {in_path}", error="File not found")
        sys.exit(1)

    ext = os.path.splitext(in_path)[1].lower()
    is_mesh_input = ext in (".glb", ".gltf", ".obj", ".fbx")
    stem = sanitize_stem(in_path)
    out_glb = os.path.abspath(args.out) if args.out else os.path.join(default_output_dir(script_dir), f"{stem}_studio_rigged.glb")

    backend = (args.backend or cfg.get("backend") or "auto").strip()
    if is_mesh_input:
        backend = "glb_rig"

    try:
        with tempfile.TemporaryDirectory(prefix="surreal_studio_") as tmpdir:
            raw_glb = in_path
            if not is_mesh_input:
                write_status(args.status_file, "running", 0.08, "Stage 1/3: Isolating foreground subject (rembg)...")
                prep_png = prepare_studio_image(in_path, tmpdir, skip_rembg=args.skip_rembg)
                raw_glb = os.path.join(tmpdir, "studio_raw.glb")

                hf_tok = cfg.get("hf_token", "")
                tripo_key = cfg.get("tripo_api_key", "")
                fal_key = cfg.get("fal_key", "")

                if backend == "auto":
                    chain = []
                    if tripo_key:
                        chain.append("tripo")
                    if fal_key:
                        chain.append("fal")
                    if hf_tok:
                        chain.extend(["hf_hunyuan21", "hf_trellis2", "hf_hunyuan2mini", "hf_instantmesh", "hf_triposg"])
                    else:
                        chain.extend(["hf_trellis2", "hf_hunyuan2mini", "hf_instantmesh", "hf_triposg"])
                else:
                    chain = [backend]

                last_err = None
                quota_hit = False
                for cand in chain:
                    try:
                        def cb(msg, c=cand):
                            write_status(args.status_file, "running", 0.35, f"Stage 2/3 [{c}]: {msg}")

                        write_status(args.status_file, "running", 0.20, f"Stage 2/3: Requesting Studio 3D mesh from {cand}...")
                        if cand == "tripo":
                            run_tripo_api(prep_png, raw_glb, tripo_key, cb)
                        elif cand == "fal":
                            run_fal_api(prep_png, raw_glb, fal_key, cb)
                        elif cand == "hf_sf3d":
                            run_hf_sf3d(prep_png, raw_glb, hf_tok, cb)
                        elif cand == "hf_hunyuan21":
                            run_hf_hunyuan(prep_png, raw_glb, hf_tok, "21", cb)
                        elif cand == "hf_hunyuan2mini":
                            run_hf_hunyuan(prep_png, raw_glb, hf_tok, "2mini", cb)
                        elif cand == "hf_trellis2":
                            run_hf_trellis2(
                                prep_png, raw_glb, hf_tok, cb,
                                trellis_res=args.trellis_res, texture_size=args.texture_size
                            )
                        elif cand == "hf_instantmesh":
                            run_hf_instantmesh(prep_png, raw_glb, hf_tok, cb)
                        elif cand == "hf_triposg":
                            run_hf_triposg(prep_png, raw_glb, hf_tok, cb)
                        else:
                            raise RuntimeError(f"Unknown backend '{cand}'")
                        if os.path.exists(raw_glb) and os.path.getsize(raw_glb) > 1024:
                            last_err = None
                            break
                        raise RuntimeError("Empty GLB output")
                    except Exception as exc:
                        err_txt = str(exc)
                        last_err = f"{cand}: {err_txt}"
                        print(f"[Studio3D] Backend {cand} skipped: {err_txt}", file=sys.stderr, flush=True)
                        if "zerogpu" in err_txt.lower() or "0s left" in err_txt.lower() or "exceeded" in err_txt.lower():
                            quota_hit = True
                            if backend == "auto":
                                break
                        write_status(args.status_file, "running", 0.25, f"Backend {cand} skipped ({err_txt[:100]})...")

                if last_err is not None:
                    if quota_hit:
                        if not hf_tok:
                            raise RuntimeError(
                                "ZeroGPU-Kontingent ohne HF_TOKEN erschoepft (0s anonym). "
                                "Loesung: Kostenlosen HF_TOKEN (huggingface.co/settings/tokens -> ~5 Min GPU/Tag gratis) eintragen "
                                "oder fertige .glb von studio.tripo3d.ai / 3d.hunyuan.tencent.com direkt hier laden."
                            )
                        raise RuntimeError(
                            "HF_TOKEN Tages-Kontingent auf ZeroGPU erschoepft (~5 Min GPU/Tag). "
                            "Tipp: Fertige .glb kostenlos auf studio.tripo3d.ai oder 3d.hunyuan.tencent.com exportieren "
                            "und hier mit 'Local Blender Auto-Rig Only (.glb/.obj Input)' automatisch riggen!"
                        )
                    raise RuntimeError(
                        "Studio 3D Backends fehlgeschlagen (" + last_err + "). "
                        "Tipp: Pruefe HF_TOKEN/API-Key oder lade eine .glb direkt per lokalem Blender Auto-Rig."
                    )

            if args.no_rig:
                os.makedirs(os.path.dirname(out_glb), exist_ok=True)
                with open(raw_glb, "rb") as src, open(out_glb, "wb") as dst:
                    dst.write(src.read())
            else:
                write_status(args.status_file, "running", 0.78, "Stage 3/3: Blender Auto-Rigging Mixamo dance skeleton & skin weights...")
                run_blender_autorig(
                    script_dir, raw_glb, out_glb,
                    args.ref_anim, args.height, args.front_yaw, args.max_faces
                )

        if args.notify_engine:
            ready = {
                "path": out_glb,
                "height": args.height,
                "name": stem + "_Studio",
                "timestamp": time.time(),
            }
            tmp_r = STUDIO_READY_FILE + ".tmp"
            with open(tmp_r, "w", encoding="utf-8") as f:
                json.dump(ready, f, indent=2)
            os.replace(tmp_r, STUDIO_READY_FILE)

        write_status(args.status_file, "done", 1.0, f"Studio 3D Model ready: {os.path.basename(out_glb)}", output_glb=out_glb)
    except Exception as exc:
        write_status(args.status_file, "error", 0.0, f"Studio 3D failed: {exc}", error=str(exc))
        print(f"[-] Studio3D Error: {exc}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()

