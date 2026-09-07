"""Synthetic packaging contract tests; no game data or external publication."""
import copy
import http.client
import hashlib
import io
import json
import socket
import subprocess
import sys
import tempfile
import tarfile
import time
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qualify_web_release import REQUIRED_JOBS, digest, files_in, qualify, verify
from package_web_sources import collect_runtime_sources, file_identity, validate_dependency_sources


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.site = self.root / "site"
        self.site.mkdir()
        (self.site / "index.html").write_text("Synthetic package test")
        (self.site / "licenses.txt").write_text("GPL-3.0 synthetic test")
        self.revision = "a" * 40
        self.dependency_sources = self.root / "dependency-sources"
        self.dependency_sources.mkdir()
        codec = json.loads((ROOT / "tools/cinematic_codec.json").read_text())
        toolchain = json.loads((ROOT / "tools/web_toolchain.json").read_text())
        dependencies = json.loads((ROOT / "tools/web_dependencies.json").read_text())
        openal = dependencies["artifacts"]["reverb_dsp.mjs"][0]
        self.tar_source("ffmpeg.tar.xz", f"ffmpeg-{codec['ffmpeg']['version']}",
                        ("configure", "COPYING.LGPLv2.1", "libavcodec/bink.c", "libavformat/bink.c", "libavutil/avutil.h"))
        self.tar_source("zlib.tar.gz", "zlib-1.3.2", ("zlib.h", "inflate.c", "deflate.c", "README", "CMakeLists.txt"))
        codec["ffmpeg"]["sha256"] = file_identity(self.dependency_sources / "ffmpeg.tar.xz")["sha256"]
        zlib_sha = hashlib.sha512((self.dependency_sources / "zlib.tar.gz").read_bytes()).hexdigest()
        with zipfile.ZipFile(self.dependency_sources / "openal.zip", "w") as archive:
            archive.comment = openal["commit"].encode()
            for name in ("CMakeLists.txt", "COPYING", "LICENSE-pffft", "alc/effects/reverb.cpp", "fmt-11.2.0/LICENSE", "gsl/LICENSE"):
                archive.writestr(name, "Synthetic public-source packaging fixture")
        with zipfile.ZipFile(self.dependency_sources / "emscripten-runtime.zip", "w") as archive:
            archive.comment = toolchain["emscriptenCommit"].encode()
            for name in ("LICENSE", "AUTHORS", "system/lib/libc/musl/COPYRIGHT", "system/lib/libcxx/LICENSE.TXT",
                         "system/lib/libcxxabi/LICENSE.TXT", "system/lib/compiler-rt/LICENSE.TXT", "tools/ports/zlib/zconf.h"):
                archive.writestr(name, "Synthetic public-source packaging fixture")
            archive.writestr("tools/ports/zlib.py", f"VERSION = '1.3.2'\nHASH = '{zlib_sha}'\n")
        self.source = self.root / "source.zip"
        with zipfile.ZipFile(self.source, "w") as archive:
            archive.comment = self.revision.encode()
            for name in ("LICENSE", "CMakeLists.txt", "scripts/web/CMakeLists.txt", "scripts/web/reverb/CMakeLists.txt", "package.json",
                         "package-lock.json", "tools/web_toolchain.json", "tools/web_dependencies.json",
                         "tools/serve_web.py", "tools/qualify_web_release.py", "tools/check_source_archive.py", "tools/package_web_sources.py"):
                archive.writestr(name, (ROOT / name).read_bytes())
            archive.writestr("tools/cinematic_codec.json", json.dumps(codec))
            archive.writestr("src/synthetic.cpp", "int synthetic = 1;\n")
        self.receipt = {"schemaVersion": 2, "revision": self.revision, "dirty": False,
                        "site": files_in(self.site), "source": file_identity(self.source),
                        "dependencySources": files_in(self.dependency_sources),
                        "toolchain": {**toolchain, **json.loads((ROOT / "package.json").read_text())["engines"]},
                        "dependencies": dependencies,
                        "lockfileSha256": digest((ROOT / "package-lock.json").read_bytes())}
        self.results = {job: {"result": "success"} for job in REQUIRED_JOBS}
        self.output = self.root / "qualified"

    def tar_source(self, filename, prefix, names):
        with tarfile.open(self.dependency_sources / filename, "w:xz" if filename.endswith(".xz") else "w:gz") as archive:
            for name in names:
                data = b"Synthetic public-source packaging fixture"
                entry = tarfile.TarInfo(prefix + "/" + name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))

    def rewrite_source(self, name, content):
        with zipfile.ZipFile(self.source) as archive:
            entries = {entry.filename: archive.read(entry) for entry in archive.infolist()}
        if content is None:
            del entries[name]
        else:
            entries[name] = content
        with zipfile.ZipFile(self.source, "w") as archive:
            archive.comment = self.revision.encode()
            for entry, data in entries.items():
                archive.writestr(entry, data)

    def make(self, receipt=None, results=None):
        qualify(receipt or self.receipt, self.results if results is None else results,
                self.site, self.source, self.revision, self.output, self.dependency_sources)

    def test_exact_pair_and_standalone_verification(self):
        self.make()
        subprocess.run([sys.executable, str(self.output / "qualify_web_release.py"), "verify", str(self.output)], check=True)

        self.assertEqual(files_in(self.output / "site"), files_in(self.site))
        self.assertEqual(files_in(self.output / "dependency-sources"), files_in(self.dependency_sources))

    def test_source_body_changes_and_omissions_cannot_reuse_receipt(self):
        original = self.source.read_bytes()
        for content in (b"int synthetic = 2;\n", None):
            with self.subTest(content=content):
                self.source.write_bytes(original)
                self.rewrite_source("src/synthetic.cpp", content)
                with self.assertRaisesRegex(ValueError, "Complete source archive"): self.make()
                self.assertFalse(self.output.exists())

    def test_missing_and_corrupted_dependency_sources_fail_closed(self):
        for name in self.receipt["dependencySources"]:
            path = self.dependency_sources / name
            original = path.read_bytes()
            for content in (None, b"corrupted source"):
                with self.subTest(name=name, content=content):
                    if content is None: path.unlink()
                    else: path.write_bytes(content)
                    with self.assertRaisesRegex(ValueError, "Dependency source hashes"): self.make()
                    self.assertFalse(self.output.exists())
                    path.write_bytes(original)

    def test_dependency_pin_cannot_be_replaced_by_receipt_hash(self):
        for name, message in (("ffmpeg.tar.xz", "FFmpeg source archive"), ("zlib.tar.gz", "zlib source archive")):
            path = self.dependency_sources / name
            original = path.read_bytes()
            path.write_bytes(original + b"changed source archive")
            self.receipt["dependencySources"] = files_in(self.dependency_sources)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                self.make()
            path.write_bytes(original)
        self.assertFalse(self.output.exists())

    def test_updating_outer_manifest_hash_cannot_replace_producer_source(self):
        self.make()
        for relative in ("source.zip", "dependency-sources/openal.zip"):
            path = self.output / relative
            original = path.read_bytes()
            with zipfile.ZipFile(path, "a") as archive:
                archive.writestr("extra-source.cpp", "changed source")
            manifest_path = self.output / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["files"][relative] = file_identity(path)
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "source archive|Dependency source hashes"): verify(self.output)
            path.write_bytes(original)
            manifest["files"][relative] = file_identity(path)
            manifest_path.write_text(json.dumps(manifest))

    def test_license_and_version_only_is_not_corresponding_source(self):
        self.tar_source("ffmpeg.tar.xz", "ffmpeg-8.0.3", ("COPYING.LGPLv2.1", "configure"))
        with zipfile.ZipFile(self.source) as archive:
            codec = json.loads(archive.read("tools/cinematic_codec.json"))
        codec["ffmpeg"]["sha256"] = file_identity(self.dependency_sources / "ffmpeg.tar.xz")["sha256"]
        self.rewrite_source("tools/cinematic_codec.json", json.dumps(codec))
        with self.assertRaisesRegex(ValueError, "Incomplete FFmpeg"):
            validate_dependency_sources(self.source, self.dependency_sources)

    def test_modified_runtime_source_cannot_hide_behind_revision_marker(self):
        repo = self.root / "runtime-repo"
        reference = repo / ".tools/emscripten-source.git"
        reference.mkdir(parents=True)
        subprocess.run(["git", "init", str(reference)], check=True, stdout=subprocess.DEVNULL)
        for name in ("LICENSE", "AUTHORS", "system/lib/input.c"):
            path = reference / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("synthetic runtime source")
        runtime = self.root / "runtime"
        for name in ("LICENSE", "AUTHORS", "system/lib/input.c"):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((reference / name).read_bytes())
        # Git archive requires each requested root to exist.
        for name in ("src/input.js", "tools/input.py", "cmake/input.cmake", "third_party/NOTICE"):
            path = reference / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("synthetic")
            target = runtime / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
        subprocess.run(["git", "-C", str(reference), "add", "."], check=True)
        subprocess.run(["git", "-C", str(reference), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                        "commit", "-m", "runtime roots"], check=True, stdout=subprocess.DEVNULL)
        commit = subprocess.check_output(["git", "-C", str(reference), "rev-parse", "HEAD"], text=True).strip()
        (runtime / "emscripten-revision.txt").write_text(commit)
        for name in ("tools/pylauncher/environment.x64", "tools/pylauncher/pylauncher.obj"):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"Synthetic Windows SDK build artifact")
        output = self.root / "runtime.zip"
        collect_runtime_sources(repo, runtime, commit, output)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.read("system/lib/input.c"), b"synthetic runtime source")
            self.assertFalse(any(name.startswith("tools/pylauncher/") for name in archive.namelist()))
        unexpected = runtime / "system/lib/extra.c"
        unexpected.write_text("untracked runtime input")
        with self.assertRaisesRegex(ValueError, "inventory differs from pinned Git tree"):
            collect_runtime_sources(repo, runtime, commit, output)
        unexpected.unlink()
        (runtime / "system/lib/input.c").write_text("modified after installation")
        with self.assertRaisesRegex(ValueError, "differs from pinned Git tree"):
            collect_runtime_sources(repo, runtime, commit, output)

    def test_failed_cancelled_skipped_and_missing_jobs_cannot_qualify(self):
        for job in REQUIRED_JOBS:
            for state in ("failure", "cancelled", "skipped", None):
                results = copy.deepcopy(self.results)
                if state is None: del results[job]
                else: results[job]["result"] = state
                with self.subTest(job=job, state=state), self.assertRaisesRegex(ValueError, "Required tier"):
                    self.make(results=results)
                self.assertFalse(self.output.exists())

    def test_dirty_or_mismatched_build_cannot_qualify(self):
        for key, value in (("dirty", True), ("revision", "b" * 40), ("lockfileSha256", "0" * 64), ("dependencies", {}), ("toolchain", {})):
            receipt = {**self.receipt, key: value}
            with self.subTest(key=key), self.assertRaises(ValueError): self.make(receipt=receipt)
            self.assertFalse(self.output.exists())

    def test_source_revision_mismatch(self):
        with zipfile.ZipFile(self.source, "a") as archive: archive.comment = b"b" * 40
        with self.assertRaisesRegex(ValueError, "revision"): self.make()

    def test_home_backup_cannot_enter_source_package(self):
        with zipfile.ZipFile(self.source, "a") as archive:
            archive.writestr("accidental.KISAK-HOME", b"KISAKHOME1\nsynthetic private backup")
        with self.assertRaisesRegex(ValueError, "Non-distributable source entry"):
            self.make()
        self.assertFalse(self.output.exists())

    def test_local_package_update_failure_and_rollback_keep_origin(self):
        self.make()
        previous = self.output
        original = files_in(previous)
        # An interrupted extraction never replaces the running package.
        partial = self.root / "partial-update"
        partial.mkdir()
        (partial / "index.html").write_text("incomplete synthetic update")
        with self.assertRaises(FileNotFoundError): verify(partial)
        self.assertEqual(files_in(previous), original)

        self.output = self.root / "replacement"
        self.revision = "b" * 40
        with zipfile.ZipFile(self.source, "a") as archive: archive.comment = self.revision.encode()
        (self.site / "index.html").write_text("Synthetic replacement package")
        self.receipt = {**self.receipt, "revision": self.revision, "site": files_in(self.site),
                        "source": file_identity(self.source)}
        self.make()
        manifest_path = self.output / "manifest.json"
        manifest_text = manifest_path.read_text()
        manifest = json.loads(manifest_text)
        manifest["build"]["revision"] = "c" * 40
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "revision"): verify(self.output)
        self.assertEqual(files_in(previous), original)
        manifest_path.write_text(manifest_text)

        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        # Execute the actual shipped server, replacing directories at one origin.
        # All requests use loopback; no downloader, proxy or remote service runs.
        for package, expected in ((previous, "Synthetic package test"),
                                  (self.output, "Synthetic replacement package"),
                                  (previous, "Synthetic package test")):
            verify(package)
            server = subprocess.Popen(
                [sys.executable, str(package / "serve_web.py"), "--directory", str(package / "site"), "--port", str(port)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                deadline = time.monotonic() + 10
                while True:
                    self.assertIsNone(server.poll(), "Packaged server exited before serving")
                    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
                    try:
                        connection.request("GET", "/index.html")
                        response = connection.getresponse()
                        self.assertEqual(response.status, 200)
                        self.assertEqual(response.read().decode(), expected)
                        self.assertEqual(response.getheader("Cache-Control"), "no-store")
                        self.assertEqual(response.getheader("Cross-Origin-Opener-Policy"), "same-origin")
                        self.assertEqual(response.getheader("Cross-Origin-Embedder-Policy"), "require-corp")
                        break
                    except OSError:
                        if time.monotonic() >= deadline: raise
                        time.sleep(0.05)
                    finally:
                        connection.close()
            finally:
                server.terminate()
                try: server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
        self.assertEqual(files_in(previous), original)

    def test_modified_site_and_missing_files_are_rejected(self):
        (self.site / "index.html").write_text("modified")
        with self.assertRaisesRegex(ValueError, "Site contents"): self.make()
        (self.site / "index.html").unlink()
        with self.assertRaisesRegex(ValueError, "Site contents"): self.make()

    def test_verifier_rejects_modified_artifact_and_unrun_tier(self):
        self.make()
        manifest_path = self.output / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["tiers"][REQUIRED_JOBS[0]]["status"] = "omitted"
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "Required tier"): verify(self.output)
        manifest["tiers"][REQUIRED_JOBS[0]]["status"] = "passed"
        manifest_path.write_text(json.dumps(manifest))
        (self.output / "source.zip").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "hashes"): verify(self.output)


if __name__ == "__main__": unittest.main()
