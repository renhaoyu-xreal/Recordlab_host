#include "recordlab_echo/stdio_channel.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>

int main() {
  const std::string script = "/tmp/recordlab_fake_stdio_worker.py";
  {
    std::ofstream out(script);
    out << R"PY(
import json
import struct
import sys

MAGIC = b"RLCB"

def read_frame():
    prefix = sys.stdin.buffer.read(12)
    if len(prefix) != 12:
        return None, b""
    if prefix[:4] != MAGIC:
        return None, b""
    header_len, payload_len = struct.unpack("<II", prefix[4:])
    header = json.loads(sys.stdin.buffer.read(header_len).decode("utf-8"))
    payload = sys.stdin.buffer.read(payload_len)
    return header, payload

def write_frame(header, payload=b""):
    data = json.dumps(header, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(MAGIC + struct.pack("<II", len(data), len(payload)) + data + payload)
    sys.stdout.buffer.flush()

while True:
    header, payload = read_frame()
    if not header:
        break
    req_id = header.get("id", "")
    action = header.get("action", "")
    if action == "emit":
        write_frame({"type": "event", "event": "imu", "payload": {"seq": 7}}, b"abc")
        write_frame({"type": "response", "id": req_id, "result": {"success": True, "emitted": True}})
    elif action == "shutdown":
        write_frame({"type": "response", "id": req_id, "result": {"success": True}})
        break
    else:
        write_frame({"type": "response", "id": req_id, "result": {"success": True, "action": action, "payload": header.get("payload", {})}})
)PY";
  }
  chmod(script.c_str(), 0755);

  recordlab::StdioChannel channel;
  bool saw_event = false;
  channel.setEventCallback([&](const recordlab::json &header, const std::vector<uint8_t> &payload) {
    saw_event = header.value("event", "") == "imu" &&
                header["payload"]["seq"] == 7 &&
                std::string(payload.begin(), payload.end()) == "abc";
  });
  assert(channel.start("python3", {script}));

  auto ping = channel.request("ping", {{"value", 9}}, 1000);
  assert(ping["success"] == true);
  assert(ping["action"] == "ping");
  assert(ping["payload"]["value"] == 9);

  auto emitted = channel.request("emit", recordlab::json::object(), 1000);
  assert(emitted["success"] == true);
  for (int i = 0; i < 20 && !saw_event; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(saw_event);
  channel.stop();
  return 0;
}
