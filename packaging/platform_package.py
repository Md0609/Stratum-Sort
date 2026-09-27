#!/usr/bin/env python3
"""Builds one per-platform package from the source package, on the machine
it runs on.

    python3 packaging/platform_package.py <source.zip> <cxx> <out-dir>

<source.zip> is what `make package` produced (stratumsort-vX.Y.Z.zip); <cxx>
is g++, clang++ or msvc. The script extracts the source package, builds and
tests it here with CMake and that compiler, writes BUILDINFO.txt saying what
was verified, and archives the result as

    stratumsort-vX.Y.Z-<os>-<arch>.tar.gz     Linux, macOS
    stratumsort-vX.Y.Z-<os>-<arch>.zip        Windows

with a .sha256 beside it. The library is header-only, so a platform package
holds no compiled code: it is the source package, unchanged, plus the record
that it built and passed its tests on that platform. The CI platform-package
job runs this on each native runner.

Python and not the shell tools because the archive has to be reproducible
on each of the three runners. `zip` is absent on Windows, and CMake's archiver
stores each file's ctime in zip entries, which nothing can pin. Here every
entry has a fixed time, owner and mode, no extra fields, and a sorted order.
"""

import gzip
import hashlib
import io
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

# Same instant as PKG_TIMESTAMP in the Makefile, so both packages agree.
EPOCH = (2026, 1, 1, 0, 0, 0)
EPOCH_SECONDS = 1767225600


def platform_name():
    system = {"linux": "linux", "darwin": "macos", "win32": "windows"}.get(sys.platform)
    machine = platform.machine().lower()
    arch = {"x86_64": "x86_64", "amd64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(machine)
    if system is None or arch is None:
        sys.exit(f"unsupported platform: {sys.platform} {machine}")
    return system, arch


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 16), b""):
            h.update(block)
    return h.hexdigest()


def read(path):
    with open(path, "rb") as f:
        return f.read()


def run(cmd, cwd):
    print("+", " ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, flush=True)
    if result.returncode != 0:
        sys.exit(f"failed ({result.returncode}): {' '.join(cmd)}")
    return result.stdout


def files_under(root):
    """Relative paths of every regular file under root, '/'-separated and sorted."""
    out = []
    for dirpath, _, names in os.walk(root):
        for name in names:
            full = os.path.join(dirpath, name)
            out.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return sorted(out)


def tar_gz(stage, top):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tf:
        for rel in files_under(os.path.join(stage, top)):
            data = read(os.path.join(stage, top, rel))
            info = tarfile.TarInfo(f"{top}/{rel}")
            info.size, info.mtime, info.mode = len(data), EPOCH_SECONDS, 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            tf.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(filename="", fileobj=out, mode="wb", compresslevel=9, mtime=0) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def zip_bytes(stage, top):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as zf:
        for rel in files_under(os.path.join(stage, top)):
            data = read(os.path.join(stage, top, rel))
            info = zipfile.ZipInfo(f"{top}/{rel}", date_time=EPOCH)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3            # Unix, so the mode bits below are honoured
            info.external_attr = 0o644 << 16
            zf.writestr(info, data, compresslevel=9)
    return out.getvalue()


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    source_zip, cxx, out_dir = os.path.abspath(sys.argv[1]), sys.argv[2], os.path.abspath(sys.argv[3])
    base = os.path.basename(source_zip)[: -len(".zip")]          # stratumsort-vX.Y.Z
    version = base[len("stratumsort-v"):]
    system, arch = platform_name()
    name = f"{base}-{system}-{arch}"
    ext = "zip" if system == "windows" else "tar.gz"

    source_hash = sha256(source_zip)
    recorded = source_zip + ".sha256"
    if os.path.exists(recorded) and read(recorded).decode().split()[0] != source_hash:
        sys.exit(f"{source_zip} does not match {recorded}")

    # A toolchain process can outlive the build and keep a file open (MSVC's
    # mspdbsrv on Windows); that must not fail the package after it is made.
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as work:
        with zipfile.ZipFile(source_zip) as zf:
            zf.extractall(os.path.join(work, "src"))
        pkg = os.path.join(work, "src", base)
        build = os.path.join(work, "build")

        configure = ["cmake", "-S", pkg, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                     "-DCMAKE_CXX_STANDARD=17", "-DCMAKE_CXX_STANDARD_REQUIRED=ON"]
        if cxx != "msvc":
            configure.append(f"-DCMAKE_CXX_COMPILER={cxx}")
        log = run(configure, work)
        run(["cmake", "--build", build, "--config", "Release", "--parallel"], work)
        tests = run(["ctest", "--test-dir", build, "--build-config", "Release", "-V"], work)

        compiler = re.search(r"The CXX compiler identification is (.+)", log).group(1).strip()
        cmake_version = run(["cmake", "--version"], work).splitlines()[0].split()[-1]
        passed = re.findall(r"^\s*\d+/\d+ Test +#\d+: (\S+) \.+ +Passed", tests, re.M)
        checks = re.findall(r"^\d+: (\d+/\d+ checks passed)$", tests, re.M)
        summary = re.search(r"^(\d+% tests passed.*)$", tests, re.M).group(1)

        stage = os.path.join(work, "stage")
        shutil.copytree(pkg, os.path.join(stage, name))
        lines = [
            f"StratumSort {version} - {system}-{arch} package",
            "",
            f"The source package {base}.zip, unchanged, plus this file. It was",
            "extracted, built and tested on the platform below before it was",
            "archived; the library is header-only, so nothing here is compiled.",
            "",
            f"source package   {base}.zip",
            f"source sha256    {source_hash}",
            f"source commit    {os.environ.get('GITHUB_SHA', 'not recorded (local build)')}",
            f"platform         {system}-{arch}",
            "runner image     " + (f"{os.environ['ImageOS']} {os.environ.get('ImageVersion', '')}".strip()
                                   if "ImageOS" in os.environ else "not recorded (local build)"),
            f"compiler         {compiler}",
            f"cmake            {cmake_version}",
            "configuration    Release, C++17",
            "tests            " + ("\n                 ".join(f"{t}: Passed" for t in passed)),
            "                 " + "\n                 ".join(checks + [summary]),
            "",
        ]
        with open(os.path.join(stage, name, "BUILDINFO.txt"), "w", newline="\n") as f:
            f.write("\n".join(lines))

        make = zip_bytes if ext == "zip" else tar_gz
        first, second = make(stage, name), make(stage, name)
        if first != second:
            sys.exit("archive is not reproducible")

    os.makedirs(out_dir, exist_ok=True)
    archive = os.path.join(out_dir, f"{name}.{ext}")
    with open(archive, "wb") as f:
        f.write(first)
    digest = sha256(archive)
    with open(archive + ".sha256", "w", newline="\n") as f:
        f.write(f"{digest}  {name}.{ext}\n")
    print(f"{archive}\n{digest}")


if __name__ == "__main__":
    main()
