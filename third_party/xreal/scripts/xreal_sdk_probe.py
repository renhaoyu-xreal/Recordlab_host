#!/usr/bin/env python3
"""通过 XREAL SDK 探测眼镜设备。

脚本只输出一段 JSON，供 BspDeviceAdapter 的 check 阶段调用。
默认只枚举设备，不打开眼镜，避免 check 阶段触发 SDK 服务部署或重启。
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def configure_runtime() -> None:
    root = Path(os.environ.get("RECORDLAB_NODES_ROOT", Path(__file__).resolve().parents[3]))
    runtime = Path(os.environ.get("RECORDLAB_XREAL_RUNTIME_ROOT", root / "third_party" / "xreal" / "runtime"))
    site_packages = Path(os.environ.get("RECORDLAB_XREAL_SITE_PACKAGES", runtime / "site-packages"))
    if site_packages.exists():
        sys.path.insert(0, str(site_packages))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--with-fsn",
        action="store_true",
        help="显式打开眼镜读取 FSN；这可能触发 SDK 服务启动，只应在 connect/init 阶段使用",
    )
    args = parser.parse_args()

    configure_runtime()
    try:
        from xrglasses import XrGlasses as Xr  # type: ignore

        factory = Xr.GlassesFactory.instance()
        product_ids = [int(pid) for pid in factory.enumerateDevices()]
        fsn = ""
        fsn_status = "not_requested"
        if args.with_fsn and product_ids:
            fsn_status = "requested"
            try:
                glasses = factory.createGlasses(int(product_ids[0]))
                try:
                    try:
                        glasses.open()
                    except Exception:
                        pass
                    fsn = str(glasses.fsn())
                finally:
                    try:
                        glasses.close()
                    except Exception:
                        pass
                fsn_status = "ok" if fsn and fsn != "UnknownFSN" else "unknown"
            except Exception as exc:
                fsn = ""
                fsn_status = "failed: " + str(exc)
        print(json.dumps({
            "success": bool(product_ids),
            "message": "" if product_ids else "No glasses found by SDK",
            "product_ids": product_ids,
            "product_id": int(product_ids[0]) if product_ids else -1,
            "device_count": len(product_ids),
            "fsn": fsn,
            "fsn_status": fsn_status,
        }, ensure_ascii=False))
        return 0
    except Exception as exc:
        print(json.dumps({
            "success": False,
            "message": str(exc),
            "product_ids": [],
            "product_id": -1,
            "device_count": 0,
            "fsn": "",
            "fsn_status": "failed",
        }, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
