#!/usr/bin/env python3
"""Recordlab 脚本执行 shim。

该文件由 recordlab_script_runner 子进程调用。它只负责把脚本执行过程
转换成 JSONL 事件；设备控制仍应由脚本通过 Master/Action/Topic API 完成。
"""

from __future__ import annotations

import json
import os
import runpy
import sys
import traceback
from pathlib import Path


PREFIX = "RECORDLAB_EVENT_JSON "


def emit(payload: dict) -> None:
    print(PREFIX + json.dumps(payload, ensure_ascii=False), flush=True)


def main() -> int:
    if len(sys.argv) < 2:
        emit({"type": "log", "stream": "stderr", "message": "缺少脚本路径"})
        return 2

    script_path = Path(sys.argv[1]).resolve()
    script_args = sys.argv[2:]
    if not script_path.exists():
        emit({"type": "log", "stream": "stderr", "message": f"脚本不存在: {script_path}"})
        return 2

    sys.path.insert(0, str(script_path.parent))
    legacy_root_env = os.environ.get("RECORDLAB_LEGACY_ROOT", "").strip()
    if legacy_root_env:
        legacy_root = Path(legacy_root_env)
        if legacy_root.exists():
            sys.path.insert(0, str(legacy_root))

    def trace(frame, event, arg):
        if event == "line" and Path(frame.f_code.co_filename).resolve() == script_path:
            emit({
                "type": "progress",
                "script_path": str(script_path),
                "line": frame.f_lineno,
            })
        return trace

    old_argv = sys.argv[:]
    sys.argv = [str(script_path)] + script_args
    sys.settrace(trace)
    try:
        runpy.run_path(str(script_path), run_name="__main__")
        emit({"type": "log", "stream": "stdout", "message": "脚本执行完成"})
        return 0
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 0
        emit({"type": "log", "stream": "stdout", "message": f"脚本退出: {code}"})
        return code
    except Exception:
        sys.settrace(None)
        emit({"type": "log", "stream": "stderr", "message": traceback.format_exc()})
        return 1
    finally:
        sys.settrace(None)
        sys.argv = old_argv


if __name__ == "__main__":
    raise SystemExit(main())
