"""Pair a tested flat site with its exact source; never publish externally."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

# Verification must be read-only, including imports beside a shipped manifest.
sys.dont_write_bytecode = True
from check_source_archive import check_archive
from package_web_sources import collect_dependency_sources, file_identity, validate_dependency_sources

REQUIRED_JOBS = ("native-portable", "parser-fuzz", "windows-portable", "wasm-browser-production")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def files_in(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("Package inputs cannot contain symlinks")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = file_identity(path)
    return result


def record(repo, site, output, source, dependency_sources):
    def git(*args):
        return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()
    actual = subprocess.check_output(["node", str(repo / "tools/check_toolchain.mjs"), "--web"], text=True)
    actual = json.loads(actual.split("Toolchain verified: ", 1)[1])
    revision = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    source.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "-C", str(repo), "archive", "--format=zip",
                    f"--output={source.resolve()}", revision], check=True)
    check_archive(source)
    # Public dependency trees are verified independently even for dirty local
    # builds. Their live pins can differ from HEAD, but dirty receipts never qualify.
    dependencies = collect_dependency_sources(repo, repo if dirty else source, dependency_sources)
    receipt = {"schemaVersion": 2, "revision": revision,
               "dirty": dirty or bool(git("status", "--porcelain", "--untracked-files=all")),
               "toolchain": actual, "site": files_in(site), "source": file_identity(source),
               "dependencySources": dependencies,
               "lockfileSha256": digest((repo / "package-lock.json").read_bytes()),
               "dependencies": json.loads((repo / "tools/web_dependencies.json").read_text())}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf8")
    print(f"Build receipt recorded (dirty={receipt['dirty']}): {output}")


def validate_tiers(results):
    for job in REQUIRED_JOBS:
        if results.get(job, {}).get("result") != "success":
            raise ValueError(f"Required tier did not pass: {job}")


def validate_inputs(receipt, site, source, revision, dependency_sources):
    if receipt.get("schemaVersion") != 2 or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Unsupported receipt or revision")
    if receipt.get("revision") != revision or receipt.get("dirty") is not False:
        raise ValueError("Site receipt must describe the exact clean source revision")
    if not receipt.get("site") or files_in(site) != receipt["site"]:
        raise ValueError("Site contents differ from the tested build receipt")
    check_archive(source)
    with zipfile.ZipFile(source) as archive:
        if archive.comment.decode("ascii") != revision:
            raise ValueError("Source git-archive revision does not match the site")
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError("Duplicate source entries")
        if file_identity(source) != receipt.get("source"):
            raise ValueError("Complete source archive differs from the build receipt")
        if digest(archive.read("package-lock.json")) != receipt["lockfileSha256"]:
            raise ValueError("Source lockfile differs from the build")
        toolchain = {**json.loads(archive.read("tools/web_toolchain.json")),
                     **json.loads(archive.read("package.json"))["engines"]}
        if receipt["toolchain"] != toolchain:
            raise ValueError("Source toolchain differs from the build")
        if json.loads(archive.read("tools/web_dependencies.json")) != receipt["dependencies"]:
            raise ValueError("Source dependency inventory differs from the build")
    if not receipt.get("dependencySources"):
        raise ValueError("Missing corresponding dependency sources in build receipt")
    validate_dependency_sources(source, dependency_sources, receipt["dependencySources"])


def qualify(receipt, results, site, source, revision, output, dependency_sources):
    validate_tiers(results)
    validate_inputs(receipt, site, source, revision, dependency_sources)
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(site, output / "site")
    shutil.copyfile(source, output / "source.zip")
    shutil.copytree(dependency_sources, output / "dependency-sources")
    with zipfile.ZipFile(source) as archive:
        for name in ("serve_web.py", "qualify_web_release.py", "check_source_archive.py", "package_web_sources.py"):
            (output / name).write_bytes(archive.read("tools/" + name))
    (output / "start-local.cmd").write_text(
        '@echo off\ncd /d "%~dp0"\npython serve_web.py --directory site --port 8000\n', encoding="utf8")
    (output / "README.txt").write_text(
        "Synthetic-platform-qualified KisakCOD package; not a qualified single-player alpha.\n"
        "Requires Python 3.11+ and a supported browser. Verify: python qualify_web_release.py verify .\n"
        "Start: start-local.cmd (Windows), or python serve_web.py --directory site --port 8000\n"
        "Open http://127.0.0.1:8000 every time. Keep that host, port and browser profile on updates.\n"
        "No network is needed to serve this package. Game files must be locally supplied by their owner.\n"
        "Corresponding project source and build instructions: source.zip. Public dependency source and\n"
        "embedded upstream notices: dependency-sources/ (FFmpeg, OpenAL including fmt/GSL, Emscripten\n"
        "runtime including libc/libc++/compiler-rt and zlib). Tool/build identities are bound in manifest.json.\n"
        "Compiler executables and game/native SDK assets are not included. See site/licenses.txt too.\n"
        "Verification checks consistency with the producer receipt, not publisher authenticity: obtain the\n"
        "package/receipt through a trusted channel. An editable JSON manifest is not a signature.\n"
        "Keep the previous package for rollback. Hosted cold-offline startup is not supported.\n", encoding="utf8")
    tiers = {job: {"status": "passed", "reason": "Required job succeeded in this workflow run"} for job in REQUIRED_JOBS}
    tiers["owned-campaign-acceptance"] = {"status": "omitted", "reason": "Manual owned-data acceptance is outside synthetic CI; this package is not alpha qualification"}
    manifest = {"schemaVersion": 2, "scope": "synthetic-platform", "alphaQualified": False,
                "build": receipt, "tiers": tiers, "files": files_in(output)}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    verify(output)


def verify(package):
    manifest = json.loads((package / "manifest.json").read_text(encoding="utf8"))
    if manifest.get("schemaVersion") != 2 or manifest.get("scope") != "synthetic-platform" or manifest.get("alphaQualified") is not False:
        raise ValueError("Unsupported qualification scope")
    actual = files_in(package)
    actual.pop("manifest.json")
    if actual != manifest["files"]:
        raise ValueError("Package file hashes or inventory do not match")
    for job in REQUIRED_JOBS:
        if manifest["tiers"].get(job, {}).get("status") != "passed":
            raise ValueError(f"Required tier missing or unrun: {job}")
    validate_inputs(manifest["build"], package / "site", package / "source.zip",
                    manifest["build"]["revision"], package / "dependency-sources")
    print(f"Verified package consistency at revision {manifest['build']['revision']}; not publisher authentication; synthetic platform scope only")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    record_parser = commands.add_parser("record")
    record_parser.add_argument("site", type=Path)
    record_parser.add_argument("output", type=Path)
    record_parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    record_parser.add_argument("--source", required=True, type=Path)
    record_parser.add_argument("--dependency-sources", required=True, type=Path)
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("package", type=Path)
    qualify_parser = commands.add_parser("qualify")
    for name in ("site", "source", "receipt", "results", "output"):
        qualify_parser.add_argument(name, type=Path)
    qualify_parser.add_argument("--revision", required=True)
    qualify_parser.add_argument("--dependency-sources", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "record": record(args.repo, args.site, args.output, args.source, args.dependency_sources)
    elif args.command == "verify": verify(args.package)
    else:
        qualify(json.loads(args.receipt.read_text()), json.loads(args.results.read_text()),
                args.site, args.source, args.revision, args.output, args.dependency_sources)


if __name__ == "__main__":
    main()
