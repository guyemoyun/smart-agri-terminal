"""运行终端本地控制的主机测试，不连接或操作开发板。"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("找不到主机 GCC")
    root = Path(__file__).resolve().parents[2]
    here = Path(__file__).resolve().parent
    output = root / "build" / "terminal-tests"
    output.mkdir(parents=True, exist_ok=True)
    common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wsign-conversion",
              "-pedantic", "-O0", "-g", "-finput-charset=UTF-8"]
    for folder in ("User/service", "User/bsp", "User/LoRa", "User/oled", "User/app"):
        common += ["-I", str(root / folder)]
    programs = {
        "policy": [here / "test_policy.c", root / "User/service/control_policy.c",
                   root / "User/service/button_state.c", root / "User/bsp/co2_parser.c",
                   root / "User/LoRa/terminal_telemetry.c", root / "User/service/terminal_view.c"],
        "actuator": [here / "test_actuator.c", root / "User/service/actuator_service.c",
                     root / "User/service/motor_control.c"],
        "co2_driver": [here / "test_co2_driver.c", root / "User/bsp/CO2.c", root / "User/bsp/co2_parser.c"],
        "services": [here / "test_services.c", root / "User/service/control_policy.c",
                     root / "User/service/control_service.c", root / "User/service/actuator_service.c",
                     root / "User/service/motor_control.c", root / "User/service/button_state.c",
                     root / "User/service/button_service.c", root / "User/service/terminal_ui.c",
                     root / "User/service/terminal_view.c"],
        "radio": [here / "test_radio.c", here / "radio_unused_stubs.c", root / "User/LoRa/llcc68_p2p.c"],
        "link_service": [here / "test_link_service.c", root / "User/LoRa/lora_service.c",
                         root / "User/LoRa/terminal_telemetry.c"]
    }
    for name, sources in programs.items():
        if name in ("policy", "link_service", "services"):
            sources = sources + [root / "User/LoRa/lora_protocol.c"]
        if name == "link_service":
            sources += [root / "User/LoRa/lora_reliable.c", root / "User/LoRa/terminal_commands.c"]
        if name == "services":
            sources += [root / "User/LoRa/terminal_commands.c", root / "User/LoRa/terminal_command_control.c"]
        binary = output / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run(common + ["-I", str(here / "stubs")] + list(map(str, sources)) + ["-o", str(binary), "-lm"], check=True, cwd=root)
        subprocess.run([str(binary)], check=True, cwd=root)


if __name__ == "__main__":
    main()
