"""Run a bounded synthetic fuzz corpus copy and require real parser paths."""
import argparse
import re
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be in [1, 3600]")
    repo = Path(__file__).resolve().parents[1]
    # A new run directory prevents generated corpus from contaminating baseline.
    args.output.mkdir(parents=True, exist_ok=False)
    corpus = args.output / "corpus"
    shutil.copytree(repo / "tests/fuzz/corpus", corpus)
    artifacts = args.output / "failures"
    artifacts.mkdir()
    command = [str(args.executable.resolve()), str(corpus.resolve()),
               f"-dict={repo / 'tests/fuzz/asset_parsers.dict'}", "-seed=20260906",
               f"-max_total_time={args.seconds}", "-max_len=262144", "-timeout=10",
               "-rss_limit_mb=1024", "-malloc_limit_mb=512", "-print_final_stats=1",
               f"-artifact_prefix={artifacts.resolve()}/"]
    print("Running:", command, flush=True)
    log = args.output / "fuzz.log"
    with log.open("w", encoding="utf8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=args.seconds + 60)
    output = log.read_text(encoding="utf8", errors="replace")
    print("\n".join(output.splitlines()[-20:]))
    result.check_returncode()
    paths = re.search(r"KISAK_FUZZ paths=(\d+),(\d+),(\d+),(\d+),(\d+)", output)
    if not paths or not all(int(value) > 0 for value in paths.groups()):
        raise SystemExit("Fuzz run did not reach every seeded success/rejection family")


if __name__ == "__main__":
    main()
