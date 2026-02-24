# Remote Build API Guide

This document defines the desktop remote build contract used by the editor's `Execution Mode = Remote`.

## Scope

- Platforms: `Windows`, `macOS`, `Linux`
- Architectures: `x64`, `ARM64`
- Build configurations: `Debug`, `Release`, `Dist` (editor currently ships `Dist`)
- Transport: JSON over HTTP

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

### Cancel Build

- `POST /api/v1/build/{jobId}/cancel`

### Download Artifact

- `GET /api/v1/build/{jobId}/artifact`
- Returns binary zip payload.

## Artifact Package Contract (Desktop v1)

The worker returns a zip with:

```text
Runtime/
  Runtime.exe|Runtime
  config.json
  *.dll|*.so|*.dylib
ScriptCore/
  ScriptCore.dll|libScriptCore.so|libScriptCore.dylib
```

The editor client extracts this package, validates checksum, then writes:

- `remote_artifact_manifest.json`
- `artifacts/Runtime/...`
- `artifacts/ScriptCore/...`

## Reliability Features

- Retry + exponential backoff for network requests (`remote_build_client.py`)
- Job timeout with server-side cancellation
- Worker log streaming via poll API
- SHA-256 artifact verification before extraction
- Optional local fallback when remote dispatch fails and host/target match

## Endpoint Routing

Editor settings can route by target OS before dispatch:

- `remoteBuildEndpointWindows`
- `remoteBuildEndpointMacOS`
- `remoteBuildEndpointLinux`
- fallback: `remoteBuildEndpoint`

When `useTargetEndpointRouting=true`, the editor resolves endpoint in this order:

1. target-specific endpoint for selected target OS
2. fallback endpoint
