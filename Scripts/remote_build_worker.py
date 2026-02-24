#!/usr/bin/env python3

import argparse
import base64
import hashlib
import json
import os
import queue
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
import zipfile
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
from pathlib import Path
from typing import Dict, List, Optional


def _host_target_os() -> str:
    if os.name == "nt":
        return "Windows"
    if os.uname().sysname.lower() == "darwin":
        return "macOS"
    return "Linux"


def _host_system_token() -> str:
    host_os = _host_target_os()
    if host_os == "Windows":
        return "windows"
    if host_os == "macOS":
        return "macosx"
    return "linux"


def _scriptcore_name(target_os: str) -> str:
    if target_os == "Windows":
        return "ScriptCore.dll"
    if target_os == "macOS":
        return "libScriptCore.dylib"
    return "libScriptCore.so"


def _cfg_shortname(configuration: str, architecture: str) -> str:
    return f"{configuration.lower()}_{architecture.lower()}"


def _build_folder_name(configuration: str, target_os: str, architecture: str) -> str:
    token = "windows" if target_os == "Windows" else ("macosx" if target_os == "macOS" else "linux")
    return f"{_cfg_shortname(configuration, architecture)}-{token}-{architecture}"


@dataclass
class BuildJob:
    job_id: str
    request_payload: Dict
    status: str = "queued"
    logs: List[str] = field(default_factory=list)
    error_message: str = ""
    artifact_path: str = ""
    artifact_sha256: str = ""
    cancelled: bool = False

    def log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.logs.append(f"{timestamp} {message}")


class BuildJobStore:
    def __init__(self) -> None:
        self._jobs: Dict[str, BuildJob] = {}
        self._lock = threading.Lock()

    def add(self, job: BuildJob) -> None:
        with self._lock:
            self._jobs[job.job_id] = job

    def get(self, job_id: str) -> Optional[BuildJob]:
        with self._lock:
            return self._jobs.get(job_id)

    def list(self) -> Dict[str, BuildJob]:
        with self._lock:
            return dict(self._jobs)


class WorkerRuntime:
    def __init__(self, engine_root: Path, worker_count: int, auth_token: str) -> None:
        self.engine_root = engine_root
        self.auth_token = auth_token
        self.jobs = BuildJobStore()
        self.queue = queue.Queue()
        self.temp_root = Path(tempfile.gettempdir()) / "LimitlessRemoteWorker"
        self.temp_root.mkdir(parents=True, exist_ok=True)
        self._workers = []
        for idx in range(max(1, worker_count)):
            thread = threading.Thread(target=self._worker_loop, name=f"remote-build-worker-{idx}", daemon=True)
            thread.start()
            self._workers.append(thread)

    def enqueue(self, payload: Dict) -> BuildJob:
        job = BuildJob(job_id=str(uuid.uuid4()), request_payload=payload)
        self.jobs.add(job)
        self.queue.put(job.job_id)
        return job

    def cancel(self, job_id: str) -> bool:
        job = self.jobs.get(job_id)
        if not job:
            return False
        job.cancelled = True
        if job.status in {"queued", "running"}:
            job.status = "cancelled"
            job.error_message = "Cancelled by user request."
        return True

    def _worker_loop(self) -> None:
        while True:
            job_id = self.queue.get()
            try:
                job = self.jobs.get(job_id)
                if job is None:
                    continue
                if job.cancelled:
                    job.status = "cancelled"
                    continue
                self._execute_job(job)
            finally:
                self.queue.task_done()

    def _execute_job(self, job: BuildJob) -> None:
        payload = job.request_payload
        job.status = "running"
        job.log("Job started.")

        target_os = payload.get("targetOS", "")
        target_arch = payload.get("targetArchitecture", "")
        configuration = payload.get("configuration", "Dist")
        scripts_archive_b64 = payload.get("generatedScriptsArchiveBase64", "")

        if target_os != _host_target_os():
            job.status = "failed"
            job.error_message = (
                f"Worker host OS mismatch. Worker={_host_target_os()}, requested={target_os}."
            )
            job.log(job.error_message)
            return

        if target_arch not in {"x64", "ARM64"}:
            job.status = "failed"
            job.error_message = f"Unsupported target architecture: {target_arch}"
            job.log(job.error_message)
            return

        if configuration not in {"Debug", "Release", "Dist"}:
            job.status = "failed"
            job.error_message = f"Unsupported configuration: {configuration}"
            job.log(job.error_message)
            return

        if not scripts_archive_b64:
            job.status = "failed"
            job.error_message = "generatedScriptsArchiveBase64 is required."
            job.log(job.error_message)
            return

        job_root = self.temp_root / job.job_id
        if job_root.exists():
            shutil.rmtree(job_root)
        job_root.mkdir(parents=True, exist_ok=True)
        project_root = job_root / "project"
        generated_scripts_root = project_root / "Build" / "Generated" / "ScriptCore"
        generated_scripts_root.mkdir(parents=True, exist_ok=True)

        try:
            archive_bytes = base64.b64decode(scripts_archive_b64.encode("ascii"))
            with zipfile.ZipFile(BytesIO(archive_bytes), mode="r") as archive:
                archive.extractall(generated_scripts_root)
        except Exception as exc:
            job.status = "failed"
            job.error_message = f"Failed to decode/extract generated script archive: {exc}"
            job.log(job.error_message)
            return

        if not self._run_build_scripts(job, project_root, configuration, target_arch):
            return

        build_folder = _build_folder_name(configuration, target_os, target_arch)
        runtime_dir = self.engine_root / "Build" / build_folder / "Runtime"
        scriptcore_path = self.engine_root / "Build" / build_folder / "Editor" / _scriptcore_name(target_os)
        if not runtime_dir.is_dir():
            job.status = "failed"
            job.error_message = f"Runtime output directory missing after build: {runtime_dir}"
            job.log(job.error_message)
            return
        if not scriptcore_path.is_file():
            job.status = "failed"
            job.error_message = f"ScriptCore output missing after build: {scriptcore_path}"
            job.log(job.error_message)
            return

        artifact_zip = job_root / "artifacts.zip"
        if not self._package_artifacts(job, runtime_dir, scriptcore_path, artifact_zip):
            return

        artifact_sha = hashlib.sha256(artifact_zip.read_bytes()).hexdigest()
        job.artifact_path = str(artifact_zip)
        job.artifact_sha256 = artifact_sha
        job.status = "succeeded"
        job.log("Job finished successfully.")

    def _run_build_scripts(self, job: BuildJob, project_root: Path, configuration: str, target_arch: str) -> bool:
        scripts_root = self.engine_root / "Scripts"
        if os.name == "nt":
            scriptcore_script = scripts_root / "build-project-scriptcore-windows.bat"
            runtime_script = scripts_root / "build-runtime-windows.bat"
            if not scriptcore_script.exists() or not runtime_script.exists():
                job.status = "failed"
                job.error_message = "Missing required Windows build scripts."
                job.log(job.error_message)
                return False

            commands = [
                [
                    "cmd.exe",
                    "/c",
                    str(scriptcore_script),
                    configuration,
                    target_arch,
                    str(project_root),
                ],
                [
                    "cmd.exe",
                    "/c",
                    str(runtime_script),
                    configuration,
                    target_arch,
                ],
            ]
        else:
            scriptcore_script = scripts_root / "build-project-scriptcore-unix.sh"
            runtime_script = scripts_root / "build-runtime-unix.sh"
            if not scriptcore_script.exists() or not runtime_script.exists():
                job.status = "failed"
                job.error_message = "Missing required Unix build scripts."
                job.log(job.error_message)
                return False

            commands = [
                [
                    "bash",
                    str(scriptcore_script),
                    "--config",
                    configuration,
                    "--platform",
                    target_arch,
                    "--project-root",
                    str(project_root),
                ],
                [
                    "bash",
                    str(runtime_script),
                    "--config",
                    configuration,
                    "--platform",
                    target_arch,
                ],
            ]

        for command in commands:
            if job.cancelled:
                job.status = "cancelled"
                job.error_message = "Cancelled before command execution."
                return False

            command_text = " ".join(command)
            job.log(f"Running: {command_text}")
            process = subprocess.Popen(
                command,
                cwd=self.engine_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            assert process.stdout is not None
            for line in process.stdout:
                line = line.rstrip()
                if line:
                    job.log(line)

                if job.cancelled:
                    process.kill()
                    job.status = "cancelled"
                    job.error_message = "Cancelled during command execution."
                    return False

            exit_code = process.wait()
            if exit_code != 0:
                job.status = "failed"
                job.error_message = f"Build script failed with exit code {exit_code}."
                job.log(job.error_message)
                return False

        return True

    def _package_artifacts(self, job: BuildJob, runtime_dir: Path, scriptcore_path: Path, output_zip: Path) -> bool:
        try:
            with zipfile.ZipFile(output_zip, mode="w", compression=zipfile.ZIP_DEFLATED) as archive:
                for path in runtime_dir.rglob("*"):
                    if not path.is_file():
                        continue
                    rel = path.relative_to(runtime_dir).as_posix()
                    archive.write(path, f"Runtime/{rel}")
                archive.write(scriptcore_path, f"ScriptCore/{scriptcore_path.name}")
            job.log(f"Packaged artifacts into {output_zip}")
            return True
        except Exception as exc:
            job.status = "failed"
            job.error_message = f"Failed to package artifacts: {exc}"
            job.log(job.error_message)
            return False


def _json_response(handler: BaseHTTPRequestHandler, payload: Dict, status: HTTPStatus = HTTPStatus.OK) -> None:
    body = json.dumps(payload).encode("utf-8")
    handler.send_response(status.value)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def _binary_response(handler: BaseHTTPRequestHandler, payload: bytes, status: HTTPStatus = HTTPStatus.OK) -> None:
    handler.send_response(status.value)
    handler.send_header("Content-Type", "application/octet-stream")
    handler.send_header("Content-Length", str(len(payload)))
    handler.end_headers()
    handler.wfile.write(payload)


def _parse_json_body(handler: BaseHTTPRequestHandler) -> Dict:
    length = int(handler.headers.get("Content-Length", "0"))
    raw = handler.rfile.read(length) if length > 0 else b"{}"
    return json.loads(raw.decode("utf-8"))


def _authorized(handler: BaseHTTPRequestHandler, runtime: WorkerRuntime) -> bool:
    if not runtime.auth_token:
        return True
    auth_header = handler.headers.get("Authorization", "")
    expected = f"Bearer {runtime.auth_token}"
    return auth_header == expected


def build_handler_factory(runtime: WorkerRuntime):
    class BuildHandler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args):  # noqa: A003
            return

        def do_GET(self):  # noqa: N802
            if not _authorized(self, runtime):
                _json_response(self, {"error": "Unauthorized"}, HTTPStatus.UNAUTHORIZED)
                return

            path = self.path.rstrip("/")
            if path == "/api/v1/health":
                _json_response(
                    self,
                    {
                        "status": "ok",
                        "hostOS": _host_target_os(),
                        "hostSystemToken": _host_system_token(),
                    },
                    HTTPStatus.OK,
                )
                return

            parts = path.split("/")
            if len(parts) == 5 and parts[:4] == ["", "api", "v1", "build"]:
                job_id = parts[4]
                job = runtime.jobs.get(job_id)
                if not job:
                    _json_response(self, {"error": "Job not found"}, HTTPStatus.NOT_FOUND)
                    return
                payload = {
                    "jobId": job.job_id,
                    "status": job.status,
                    "logs": job.logs,
                    "errorMessage": job.error_message,
                    "artifactSha256": job.artifact_sha256,
                }
                if job.status == "succeeded" and job.artifact_path:
                    payload["artifactUrl"] = f"/api/v1/build/{job.job_id}/artifact"
                _json_response(self, payload, HTTPStatus.OK)
                return

            if len(parts) == 6 and parts[:4] == ["", "api", "v1", "build"] and parts[5] == "artifact":
                job_id = parts[4]
                job = runtime.jobs.get(job_id)
                if not job:
                    _json_response(self, {"error": "Job not found"}, HTTPStatus.NOT_FOUND)
                    return
                if job.status != "succeeded" or not job.artifact_path:
                    _json_response(self, {"error": "Artifact not ready"}, HTTPStatus.CONFLICT)
                    return
                artifact_bytes = Path(job.artifact_path).read_bytes()
                _binary_response(self, artifact_bytes, HTTPStatus.OK)
                return

            _json_response(self, {"error": "Not found"}, HTTPStatus.NOT_FOUND)

        def do_POST(self):  # noqa: N802
            if not _authorized(self, runtime):
                _json_response(self, {"error": "Unauthorized"}, HTTPStatus.UNAUTHORIZED)
                return

            path = self.path.rstrip("/")
            if path == "/api/v1/build":
                try:
                    payload = _parse_json_body(self)
                except Exception as exc:
                    _json_response(self, {"error": f"Invalid JSON body: {exc}"}, HTTPStatus.BAD_REQUEST)
                    return
                job = runtime.enqueue(payload)
                _json_response(self, {"jobId": job.job_id, "status": job.status}, HTTPStatus.ACCEPTED)
                return

            parts = path.split("/")
            if len(parts) == 6 and parts[:4] == ["", "api", "v1", "build"] and parts[5] == "cancel":
                job_id = parts[4]
                cancelled = runtime.cancel(job_id)
                if not cancelled:
                    _json_response(self, {"error": "Job not found"}, HTTPStatus.NOT_FOUND)
                    return
                _json_response(self, {"jobId": job_id, "status": "cancelled"}, HTTPStatus.OK)
                return

            _json_response(self, {"error": "Not found"}, HTTPStatus.NOT_FOUND)

    return BuildHandler


def main() -> int:
    parser = argparse.ArgumentParser(description="Limitless remote native desktop build worker")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--engine-root", default="")
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--auth-token", default=os.getenv("LIMITLESS_REMOTE_BUILD_TOKEN", ""))
    args = parser.parse_args()

    if args.engine_root:
        engine_root = Path(args.engine_root).resolve()
    else:
        engine_root = Path(__file__).resolve().parent.parent

    if not (engine_root / "Scripts").is_dir():
        print(f"[worker] invalid engine root (missing Scripts/): {engine_root}")
        return 1

    runtime = WorkerRuntime(engine_root=engine_root, worker_count=args.workers, auth_token=args.auth_token)
    server = ThreadingHTTPServer((args.host, args.port), build_handler_factory(runtime))
    print(f"[worker] listening on http://{args.host}:{args.port}")
    print(f"[worker] engine root: {engine_root}")
    print(f"[worker] host platform: {_host_target_os()}")
    if args.auth_token:
        print("[worker] auth token: enabled")
    else:
        print("[worker] auth token: disabled")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
