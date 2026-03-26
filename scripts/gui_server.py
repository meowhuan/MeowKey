from __future__ import annotations

import argparse
import http.server
import socketserver
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
GUI_ROOT = PROJECT_ROOT / "gui"


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(GUI_ROOT), **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the MeowKey WebHID GUI.")
    parser.add_argument("--host", default="127.0.0.1", help="Host interface to bind.")
    parser.add_argument("--port", type=int, default=8765, help="TCP port to listen on.")
    args = parser.parse_args()

    with socketserver.TCPServer((args.host, args.port), NoCacheHandler) as server:
        print(f"MeowKey GUI available at http://{args.host}:{args.port}")
        print("Use a Chromium-based browser for WebHID access.")
        server.serve_forever()


if __name__ == "__main__":
    main()

