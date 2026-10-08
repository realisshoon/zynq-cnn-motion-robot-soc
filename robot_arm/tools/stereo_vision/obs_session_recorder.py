#!/usr/bin/env python3
"""Record the OBS program scene alongside a UART monitor session."""

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid


ROOT = Path(__file__).resolve().parents[2]


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    deadline = time.monotonic() + 2
    while True:
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


def default_config():
    return Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming")) / "obs-studio/plugin_config/obs-websocket/config.json"


def authentication(password, challenge):
    secret = base64.b64encode(hashlib.sha256((password + challenge["salt"]).encode()).digest()).decode()
    return base64.b64encode(hashlib.sha256((secret + challenge["challenge"]).encode()).digest()).decode()


class OBS:
    def __init__(self, config_path=None):
        from websocket import create_connection

        config = read_json(config_path or default_config())
        if not config or not config.get("server_enabled"):
            raise RuntimeError("Enable the authenticated OBS WebSocket server first")
        self.socket = create_connection(f"ws://127.0.0.1:{int(config.get('server_port', 4455))}", timeout=8)
        try:
            hello = json.loads(self.socket.recv())
            if hello.get("op") != 0:
                raise RuntimeError("Not an OBS WebSocket v5 server")
            identify = {"rpcVersion": 1, "eventSubscriptions": 0}
            challenge = hello["d"].get("authentication")
            if challenge:
                identify["authentication"] = authentication(config["server_password"], challenge)
            self.socket.send(json.dumps({"op": 1, "d": identify}))
            if json.loads(self.socket.recv()).get("op") != 2:
                raise RuntimeError("OBS authentication failed")
        except BaseException:
            self.socket.close()
            raise

    def call(self, request, **values):
        identity = uuid.uuid4().hex
        self.socket.send(json.dumps({"op": 6, "d": {"requestType": request, "requestId": identity, "requestData": values}}))
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            response = json.loads(self.socket.recv())
            if response.get("op") != 7 or response["d"].get("requestId") != identity:
                continue
            data = response["d"]
            if not data["requestStatus"]["result"]:
                raise RuntimeError(f"OBS {request} failed (code {data['requestStatus']['code']})")
            return data.get("responseData", {})
        raise TimeoutError(f"OBS {request} response timed out")

    def close(self):
        self.socket.close()


def status_sample(client):
    before = time.time()
    status = client.call("GetRecordStatus")
    after = time.time()
    return {"utc": datetime.fromtimestamp((before + after) / 2, timezone.utc).isoformat(),
            "round_trip_ms": (after - before) * 1000,
            "video_seconds": status["outputDuration"] / 1000,
            "active": status["outputActive"], "paused": status["outputPaused"]}


def wait_record_state(client, active, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = client.call("GetRecordStatus")
        if status["outputActive"] == active:
            return
        time.sleep(0.1)
    raise TimeoutError(f"OBS output did not reach active={active}; inspect OBS before starting another recording")


def wait_file_finalized(path, timeout=8):
    deadline = time.monotonic() + timeout
    previous = None
    stable = 0
    while time.monotonic() < deadline:
        if path.is_file():
            current = path.stat().st_size
            stable = stable + 1 if current == previous and current > 0 else 0
            if stable >= 3:
                return
            previous = current
        time.sleep(0.1)
    raise TimeoutError("OBS output file was not finalized; original recording was preserved")


def inspect(client):
    scene = client.call("GetCurrentProgramScene")["sceneName"]
    return {"version": client.call("GetVersion"), "scene_name": scene,
            "video_settings": client.call("GetVideoSettings"),
            "scene_items": client.call("GetSceneItemList", sceneName=scene)["sceneItems"],
            "record_status": client.call("GetRecordStatus")}


def scene_signature(scene_name, items):
    layout = [{"name": item["sourceName"], "enabled": item["sceneItemEnabled"],
               "transform": item["sceneItemTransform"]} for item in items]
    return json.dumps([scene_name, layout], sort_keys=True, ensure_ascii=False)


def file_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def browser_video(path):
    if path.suffix.lower() == ".mp4":
        return path
    from imageio_ffmpeg import get_ffmpeg_exe

    output = path.with_name(path.stem + ".browser.mp4")
    command = [get_ffmpeg_exe(), "-nostdin", "-v", "error", "-n", "-i", str(path),
               "-map", "0:v:0", "-map", "0:a?", "-c", "copy", "-movflags", "+faststart", str(output)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    path.with_name(path.stem + ".remux.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode or not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError("MP4 stream-copy remux failed; original recording was preserved")
    return output


def discover_session(directory, earliest):
    candidates = []
    for path in directory.glob("monitor_*/session.json"):
        session = read_json(path)
        if not session or session.get("status") != "running" or (path.parent / "obs/recording.json").exists():
            continue
        try:
            started = datetime.fromisoformat(session["started_utc"]).timestamp()
        except (ValueError, KeyError):
            continue
        if started >= earliest:
            candidates.append((started, path.parent))
    return max(candidates, default=(None, None), key=lambda candidate: candidate[0])[1]


def restore_directory(client, original, temporary):
    current = client.call("GetRecordDirectory")["recordDirectory"]
    if Path(current).resolve() != temporary.resolve():
        return "not_restored_user_changed_directory"
    if client.call("GetRecordStatus")["outputActive"]:
        return "not_restored_recording_still_active"
    client.call("SetRecordDirectory", recordDirectory=original)
    return "restored"


def record_session(client, session_path, poll=0.5, max_seconds=3600,
                   stop_event=None, on_started=None):
    session_path = session_path.resolve()
    session = read_json(session_path / "session.json")
    if not session or session.get("status") != "running":
        raise RuntimeError("Select a running UART monitor session")
    details = inspect(client)
    if details["record_status"]["outputActive"]:
        raise RuntimeError("OBS is already recording; the existing recording will not be stopped")
    if "SetRecordDirectory" not in details["version"].get("availableRequests", []):
        raise RuntimeError("OBS WebSocket >=5.3 with SetRecordDirectory is required")
    output = session_path / "obs"
    output.mkdir(exist_ok=True)
    lock_path = output / "recorder.lock"
    lock = lock_path.open("x", encoding="ascii")
    original = None
    owned = False
    started_at = time.monotonic()
    layout_check = 0
    initial_layout = scene_signature(details["scene_name"], details["scene_items"])
    record = {"status": "starting", "session": str(session_path), "created_utc": utc_now(),
              "samples": [], "scene": details, "errors": [], "layout_changed": False, "not_exposure_synchronized": True,
              "timing_note": "Host receipt UTC vs OBS output duration; HDMI/capture/UART delay remains unknown."}
    manifest = output / "recording.json"
    try:
        if manifest.exists():
            raise RuntimeError("This session already has a recording manifest; refusing to overwrite")
        original = client.call("GetRecordDirectory")["recordDirectory"]
        record["original_record_directory"] = original
        write_json(manifest, record)
        client.call("SetRecordDirectory", recordDirectory=str(output))
        record["start_request_utc"] = utc_now()
        client.call("StartRecord")
        owned = True
        wait_record_state(client, True)
        record["start_response_utc"] = utc_now()
        record["status"] = "recording"
        write_json(manifest, record)
        print(f"[OBS] recording: {output}", flush=True)
        if on_started is not None:
            on_started(record)
        while stop_event is None or not stop_event.is_set():
            sample = status_sample(client)
            record["samples"].append(sample)
            if time.monotonic() - layout_check >= 2:
                name = client.call("GetCurrentProgramScene")["sceneName"]
                items = client.call("GetSceneItemList", sceneName=name)["sceneItems"]
                if scene_signature(name, items) != initial_layout:
                    record["layout_changed"] = True
                layout_check = time.monotonic()
            write_json(manifest, record)
            if not sample["active"]:
                owned = False
                raise RuntimeError("OBS never became active or was stopped externally; output path is unverified")
            session = read_json(session_path / "session.json")
            if session and session.get("status") in ("stopped", "interrupted", "error"):
                break
            if time.monotonic() - started_at >= max_seconds:
                record["status"] = "time_limit_reached"
                print("[OBS] recording time limit reached; UART/robot state was NOT changed", flush=True)
                break
            if stop_event is None:
                time.sleep(poll)
            else:
                stop_event.wait(poll)
    except KeyboardInterrupt:
        record["status"] = "interrupted"
    except Exception as error:
        record["status"] = "error"
        record["errors"].append(str(error))
    finally:
        if owned:
            try:
                record["stop_request_utc"] = utc_now()
                result = client.call("StopRecord")
                wait_record_state(client, False)
                record["stop_response_utc"] = utc_now()
                video = Path(result["outputPath"]).resolve()
                if video.parent != output:
                    raise RuntimeError("OBS output was not found in the session folder")
                wait_file_finalized(video)
                record["video_path"] = str(video)
                record["video_bytes"] = video.stat().st_size
                record["video_sha256"] = file_digest(video)
                try:
                    browser = browser_video(video)
                    record["browser_video_path"] = str(browser)
                    record["browser_video_bytes"] = browser.stat().st_size
                except Exception as error:
                    record["remux_warning"] = str(error)
                    print(f"[OBS] remux warning: {error}", flush=True)
                if record["status"] == "recording":
                    record["status"] = "complete"
                print(f"[OBS] saved: {video}", flush=True)
            except Exception as error:
                record["status"] = "error"
                record["errors"].append(str(error))
        if original is not None:
            try:
                record["directory_restore"] = restore_directory(client, original, output)
            except Exception as error:
                record["directory_restore"] = "failed"
                record["errors"].append(str(error))
        record["ended_utc"] = utc_now()
        if original is not None:
            write_json(manifest, record)
        lock.close()
        lock_path.unlink()
    if record["errors"]:
        raise RuntimeError("; ".join(record["errors"]))
    return record


class SessionRecorder:
    def __init__(self, session_path):
        self.session_path = Path(session_path).resolve()
        self.stop_event = threading.Event()
        self.ready = threading.Event()
        self.result = {"status": "starting"}
        self.thread = threading.Thread(target=self.run, name="obs-session-recorder")

    def started(self, record):
        self.result = {"status": "recording", "directory": str(self.session_path / "obs"),
                       "not_exposure_synchronized": True}
        self.ready.set()

    def run(self):
        client = None
        try:
            client = OBS()
            if self.stop_event.is_set():
                self.result = {"status": "cancelled"}
                return
            record = record_session(client, self.session_path, stop_event=self.stop_event,
                                    on_started=self.started)
            self.result = {key: record[key] for key in (
                "status", "video_path", "video_bytes", "video_sha256", "browser_video_path",
                "directory_restore", "remux_warning", "not_exposure_synchronized") if key in record}
        except Exception as error:
            self.result = {"status": "error", "error": str(error)}
            print(f"[OBS] WARNING: video recording failed: {error}; UART logging continues. "
                  "No video is guaranteed for this session.", flush=True)
        finally:
            try:
                if client is not None:
                    client.close()
            except Exception as error:
                self.result = {**self.result, "connection_close_warning": str(error)}
                print(f"[OBS] WARNING: connection cleanup failed: {error}", flush=True)
            self.ready.set()

    def start(self):
        print("[OBS] starting automatic session recording...", flush=True)
        self.thread.start()
        self.ready.wait()

    def close(self):
        self.stop_event.set()
        if self.thread.ident is not None:
            if self.thread.is_alive():
                print("[OBS] finalizing video; please wait...", flush=True)
            self.thread.join()

    def summary(self):
        return dict(self.result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--session", type=Path)
    mode.add_argument("--watch", type=Path, help="Wait for a newly started monitor session")
    mode.add_argument("--check", action="store_true", help="Read-only connection/scene check")
    parser.add_argument("--obs-config", type=Path, default=default_config())
    parser.add_argument("--max-seconds", type=float, default=3600, help="Recording-only time limit; no UART commands are sent")
    args = parser.parse_args()
    if not math.isfinite(args.max_seconds) or args.max_seconds <= 0:
        parser.error("--max-seconds must be finite and positive")
    client = None
    try:
        client = OBS(args.obs_config)
        if args.check:
            print(json.dumps(inspect(client), ensure_ascii=False, indent=2))
            return 0
        session = args.session
        if session is None:
            directory = (args.watch or ROOT / "captures").resolve()
            earliest = time.time() - 5
            print(f"[OBS] ready; waiting for a NEW UART session under {directory}", flush=True)
            while session is None:
                session = discover_session(directory, earliest)
                if session is None:
                    time.sleep(0.5)
        record_session(client, session, max_seconds=args.max_seconds)
        return 0
    except KeyboardInterrupt:
        return 0
    except Exception as error:
        print(f"[OBS] ERROR: {error}")
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main())
