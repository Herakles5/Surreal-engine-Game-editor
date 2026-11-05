#!/usr/bin/env python3
"""
AION GAME MASTER BRAIN & NARRATIVE DIRECTOR
============================================
Powers Aion as the intelligent Game Master (GM) of Surreal 3D Studio & Dojo:
1. Dispatches procedural quests and trials into the running engine via /tmp/aion_gm_request.json.
2. Directs the player with dynamic voice commentary and story lore via Piper TTS and Aion Bridge.
3. Adapts world challenges, weather, and gravity dynamically.
"""

import os
import sys
import time
import json
import socket
import argparse
from typing import Optional

GM_REQUEST_FILE = "/tmp/aion_gm_request.json"
BRIDGE_IN_PIPE = "/tmp/aion_ai_thought.txt"

# Piper TTS integration
try:
    sys.path.append("/root/coding/MeinOS")
    from aion_streaming_tts import AionStreamingTTS
    tts_engine = AionStreamingTTS()
except Exception as e:
    tts_engine = None


def send_gm_command(trial_code: int, trial_name: str, narrate_text: Optional[str] = None):
    """Writes a Game Master directive to the engine."""
    payload = {
        "timestamp": time.time(),
        "trial": trial_code,
        "name": trial_name,
        "director": "AION_AI_GAME_MASTER"
    }
    with open(GM_REQUEST_FILE, "w") as f:
        json.dump(payload, f)
    print(f"[AION GM] Dispatched trial: {trial_name} (code {trial_code})")

    if narrate_text:
        speak(narrate_text)


def speak(text: str):
    """Speaks commentary out loud through Aion's voice engine and bridges it to IDE."""
    print(f"[AION VOICE] {text}")
    try:
        # Write to bridge file for IDE & UI capture
        with open(BRIDGE_IN_PIPE, "a") as f:
            f.write(f"[AION GM] {text}\n")
    except Exception:
        pass

    if tts_engine:
        try:
            tts_engine.speak(text)
        except Exception as e:
            print(f"[AION GM] TTS error: {e}")


def main():
    parser = argparse.ArgumentParser(description="Aion Game Master CLI & Director")
    subparsers = parser.add_subparsers(dest="cmd")

    start_p = subparsers.add_parser("start", help="Start a Game Master trial")
    start_p.add_argument("type", choices=["parkour", "quantum", "lunar", "storm", "stop"])

    narrate_p = subparsers.add_parser("narrate", help="Narrate a voice event")
    narrate_p.add_argument("text", type=str)

    args = parser.parse_args()

    if args.cmd == "start":
        mapping = {
            "parkour": (1, "PARKOUR", "Initiating Metropolis Rooftop Sprint. Race through the neon gates!"),
            "quantum": (2, "QUANTUM", "Quantum Cores detected in Sector 4. Harvest all energy signatures!"),
            "lunar": (3, "LUNAR", "Low gravity field activated. Prepare for high-altitude skyline leaps!"),
            "storm": (4, "STORM", "Cyber storm incoming. Midnight infiltration protocol active!"),
            "stop": (0, "STOP", "Trial terminated. Returning to free exploration mode.")
        }
        code, name, speech = mapping[args.type]
        send_gm_command(code, name, speech)

    elif args.cmd == "narrate":
        speak(args.text)

    else:
        parser.print_help()


if __name__ == "__main__":
    main()

