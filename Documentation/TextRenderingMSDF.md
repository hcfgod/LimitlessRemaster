# MSDF Text Rendering Setup

## Overview

The engine now supports runtime 2D text rendering via vendored `msdf-atlas-gen` and `msdfgen`.

## What Is Implemented

- A runtime `Font` object that loads a `.ttf` or `.otf` file.
- MSDF atlas generation on load using vendored `msdf-atlas-gen`.
- A dedicated `Renderer2D` text pass that batches glyph quads and renders with an MSDF shader.
- A scene `TextComponent` with serialization, clone support, and scene rendering integration.

## New Runtime APIs

- `Limitless::Font::CreateFromFile(fontPath, specification)`
- `Renderer2D::DrawText(transform, text, font, fontSize, color)`

## Scene Component

`TextComponent` fields:

- `Text`: text content
- `FontFilePath`: path to a font file
- `FontSize`: requested render size
- `Color`: text tint and alpha

## Usage Example

1. Add `TextComponent` to an entity.
2. Set `FontFilePath` to a valid `.ttf` file.
3. Set `Text` and `FontSize`.
4. Run the scene; the renderer will build an MSDF atlas the first time the font is used.

## Current Scope

- Default runtime charset is printable ASCII plus newline/tab.
- Fonts are generated at runtime and cached per `TextComponent` instance.
- Text currently renders in the text pass after sprite rendering.
