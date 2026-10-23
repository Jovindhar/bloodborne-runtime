#!/usr/bin/env python3
"""Copies the launcher's setting texts into the in-game menu's locales as launcher.* keys, so the
menu uses the launcher's words in every language (the menu mirrors its Graphics, Game and Advanced
pages).
  import_launcher.py LAUNCHER_LOCALES_DIR    (bloodborne-launcher src/i18n/locales)
Texts with {{placeholders}} are not copied. Run gen_text.py --check afterwards."""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Dotted paths in the launcher's files: a text, or a group whose texts are all copied.
PATHS = [
    "graphics.title", "graphics.display", "graphics.aspectRatio", "graphics.aspectRatioHint", "graphics.aspectAuto", "graphics.frameRate", "graphics.upscaling", "graphics.upscalingHint",
    "graphics.detail", "graphics.effects", "graphics.effectsHint", "graphics.outputRes", "graphics.fullscreen",
    "graphics.memory", "graphics.memoryHint", "graphics.memoryHigh",
    "graphics.monitor", "graphics.monitorHint", "graphics.monitorMain", "graphics.presentMode",
    "graphics.fpsMode", "graphics.fpsLimit", "graphics.fpsLimitHint", "graphics.showFps",
    "graphics.fpsSize", "graphics.fpsPosition",
    "graphics.metalHud", "graphics.metalHudHint", "graphics.upscaler", "graphics.preset",
    "graphics.sharpen", "graphics.sharpness", "graphics.objectMotion", "graphics.objectMotionHint",
    "graphics.modelLod",
    "game.title", "game.extras", "game.skipIntro",
    "advanced.title", "advanced.performance", "advanced.renderer", "advanced.rendererHint", "advanced.drawPipe", "advanced.readbacks", "advanced.preupload",
    "advanced.liveResolution", "advanced.liveResolutionHint", "advanced.diagnostics", "advanced.debugCamera",
    "advanced.debugCameraHint", "advanced.debugMenu", "advanced.debugMenuHint",
    "common.restartNote", "common.off",
    "options.preset", "options.fps", "options.fpsSize", "options.fpsPosition", "options.lod", "options.present", "options.drawPipe", "options.renderer",
    "options.readbacks", "options.preupload", "options.live", "options.effects", "options.upscaler",
]


def texts(tree, path):
    node = tree
    for part in path.split("."):
        node = node[part]
    if isinstance(node, str):
        return {path: node}
    return {f"{path}.{key}": text for key, text in node.items() if isinstance(text, str)}


def main():
    source = Path(sys.argv[1])
    for target in sorted(HERE.glob("*.json")):
        tree = json.loads((source / target.name).read_text(encoding="utf-8"))
        menu = {k: v for k, v in json.loads(target.read_text(encoding="utf-8")).items()
                if not k.startswith("launcher.")}
        for path in PATHS:
            for key, text in texts(tree, path).items():
                if "{{" not in text:
                    menu["launcher." + key] = text
        target.write_text(json.dumps(menu, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"{target.name}: {sum(k.startswith('launcher.') for k in menu)} launcher texts")


main()
