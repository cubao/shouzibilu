#!/usr/bin/env python3
"""serve.py - demo 开发用本地静态服务器, 禁缓存(no-store)。

用法: python3 tools/serve.py [端口]   (默认 8000, 从仓库根目录服务)
"""

import http.server
import sys


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # 安静


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    with http.server.ThreadingHTTPServer(("", port), NoCacheHandler) as srv:
        print(f"http://localhost:{port}/demo/")
        srv.serve_forever()
