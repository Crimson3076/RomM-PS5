# Third-party components

This project vendors a small number of third-party source files directly
(no package manager available in the PS5 cross-compilation environment).
Each is small, single-purpose, source-only (no prebuilt binaries), and
kept in its own directory with its original license.

## cjson/ — cJSON

- Upstream: https://github.com/DaveGamble/cJSON
- Version: v1.7.18 (pinned tag)
- License: MIT (`cjson/LICENSE`)
- Why: RomM's API returns JSON. Hand-rolling a fully correct generic JSON
  parser (escape sequences, unicode, nested structures, number formats)
  under this project's time constraints was judged riskier than reusing a
  small, extremely widely-deployed, permissively-licensed implementation
  of a public, stable spec (RFC 8259). Used by `src/romm_api/romm_api_http.c`.
- Modifications: none (vendored verbatim).

## font8x8/ — font8x8_basic.h

- Upstream: https://github.com/dhepper/font8x8
- License: Public Domain (`font8x8/README-LICENSE.md`), itself based on
  Marcel Sondaar's public-domain VGA font work.
- Why: The PS5 on-console UI needs to render actual game titles as text,
  not just placeholder bars — this needed real glyph bitmap data. Rather
  than hand-transcribing bitmap font data from memory (a real risk of
  silently-wrong pixel patterns that can't be visually verified without
  PS5 hardware), this uses a verified, widely-used public-domain 8x8 font
  fetched directly from its own repository. Used by
  `src/ps5/font_render.c`.
- Modifications: none (vendored verbatim; only `font8x8_basic.h` is used,
  covering basic Latin/ASCII, which is all this project's UI needs).

## PS5 tiled-framebuffer addressing (not a vendored file — adapted, cited inline)

`src/ps5/tilemap.c` in this project reimplements the pixel-tiling address
math from `ps5-payload-dev/SDL`'s `src/video/ps5/SDL_ps5tilemap.c`
(https://github.com/ps5-payload-dev/SDL, branch `release-2.30.x-ps5`,
zlib licensed) with SDL's types (`Uint32`, `SDL_Rect`, `SDL_malloc`, ...)
replaced by plain C standard library equivalents, since this project does
not build or link SDL2 for the PS5 target (see `docs/architecture.md` §5a
for why). The tiling algorithm itself (`PS5_TileOffset`) is copied as-is —
it is real, working logic for a real hardware requirement (the PS5 GPU's
scanout buffer needs pixels in a swizzled/tiled layout, not plain
row-major order) that this project has no way to independently verify
without hardware, so reusing a maintained implementation was judged safer
than re-deriving the bit-interleaving pattern from scratch. Retains the
original zlib license notice in the file header per that license's terms.
