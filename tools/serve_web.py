#!/usr/bin/env python3
"""Serve a generated KisakCOD browser build from a narrow local root."""

from __future__ import annotations

import argparse
import functools
import http.server
import mimetypes
import sys
from pathlib import Path


class WebBuildRequestHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".mjs": "text/javascript; charset=utf-8",
        ".wasm": "application/wasm",
    }

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def log_message(self, format: str, *args: object) -> None:
        # pythonw.exe has no stderr. Keeping logging optional lets the server
        # run in a hidden window without aborting every response.
        if sys.stderr is not None:
            super().log_message(format, *args)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--directory",
        type=Path,
        default=Path("build/web/site"),
        help="generated site directory (default: build/web/site)",
    )
    parser.add_argument("--port", type=int, default=8000)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    directory = args.directory.resolve(strict=False)
    if not directory.exists():
        raise SystemExit(
            f"Web build not found: {directory}\n"
            "Build it first with tools/build_web.ps1."
        )
    if not directory.is_dir():
        raise SystemExit(f"Not a directory: {directory}")
    if not 1 <= args.port <= 65535:
        raise SystemExit("Port must be between 1 and 65535")

    mimetypes.add_type("application/wasm", ".wasm")
    handler = functools.partial(WebBuildRequestHandler, directory=str(directory))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    if sys.stdout is not None:
        print(f"Serving {directory} at http://127.0.0.1:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
