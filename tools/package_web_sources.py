"""Collect public corresponding source from the exact build caches, never the native SDKs."""
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

SOURCE_FILES = ("ffmpeg.tar.xz", "openal.zip", "emscripten-runtime.zip", "zlib.tar.gz")
RUNTIME_ROOTS = ("system", "src", "tools", "cmake", "third_party")
RUNTIME_NOTICES = ("LICENSE", "AUTHORS")


def file_identity(path):
    with path.open("rb") as stream:
        sha = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"sha256": sha, "bytes": path.stat().st_size}


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def pins(source):
    names = ("tools/cinematic_codec.json", "tools/web_toolchain.json",
             "tools/web_dependencies.json", "scripts/web/reverb/CMakeLists.txt")
    if source.is_dir():
        data = {name: (source / name).read_bytes() for name in names}
    else:
        with zipfile.ZipFile(source) as archive:
            data = {name: archive.read(name) for name in names}
    codec = json.loads(data[names[0]])["ffmpeg"]
    toolchain = json.loads(data[names[1]])
    inventory = json.loads(data[names[2]])
    reverb = data[names[3]].decode()
    openal = next(item for item in inventory["artifacts"]["reverb_dsp.mjs"] if item["name"] == "OpenAL Soft")
    if not re.search(r"GIT_TAG\s+" + re.escape(openal["commit"]) + r"\b", reverb):
        raise ValueError("OpenAL source pin differs from the build recipe")
    if not re.fullmatch(r"[0-9a-f]{40}", toolchain.get("emscriptenCommit", "")):
        raise ValueError("Missing pinned Emscripten source commit")
    return codec, toolchain, openal


def zlib_pin(port):
    version = re.search(r"^VERSION = ['\"]([^'\"]+)['\"]", port, re.M)
    sha = re.search(r"^HASH = ['\"]([0-9a-f]{128})['\"]", port, re.M)
    if not version or not sha or not re.fullmatch(r"[0-9.]+", version[1]):
        raise ValueError("Unsupported pinned Emscripten zlib port")
    return version[1], sha[1]


def tar_members(path):
    with tarfile.open(path) as archive:
        names = set()
        for member in archive:
            parts = PurePosixPath(member.name).parts
            if not parts or member.name.startswith("/") or ".." in parts or "\\" in member.name or ":" in member.name:
                raise ValueError("Unsafe dependency source archive path")
            if not (member.isfile() or member.isdir()):
                raise ValueError("Dependency source archive contains a link or special file")
            if member.name in names:
                raise ValueError("Duplicate dependency source archive entry")
            names.add(member.name)
        return names


def require_members(names, required, component):
    if not set(required) <= set(names):
        raise ValueError(f"Incomplete {component} corresponding source")


def validate_dependency_sources(source, directory, expected=None):
    actual = {path.name: file_identity(path) for path in sorted(directory.iterdir()) if path.is_file()}
    if any(path.is_symlink() or not path.is_file() for path in directory.iterdir()):
        raise ValueError("Dependency sources must be regular archive files")
    if set(actual) != set(SOURCE_FILES) or (expected is not None and actual != expected):
        raise ValueError("Dependency source hashes or inventory differ from the build receipt")
    codec, toolchain, openal = pins(source)
    if actual["ffmpeg.tar.xz"]["sha256"] != codec["sha256"]:
        raise ValueError("FFmpeg source archive differs from its pinned digest")
    require_members(tar_members(directory / "ffmpeg.tar.xz"),
                    [f"ffmpeg-{codec['version']}/{name}" for name in
                     ("configure", "COPYING.LGPLv2.1", "libavcodec/bink.c", "libavformat/bink.c", "libavutil/avutil.h")], "FFmpeg")
    with zipfile.ZipFile(directory / "openal.zip") as archive:
        if archive.comment.decode("ascii") != openal["commit"]:
            raise ValueError("OpenAL source revision mismatch")
        require_members(archive.namelist(), ("CMakeLists.txt", "COPYING", "LICENSE-pffft", "alc/effects/reverb.cpp", "fmt-11.2.0/LICENSE", "gsl/LICENSE"), "OpenAL")
    with zipfile.ZipFile(directory / "emscripten-runtime.zip") as archive:
        if archive.comment.decode("ascii") != toolchain["emscriptenCommit"]:
            raise ValueError("Emscripten source revision mismatch")
        require_members(archive.namelist(), (*RUNTIME_NOTICES, "system/lib/libc/musl/COPYRIGHT", "system/lib/libcxx/LICENSE.TXT",
                        "system/lib/libcxxabi/LICENSE.TXT", "system/lib/compiler-rt/LICENSE.TXT", "tools/ports/zlib.py", "tools/ports/zlib/zconf.h"), "Emscripten runtime")
        version, sha = zlib_pin(archive.read("tools/ports/zlib.py").decode())
    with (directory / "zlib.tar.gz").open("rb") as stream:
        if hashlib.file_digest(stream, "sha512").hexdigest() != sha:
            raise ValueError("zlib source archive differs from the Emscripten port pin")
    require_members(tar_members(directory / "zlib.tar.gz"),
                    [f"zlib-{version}/{name}" for name in ("zlib.h", "inflate.c", "deflate.c", "README", "CMakeLists.txt")], "zlib")
    return actual


def runtime_source(name):
    path = PurePosixPath(name)
    # Match the pinned SDK's tools/install.py distribution exclusions within
    # RUNTIME_ROOTS; maintenance scripts and dotfiles are not build inputs.
    return (not path.name.startswith(".") and name != "tools/install.py"
            and path.parts[:2] != ("tools", "maint")
            # The Windows SDK includes these pylauncher build leftovers.
            and name not in {"tools/pylauncher/environment.x64", "tools/pylauncher/pylauncher.obj"}
            and "__pycache__" not in path.parts and "prebuilt" not in path.parts
            and path.suffix.lower() not in {".pyc", ".a", ".o", ".bc", ".wasm", ".exe", ".dll"})


def collect_runtime_sources(repo, runtime, commit, output):
    # The version marker alone says nothing about modified installed source.
    # Compare every shipped runtime input against the pinned public Git tree.
    reference = repo / ".tools/emscripten-source.git"
    if not reference.exists():
        subprocess.run(["git", "init", "--bare", str(reference)], check=True)
    present = subprocess.run(["git", "-C", str(reference), "cat-file", "-e", commit + "^{commit}"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if present.returncode:
        subprocess.run(["git", "-C", str(reference), "fetch", "--depth=1",
                        "https://github.com/emscripten-core/emscripten.git", commit], check=True)
    # git archive otherwise applies the host's core.autocrlf setting; SDK
    # release sources use the committed bytes, including on Windows.
    data = subprocess.check_output(["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf",
                                    "-C", str(reference), "archive", "--format=zip",
                                    commit, *RUNTIME_ROOTS, *RUNTIME_NOTICES])
    with zipfile.ZipFile(io.BytesIO(data)) as upstream, zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.comment = commit.encode("ascii")
        expected = set()
        for entry in upstream.infolist():
            if entry.is_dir() or not runtime_source(entry.filename):
                continue
            expected.add(entry.filename)
            path = runtime / entry.filename
            content = upstream.read(entry)
            if path.is_symlink() or path.read_bytes() != content:
                raise ValueError(f"Installed runtime source differs from pinned Git tree: {entry.filename}")
            archive.writestr(entry.filename, content)
        actual = {path.relative_to(runtime).as_posix()
                  for root in RUNTIME_ROOTS for path in (runtime / root).rglob("*")
                  if path.is_file() and runtime_source(path.relative_to(runtime).as_posix())}
        if actual != expected - set(RUNTIME_NOTICES):
            missing = sorted(expected - set(RUNTIME_NOTICES) - actual)
            extra = sorted(actual - expected)
            raise ValueError(f"Installed runtime source inventory differs from pinned Git tree: missing={missing}, extra={extra}")


def collect_dependency_sources(repo, source, output):
    codec, toolchain, openal = pins(source)
    sdk = repo / ".tools/emsdk"
    runtime = sdk / "upstream/emscripten"
    if git(sdk, "rev-parse", "HEAD") != toolchain["emsdkCommit"]:
        raise ValueError("Emsdk checkout differs from its pinned commit")
    if (runtime / "emscripten-revision.txt").read_text().strip() != toolchain["emscriptenCommit"]:
        raise ValueError("Installed Emscripten source revision differs from pin")
    if (runtime / "emscripten-version.txt").read_text().strip().strip('"') != toolchain["emscripten"]:
        raise ValueError("Installed Emscripten version differs from pin")
    cache = (repo / "build/reverb-wasm/CMakeCache.txt").read_text()
    match = re.search(r"^OpenAL_SOURCE_DIR:STATIC=(.+)$", cache, re.M)
    if not match:
        raise ValueError("Missing actual OpenAL build source directory")
    openal_root = Path(match[1].strip())
    if git(openal_root, "rev-parse", "HEAD") != openal["commit"] or git(openal_root, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("OpenAL build source must be the clean pinned checkout")
    ffmpeg = repo / ".tools" / Path(codec["url"]).name
    if file_identity(ffmpeg)["sha256"] != codec["sha256"]:
        raise ValueError("FFmpeg source cache differs from pin")
    # FFmpeg is built out of tree: every distributed input must match the extracted build source.
    with tarfile.open(ffmpeg) as archive:
        tar_members(ffmpeg)
        for member in archive:
            if member.isfile():
                path = repo / ".tools" / member.name
                if path.is_symlink() or path.read_bytes() != archive.extractfile(member).read():
                    raise ValueError(f"Modified FFmpeg build source: {member.name}")
    output.mkdir(parents=True, exist_ok=True)
    if any(path.name not in SOURCE_FILES or path.is_symlink() or not path.is_file() for path in output.iterdir()):
        raise ValueError("Dependency source output contains unrelated inputs")
    shutil.copyfile(ffmpeg, output / "ffmpeg.tar.xz")
    subprocess.run(["git", "-C", str(openal_root), "archive", "--format=zip",
                    f"--output={(output / 'openal.zip').resolve()}", openal["commit"]], check=True)
    collect_runtime_sources(repo, runtime, toolchain["emscriptenCommit"], output / "emscripten-runtime.zip")
    version, sha = zlib_pin((runtime / "tools/ports/zlib.py").read_text())
    if os.environ.get("EMCC_LOCAL_PORTS"):
        raise ValueError("Local port overrides cannot qualify")
    ports = Path(os.environ.get("EM_PORTS", str(Path(os.environ.get("EM_CACHE", str(runtime / "cache"))) / "ports")))
    url = f"https://github.com/madler/zlib/archive/refs/tags/v{version}.tar.gz"
    cached = ports / ("zlib." + url.rsplit("/", 1)[1].split(".", 1)[1])
    if cached.is_file():
        data = cached.read_bytes()
    else:
        # SDKs can ship a prebuilt port without its download cache. Fetch only the
        # public source, requiring the installed port's SHA-512 before retaining it.
        with urllib.request.urlopen(url, timeout=60) as response:
            if not response.url.startswith("https://"):
                raise ValueError("Dependency source redirect must retain HTTPS")
            data = response.read()
    if hashlib.sha512(data).hexdigest() != sha:
        raise ValueError("zlib source cache/download differs from pin")
    (output / "zlib.tar.gz").write_bytes(data)
    extracted = ports / "zlib"
    if extracted.exists():
        with tarfile.open(output / "zlib.tar.gz") as archive:
            for member in archive:
                if member.isfile():
                    path = extracted / member.name
                    expected = ((runtime / "tools/ports/zlib/zconf.h").read_bytes()
                                if PurePosixPath(member.name).name == "zconf.h" else archive.extractfile(member).read())
                    if path.is_symlink() or path.read_bytes() != expected:
                        raise ValueError(f"Modified zlib build source: {member.name}")
    return validate_dependency_sources(source, output)
