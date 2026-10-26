"""生成 qmake 的 .qmake.stash 缓存，绕过 qmake 的编译器自动探测。

背景：本机受限环境中 qmake 通过 QProcess 读取编译器输出时会失败
（QProcess: CreateFile failed → COMPILER_MACROS / INCDIRS 解析失败），
导致 toolchain.prf 无法自动收集编译器信息。这里预先探测并写入 qmake 缓存。

qmake 缓存语法：  cache(<变量名>, set stash, <保存值的变量名>)
"""

import re
import subprocess
from pathlib import Path

GXX = r"D:\Qt\Tools\mingw1310_64\bin\g++.exe"
OUT = Path(__file__).resolve().parent.parent / ".qmake.stash"


def clean(p: str) -> str:
    """模拟 qmake 的 clean_path：统一分隔符并折叠 ".." 段。"""
    p = p.strip().replace("\\", "/")
    p = re.sub(r"/+", "/", p)
    parts = []
    for seg in p.split("/"):
        if seg == ".." and len(parts) > 1 and parts[-1] not in ("", ".."):
            parts.pop()
        else:
            parts.append(seg)
    return "/".join(parts)


def main() -> None:
    verbose = subprocess.run([GXX, "-E", "-v", "-xc++", "-"],
                             stdin=subprocess.DEVNULL,
                             capture_output=True, text=True, errors="replace")
    out = verbose.stdout + verbose.stderr

    incdirs, libdirs = [], []
    add_includes = False
    for raw in out.splitlines():
        line = raw.strip()
        if "#include <" in line:
            add_includes = True
            continue
        if line.startswith("End of search"):
            add_includes = False
            continue
        if line.startswith("LIBRARY_PATH="):
            for part in line[len("LIBRARY_PATH="):].split(";"):
                if part.strip():
                    libdirs.append(clean(part))
            continue
        if add_includes and line:
            incdirs.append(clean(line))

    macro_out = subprocess.run([GXX, "-E", "-dM", "-xc++", "-"],
                               stdin=subprocess.DEVNULL,
                               capture_output=True, text=True, errors="replace")
    macros = macro_out.stdout or macro_out.stderr

    def macro(name: str) -> str:
        m = re.search(r"^#define\s+" + re.escape(name) + r"\s+(\S+)", macros, re.M)
        return m.group(1).strip() if m else "0"

    major, minor, patch = macro("__GNUC__"), macro("__GNUC_MINOR__"), macro("__GNUC_PATCHLEVEL__")

    ver_line = ""
    for line in out.splitlines():
        if "gcc version" in line:
            ver_line = line.strip()
            break

    lines = [
        "# 由 tools/gen_qmake_stash.py 生成：本机 qmake 无法自动探测编译器，",
        "# 故预先写入探测结果（qmake 读的是 .qmake.stash 缓存）。",
        "",
        "QMAKE_GCC_MAJOR_VERSION = %s" % major,
        "QMAKE_GCC_MINOR_VERSION = %s" % minor,
        "QMAKE_GCC_PATCH_VERSION = %s" % patch,
        "cache(QMAKE_GCC_MAJOR_VERSION, set stash, QMAKE_GCC_MAJOR_VERSION)",
        "cache(QMAKE_GCC_MINOR_VERSION, set stash, QMAKE_GCC_MINOR_VERSION)",
        "cache(QMAKE_GCC_PATCH_VERSION, set stash, QMAKE_GCC_PATCH_VERSION)",
        "cache(QMAKE_CXX.QMAKE_GCC_MAJOR_VERSION, set stash, QMAKE_GCC_MAJOR_VERSION)",
        "cache(QMAKE_CXX.QMAKE_GCC_MINOR_VERSION, set stash, QMAKE_GCC_MINOR_VERSION)",
        "cache(QMAKE_CXX.QMAKE_GCC_PATCH_VERSION, set stash, QMAKE_GCC_PATCH_VERSION)",
        "COMPILER_MACROS = QMAKE_GCC_MAJOR_VERSION QMAKE_GCC_MINOR_VERSION QMAKE_GCC_PATCH_VERSION",
        "cache(QMAKE_CXX.COMPILER_MACROS, set stash, COMPILER_MACROS)",
        "",
    ]

    lines.append("QMAKE_CXX_INCDIRS = %s" % " ".join(incdirs))
    lines.append("cache(QMAKE_CXX.INCDIRS, set stash, QMAKE_CXX_INCDIRS)")
    lines.append("QMAKE_CXX_LIBDIRS = %s" % " ".join(libdirs))
    lines.append("cache(QMAKE_CXX.LIBDIRS, set stash, QMAKE_CXX_LIBDIRS)")
    lines.append("")
    lines.append("SPEC_VER = %s" % ver_line)
    lines.append("cache(QMAKE_CXX.SPEC_VER, set stash, SPEC_VER)")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print("wrote", OUT)
    print("incdirs =", len(incdirs), " libdirs =", len(libdirs))
    print("gcc =", major, minor, patch)


if __name__ == "__main__":
    main()
