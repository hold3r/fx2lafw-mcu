"""Compile the real protocol and TinyUSB FIFO for a host regression test.

Requires a native C11 compiler (clang by default). USB and capture are modeled;
this does not replace a hardware test. Optional argument selects an older
protocol source for reproducing the regression.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        root / "src/fx2lafw_protocol.c"
    )
    compiler = shutil.which(os.environ.get("CC", "clang"))
    if compiler is None:
        raise SystemExit("Native C compiler missing; set CC or install clang.")
    # Windows clang supports GNU attributes but defaults to MSVC macros.
    # TinyUSB selects its supported attribute implementation via __GNUC__.
    compiler_flags = (["-fgnuc-version=4.2.1"]
                      if os.name == "nt" and "clang" in Path(compiler).name else [])
    build = root / "build/verification"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        work = Path(temporary)
        # Legacy source needs critical section macros; the fixed source does not.
        (work / "FreeRTOS.h").write_text("#pragma once\n", encoding="utf-8")
        (work / "task.h").write_text(
            "#pragma once\n#define taskENTER_CRITICAL() ((void)0)\n"
            "#define taskEXIT_CRITICAL() ((void)0)\n", encoding="utf-8"
        )
        executable = work / ("protocol_stop.exe" if os.name == "nt" else "protocol_stop")
        subprocess.run([
            compiler, *compiler_flags, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DCFG_TUSB_MCU=OPT_MCU_NONE", "-DCFG_TUSB_OS=OPT_OS_NONE",
            "-DCFG_TUSB_DEBUG=0", "-DTUP_DCD_ENDPOINT_MAX=8",
            "-I" + str(work), "-I" + str(root / "tests/include"),
            "-I" + str(root / "src"), "-I" + str(root / "src/config"),
            "-I" + str(root / "lib/tinyusb/src"),
            str(root / "tests/protocol_stop.c"), str(source),
            str(root / "lib/tinyusb/src/common/tusb_fifo.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
