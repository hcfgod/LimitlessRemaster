#!/usr/bin/env python3

import argparse
import base64
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from io import BytesIO
from pathlib import Path
from typing import Callable, Dict, Optional


def _normalize_endpoint(endpoint: str) -> str:
    endpoint = endpoint.strip().rstrip("/")
    if not endpoint.startswith("http://") and not endpoint.startswith("https://"):
        endpoint = "http://" + endpoint
    return endpoint


def _request_json(url: str, method: str, payload: Optional[dict], headers: Dict[str, str], timeout: int) -> dict:
    data = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers = {**headers, "Content-Type": "application/json"}
    req = urllib.request.Request(url=url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        body = response.read().decode("utf-8")
        return json.loads(body) if body else {}


def _request_bytes(url: str, headers: Dict[str, str], timeout: int) -> bytes:
    req = urllib.request.Request(url=url, headers=headers, method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.read()


def _with_retry(fn: Callable[[], object], max_retries: int):
    attempt = 0
    while True:
        try:
            return fn()
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt >= max_retries:
                raise exc
            backoff = min(6.0, 1.0 * (2**attempt))
            print(f"[client] transient network failure: {exc}; retrying in {backoff:.1f}s", flush=True)
            time.sleep(backoff)
            attempt += 1


def _create_scripts_archive_base64(generated_scripts_dir: Path) -> str:
    if not generated_scripts_dir.is_dir():
        raise RuntimeError(f"generated scripts directory not found: {generated_scripts_dir}")

    buffer = BytesIO()
    with zipfile.ZipFile(buffer, mode="w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in generated_scripts_dir.rglob("*"):
            if not path.is_file():
                continue
            rel = path.relative_to(generated_scripts_dir).as_posix()
            archive.write(path, rel)
    return base64.b64encode(buffer.getvalue()).decode("ascii")


def _scriptcore_name_for_target(target_os: str) -> str:
    if target_os == "Windows":
        return "ScriptCore.dll"
    if target_os == "macOS":
        return "libScriptCore.dylib"
    return "libScriptCore.so"


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _poll_build_job(endpoint: str,
                    job_id: str,
                    headers: dict[str, str],
                    timeout_seconds: int,
                    poll_interval_seconds: int,
                    max_retries: int) -> dict:
    deadline = time.time() + timeout_seconds
    last_log_index = 0

    while True:
        if time.time() > deadline:
            cancel_url = f"{endpoint}/api/v1/build/{job_id}/cancel"
            try:
                _with_retry(lambda: _request_json(cancel_url, "POST", {}, headers, timeout=30), max_retries)
            except Exception:
                pass
            raise RuntimeError(f"build timed out after {timeout_seconds}s (job: {job_id})")

        status_url = f"{endpoint}/api/v1/build/{job_id}"
        state = _with_retry(lambda: _request_json(status_url, "GET", None, headers, timeout=30), max_retries)

        logs = state.get("logs", [])
        for line in logs[last_log_index:]:
            print(f"[worker] {line}", flush=True)
        last_log_index = len(logs)

        status = state.get("status", "")
        if status in {"succeeded", "failed", "cancelled"}:
            return state

        time.sleep(max(1, poll_interval_seconds))


def main() -> int:
    parser = argparse.ArgumentParser(description="Limitless remote build API client")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--target-os", required=True, choices=["Windows", "macOS", "Linux"])
    parser.add_argument("--target-arch", required=True, choices=["x64", "ARM64"])
    parser.add_argument("--config", required=True, choices=["Debug", "Release", "Dist"])
    parser.add_argument("--build-backend", required=True, choices=["LegacySdk", "InternalToolchain"])
    parser.add_argument("--generated-scripts-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--pool", default="default")
    parser.add_argument("--auth-token", default="")
    parser.add_argument("--timeout-seconds", type=int, default=1200)
    parser.add_argument("--poll-interval-seconds", type=int, default=2)
    parser.add_argument("--max-retries", type=int, default=3)
    args = parser.parse_args()

    endpoint = _normalize_endpoint(args.endpoint)
    output_dir = Path(args.output_dir)
    generated_scripts_dir = Path(args.generated_scripts_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    headers: Dict[str, str] = {"Accept": "application/json"}
    if args.auth_token:
        headers["Authorization"] = f"Bearer {args.auth_token}"

    scripts_archive_b64 = _create_scripts_archive_base64(generated_scripts_dir)
    build_request = {
        "protocolVersion": 1,
        "configuration": args.config,
        "targetOS": args.target_os,
        "targetArchitecture": args.target_arch,
        "buildBackend": args.build_backend,
        "pool": args.pool,
        "generatedScriptsArchiveBase64": scripts_archive_b64,
        "submittedAtUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }

    print(f"[client] submitting remote build to {endpoint}", flush=True)
    submit_url = f"{endpoint}/api/v1/build"
    submit_response = _with_retry(
        lambda: _request_json(submit_url, "POST", build_request, headers, timeout=60),
        args.max_retries,
    )
    job_id = submit_response.get("jobId", "")
    if not job_id:
        raise RuntimeError("remote build API did not return a jobId")
    print(f"[client] job queued: {job_id}", flush=True)

    state = _poll_build_job(
        endpoint=endpoint,
        job_id=job_id,
        headers=headers,
        timeout_seconds=max(30, args.timeout_seconds),
        poll_interval_seconds=max(1, args.poll_interval_seconds),
        max_retries=max(0, args.max_retries),
    )

    status = state.get("status", "")
    if status != "succeeded":
        raise RuntimeError(state.get("errorMessage", f"remote build job ended with status '{status}'"))

    artifact_url = state.get("artifactUrl", "")
    if not artifact_url:
        raise RuntimeError("remote build job succeeded but no artifactUrl was returned")
    artifact_sha256 = state.get("artifactSha256", "")

    if artifact_url.startswith("/"):
        artifact_url = endpoint + artifact_url

    artifact_bytes = _with_retry(
        lambda: _request_bytes(artifact_url, headers, timeout=120),
        args.max_retries,
    )
    if artifact_sha256:
        local_sha = _sha256_bytes(artifact_bytes)
        if local_sha.lower() != artifact_sha256.lower():
            raise RuntimeError(
                f"artifact checksum mismatch: expected {artifact_sha256}, got {local_sha}"
            )

    artifacts_zip = output_dir / "artifacts.zip"
    artifacts_zip.write_bytes(artifact_bytes)

    artifacts_dir = output_dir / "artifacts"
    if artifacts_dir.exists():
        shutil.rmtree(artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(artifacts_zip, mode="r") as archive:
        archive.extractall(artifacts_dir)

    runtime_dir = artifacts_dir / "Runtime"
    scriptcore_dir = artifacts_dir / "ScriptCore"
    scriptcore_path = scriptcore_dir / _scriptcore_name_for_target(args.target_os)
    managed_payload_dir = runtime_dir / "Managed"
    if not runtime_dir.is_dir():
        raise RuntimeError(f"remote artifact missing Runtime directory: {runtime_dir}")
    if not scriptcore_path.is_file():
        raise RuntimeError(f"remote artifact missing ScriptCore library: {scriptcore_path}")
    if not managed_payload_dir.is_dir():
        raise RuntimeError(f"remote artifact missing managed payload directory: {managed_payload_dir}")

    manifest = {
        "targetOS": args.target_os,
        "targetArchitecture": args.target_arch,
        "runtimeDirectory": str(runtime_dir.resolve()),
        "scriptCoreLibraryPath": str(scriptcore_path.resolve()),
        "managedPayloadDirectory": str(managed_payload_dir.resolve()),
        "dynamicLibraryDirectories": [str(runtime_dir.resolve())],
        "sourceJobId": job_id,
        "artifactSha256": artifact_sha256,
    }
    manifest_path = output_dir / "remote_artifact_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(f"[client] artifact ready at {artifacts_dir}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[client] error: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
