"""在主机模拟网关和终端的 v3 双向事务，不操作真实串口或开发板。"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc",default=shutil.which("gcc"))
    args=parser.parse_args()
    if not args.cc:parser.error("找不到主机GCC")
    root=Path(__file__).resolve().parents[2]
    here=Path(__file__).resolve().parent
    output=root/"build"/"lora-duplex-tests"
    output.mkdir(parents=True,exist_ok=True)
    binary=output/("duplex.exe" if os.name=="nt" else "duplex")
    command=[args.cc,"-std=c11","-Wall","-Wextra","-Werror","-Wconversion","-Wsign-conversion",
             "-pedantic","-O0","-g","-finput-charset=UTF-8"]
    for directory in ("User/LoRa","User/service"):
        command += ["-I",str(root/directory)]
    sources=[here/"test_duplex.c"]+[root/"User/LoRa"/name for name in
        ("lora_protocol.c","lora_reliable.c","terminal_commands.c","gateway_command.c")]
    subprocess.run(command+list(map(str,sources))+["-o",str(binary)],check=True,cwd=root)
    subprocess.run([str(binary)],check=True,cwd=root)

if __name__=="__main__":main()
