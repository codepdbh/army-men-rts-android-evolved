"""Compile our fixed-function shaders to SPIR-V with the Vulkan 1.1 target."""
import os
from pathlib import Path
import subprocess
import struct
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / "android/app/src/main/cpp/shaders"
sdk = Path(os.environ.get("ANDROID_HOME", Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk"))
compiler = sdk / "ndk/28.2.13676358/shader-tools/windows-x86_64/glslc.exe"
header = ["#pragma once", "#include <cstdint>"]
with tempfile.TemporaryDirectory() as directory:
    for stage in ("vert", "frag"):
        binary = Path(directory) / (stage + ".spv")
        subprocess.run([str(compiler), "--target-env=vulkan1.1", "-O", str(source / ("fixed." + stage)), "-o", str(binary)], check=True)
        data = binary.read_bytes()
        words = struct.unpack("<" + "I" * (len(data) // 4), data)
        header.append("constexpr uint32_t fixed_" + stage + "[]={")
        for i in range(0, len(words), 8):
            header.append(",".join(f"0x{value:08x}" for value in words[i:i + 8]) + ",")
        header.append("};")
(source / "fixed_spirv.h").write_text("\n".join(header) + "\n", encoding="utf-8")
