#!/usr/bin/env python3
"""Summarize one SM lab session from its run directory."""
import json
import re
import sys
from pathlib import Path


def main() -> None:
    run = Path(sys.argv[1])
    log = run / "game.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    presents = re.findall(r"FG_PRESENT kind=(\w+)", text)
    real = sum(1 for p in presents if p == "real")
    gen = sum(1 for p in presents if p == "generated")
    bad = [line for line in text.splitlines()
           if re.search(r"DeviceLost|device lost|ErrorDeviceLost|Assertion Failed", line)]
    marks = [line.split(" ", 1)[0] for line in text.splitlines()
             if line.startswith(("SM_EXIT_", "FG_EXIT_", "FG_DRAW_RETIRED", "FG_PRESENT_RETIRED",
                                 "FG_RETIRE_HOLD", "NATIVE_ASYNC_EXIT"))]
    nv = run / "nvpresent.log"
    nvtext = nv.read_text(errors="replace") if nv.exists() else ""
    status = {
        "run": run.name,
        "game_log_bytes": len(text),
        "real_presents": real,
        "generated_presents": gen,
        "error_lines": bad[:3],
        "milestones": marks,
        "nvpresent": {
            "enabled": "Smooth Motion enabled             [1]" in nvtext,
            "supported": "System support: 1. Device support: 1" in nvtext,
            "passthrough": "passthrough swapchain" in nvtext,
            "swapchains_created": nvtext.count("NVP_Swapchain (Handle="),
            "swapchains_failed": nvtext.count("NVP_Swapchain creation failed"),
        },
        "shot": str(run / "before-close.png") if (run / "before-close.png").exists() else None,
    }
    print("SUMMARY " + json.dumps(status, ensure_ascii=False))


if __name__ == "__main__":
    main()
