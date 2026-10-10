"""编译真实电机驱动与主机 HAL 模拟，运行软件单元测试，不访问硬件。"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"), help="主机 GCC 路径")
    args = parser.parse_args()
    if not args.cc:
        parser.error("未找到主机 GCC，请使用 --cc 指定编译器")

    root = Path(__file__).resolve().parents[2]
    test_dir = Path(__file__).resolve().parent
    build_dir = root / "build" / "motor-control-tests"
    build_dir.mkdir(parents=True, exist_ok=True)
    binary = build_dir / ("motor_control_tests.exe" if os.name == "nt" else "motor_control_tests")
    cmd = [
        args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion",
        "-Wsign-conversion", "-pedantic", "-O0", "-g", "-finput-charset=UTF-8",
        "-I", str(test_dir / "stubs"), "-I", str(root / "User" / "service"),
        str(test_dir / "test_motor_control.c"),
        str(root / "User" / "service" / "motor_control.c"),
        "-o", str(binary),
    ]
    subprocess.run(cmd, check=True, cwd=root)
    subprocess.run([str(binary)], check=True, cwd=root)


if __name__ == "__main__":
    main()
