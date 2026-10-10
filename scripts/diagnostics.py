#!/usr/bin/env python3
"""Preflight native binaries without starting the game or touching saves."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def check(probe, capabilities):
    if not probe.is_file():
        raise RuntimeError(f"运行程序不存在：{probe}。请重新安装完整运行包。")
    if shutil.which("ldd"):
        result = subprocess.run(["ldd", str(probe)], capture_output=True, text=True, timeout=15)
        missing = [line.strip() for line in result.stdout.splitlines() if "not found" in line]
        if missing:
            raise RuntimeError("缺少运行库：\n" + "\n".join(missing) + "\n请恢复完整运行包，不要混用不同版本的库。")
    result = subprocess.run([str(capabilities), "--diagnose"], capture_output=True, text=True, timeout=20)
    print(result.stdout, end="", flush=True)
    if result.returncode:
        raise RuntimeError("显卡初始化失败。请检查 NVIDIA 驱动、Vulkan ICD 和运行库版本。\n" + result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("capabilities", type=Path)
    args = parser.parse_args()
    try:
        check(args.probe.resolve(), args.capabilities.resolve())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"启动诊断：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
