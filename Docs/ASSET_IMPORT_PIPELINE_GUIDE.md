# Asset Import Pipeline Guide (Editor/tooling)

This document describes the **Asset Import Pipeline** used by the editor to scan and (re)import assets under `Assets/`.

> This pipeline is separate from runtime asset loading (`AssetImporter<T>::LoadAsync`). Runtime loading focuses on correctness and async CPU/GPU staging; the import pipeline focuses on **incremental project-wide import**.

## Goals

- Discover known asset types under `Assets/`.
- Incrementally reimport only what changed.
- Cascade reimport/reload through dependencies (Unity-style).
- Provide an editor-facing “Validate Asset Database” command.

## Where it lives

- `Limitless/Source/Assets/AssetImportPipeline.{h,cpp}`
- `Limitless/Source/Assets/AssetDatabase.{h,cpp}`

## Running from the editor

Menu:

- **Assets → Reimport Changed**
- **Assets → Reimport All**
- **Assets → Validate Asset Database**

These commands are intended for “I changed a bunch of stuff, sync everything” workflows, and for debugging missing/stale records.

## Incremental import rules

The incremental pass is **best-effort** and currently based on:

- Source file size (`sourceSizeBytes`)
- Source last write time ticks (`sourceLastWriteTimeTicks`)
- Importer version (`importerVersion`)

These fields are stored per record in the persistent database (`Build/AssetDatabase.json`).

If an asset is up-to-date under these rules, it is skipped during “Reimport Changed”.

## Dependency cascade

When an asset is imported, the pipeline can cascade to its **dependents** using the reverse dependency graph maintained by `AssetDatabase`.

This supports workflows like:

- “Shader changed → reimport/reload materials that depend on it”
- “Texture changed → reimport/reload materials that depend on it”

## Validate Asset Database

Validation reports common issues such as:

- Records whose `resolvedPath` no longer exists on disk
- Duplicate GUIDs mapped to multiple keys (should not occur, but detected defensively)

The editor also exposes a UI panel with safe fix-ups:

- **Window → Asset Diagnostics**

## Troubleshooting

### “Reimport Changed didn’t pick up my change”

- Ensure the file was actually saved (last write time must update).
- If a tool writes files via atomic swap or preserves timestamps, use **Reimport All**.

### “References broke after I regenerated a GUID”

That is expected: GUID regeneration is a deliberate “break references” tool (Unity-style).
Use the **Asset Diagnostics** panel to locate missing references and repair or replace them.

