# Remote Build API Guide

This document defines the desktop remote build contract used by the editor's `Execution Mode = Remote`.

## Scope

- Platforms: `Windows`, `macOS`, `Linux`
- Architectures: `x64`, `ARM64`
- Build configurations: `Debug`, `Release`, `Dist` (editor currently ships `Dist`)
- Transport: JSON over HTTP
- Current client/worker protocol version: `1`

Note: with `Execution Mode = Auto`, remote dispatch is still used whenever local routing is unavailable (for example Windows -> macOS, or Windows -> Linux without WSL).

## Worker Process

Start a native worker on each target platform host:

```bash
python Scripts/remote_build_worker.py --host 0.0.0.0 --port 8080 --engine-root /path/to/LimitlessRemaster --workers 1
```

Optional auth token:

```bash
LIMITLESS_REMOTE_BUILD_TOKEN=your-token python Scripts/remote_build_worker.py --port 8080
```

Auth note:

- when a worker auth token is configured, the current implementation requires the `Authorization: Bearer ...` header on all API endpoints, including `GET /api/v1/health`

The worker currently:

- validates target OS/architecture/configuration
- extracts the submitted generated ScriptCore mirror into a temp project area
- runs project ScriptCore build + runtime build scripts on the worker host
- packages produced artifacts into a zip
- exposes status/log polling plus artifact download endpoints

## API Endpoints

### Health

- `GET /api/v1/health`
- Response:

```json
{
  "status": "ok",
  "hostOS": "Windows",
  "hostSystemToken": "windows"
}
```

### Submit Build

- `POST /api/v1/build`
- Request:

```json
{
  "protocolVersion": 1,
  "configuration": "Dist",
  "targetOS": "Windows",
  "targetArchitecture": "x64",
  "buildBackend": "LegacySdk",
  "pool": "default",
  "generatedScriptsArchiveBase64": "<zip-bytes-base64>",
  "submittedAtUtc": "2026-02-24T12:34:56Z"
}
```

Notes:

- `generatedScriptsArchiveBase64` is required.
- The current worker validates `targetOS`, `targetArchitecture`, and `configuration`.
- `protocolVersion` is currently sent by the client, but the worker does not yet reject mismatches via strict protocol-version gating.
- `buildBackend` is part of the request contract and is forwarded by the client, even though the current worker-side implementation is driven primarily by the platform build scripts it invokes.
- `pool` is part of the request contract and is forwarded by the client, but the current worker implementation does not yet route or schedule jobs by pool label.

- Response:

```json
{
  "jobId": "7c48f95f-85ec-45d8-8fd5-fc69db308ac2",
  "status": "queued"
}
```

### Poll Build

- `GET /api/v1/build/{jobId}`
- Response (running):

```json
{
  "jobId": "7c48f95f-85ec-45d8-8fd5-fc69db308ac2",
  "status": "running",
  "logs": ["12:02:10 Job started.", "12:02:12 Running: ..."],
  "errorMessage": "",
  "artifactSha256": ""
}
```

- Response (success):

```json
{
  "jobId": "7c48f95f-85ec-45d8-8fd5-fc69db308ac2",
  "status": "succeeded",
  "logs": ["..."],
  "errorMessage": "",
  "artifactSha256": "<sha256-hex>",
  "artifactUrl": "/api/v1/build/7c48f95f-85ec-45d8-8fd5-fc69db308ac2/artifact"
}
```

Failure/cancel states use the same polling endpoint with:

- `status`: `failed` or `cancelled`
- `errorMessage`: populated with the failure reason when available

### Cancel Build

- `POST /api/v1/build/{jobId}/cancel`

### Download Artifact

- `GET /api/v1/build/{jobId}/artifact`
- Returns binary zip payload.

## Artifact Package Contract

The worker returns a zip with:

```text
Runtime/
  Runtime.exe|Runtime
  config.json
  Managed/
    ...
  *.dll|*.so|*.dylib
ScriptCore/
  ScriptCore.dll|libScriptCore.so|libScriptCore.dylib
```

Important current behavior:

- the worker packages **all files recursively** under the built `Runtime/` directory
- this means the managed payload is expected to be present under `Runtime/Managed/`
- the client currently treats the managed payload directory as required
- `ScriptCore/` contains the platform-native ScriptCore library used by the packaged/runtime build flow

The editor client extracts this package, validates checksum, then writes:

- `remote_artifact_manifest.json`
- `artifacts/Runtime/...`
- `artifacts/ScriptCore/...`

The generated manifest currently records:

- `targetOS`
- `targetArchitecture`
- `runtimeDirectory`
- `scriptCoreLibraryPath`
- `managedPayloadDirectory`
- `dynamicLibraryDirectories`
- `sourceJobId`
- `artifactSha256`

The C++ provider then parses that manifest and exposes:

- staging root
- runtime directory
- ScriptCore library path
- managed payload directory
- dynamic library source directories

## Reliability Features

- Retry + exponential backoff for network requests (`remote_build_client.py`)
- Job timeout with server-side cancellation
- Worker log streaming via poll API
- SHA-256 artifact verification before extraction
- Optional local fallback when remote dispatch fails and either host/target match or Windows->Linux local WSL cross-build is available

## Current Build Script Behavior

The current worker implementation invokes:

- Windows:
  - `Scripts/build-project-scriptcore-windows.bat`
  - `Scripts/build-runtime-windows.bat`

- Unix:
  - `Scripts/build-project-scriptcore-unix.sh`
  - `Scripts/build-runtime-unix.sh`

The generated ScriptCore mirror submitted by the client is unpacked into:

- `<temp-job-root>/project/Build/Generated/ScriptCore`

## Endpoint Routing

Editor settings can route by target OS before dispatch:

- `remoteBuildEndpointWindows`
- `remoteBuildEndpointMacOS`
- `remoteBuildEndpointLinux`
- fallback: `remoteBuildEndpoint`

When `useTargetEndpointRouting=true`, the editor resolves endpoint in this order:

1. target-specific endpoint for selected target OS
2. fallback endpoint

## Related Files

- `Scripts/remote_build_worker.py`
- `Scripts/remote_build_client.py`
- `Limitless/Source/Project/RemoteBuildProvider.{h,cpp}`
- `Limitless/Source/Project/GameBuilder.cpp`
