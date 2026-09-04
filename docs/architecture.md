# RomM-PS5 Architecture (Milestone 0 — Research Findings)

Status: **DRAFT — proposed architecture, pending review before large-scale implementation.**

This document captures Milestone 0 research: SDK/toolchain selection, reference
projects, RomM API surface, and the download/extraction model. It intentionally
stops short of implementation. Several items are marked **UNVERIFIED** because
they could not be confirmed without a live RomM server and/or real PS5
hardware — both unavailable in this research environment. They must be
validated before Milestone 3 (safe test download) is considered complete.

---

## 1. SDK and toolchain

### Selected: `ps5-payload-dev/sdk`

- Repository: https://github.com/ps5-payload-dev/sdk
- This is the **successor/active fork** of the original `john-tornblom/ps5-payload-sdk`,
  which was **archived on 2024-04-10** and now redirects contributors to
  `ps5-payload-dev/sdk`. We select the active repository, not the archived one.
- Maintained by the `ps5-payload-dev` GitHub organization (John Törnblom and
  contributors), which also owns the majority of actively maintained PS5
  userland homebrew tooling (loaders, servers, SDL port).
- Provides a Clang/LLD-based cross toolchain (`prospero-clang`,
  `prospero-clang++`, `prospero-lld`) targeting the PS5's FreeBSD-derived
  userland on x86_64, a sysroot, custom CRT, BSD-compatible headers, and
  PS5-specific headers/stubs for calling into system SPRX libraries via
  dynamic linking. CMake and Make toolchain files are provided.
- License and activity: public repository with an active commit history
  (800+ commits) and tagged releases.

### What the SDK does **not** provide out of the box

This is an important, honestly-documented gap: `ps5-payload-sdk` is a bare
cross-compilation toolchain plus PS5 syscall/SPRX headers. It is **not** a
curated application framework. The following must be cross-compiled/ported
by us or pulled from existing community ports — they are not bundled parts
of the SDK release itself:

- HTTP/HTTPS client stack (libcurl + a TLS backend)
- JSON parsing library
- ZIP/deflate handling
- Image decoding (PNG/JPEG)

### Confirmed capability matrix

| Requirement | Status | Notes |
|---|---|---|
| Cross toolchain (C/C++) | **Available** | Clang/LLD via ps5-payload-dev/sdk |
| Controller input | **Available** | `ps5-payload-dev/SDL` — official PS5 port of SDL2 (zlib licensed), used by existing launchers (e.g. `Rufidj/Nativehbl`) for `SDL_GameController` input |
| Rendering surface for UI | **Available** | Same SDL2 port provides a renderer/window surface on PS5 |
| Multithreading | **Available** — UNVERIFIED extent | PS5 userland is FreeBSD-derived; pthreads is expected to be present via libc, consistent with other payload projects (e.g. `ftpsrv`, `websrv` are networked/concurrent), but we have not confirmed pthreads behavior on real hardware |
| HTTP client (libcurl) | **Likely portable — UNVERIFIED** | No bundled port found in this research pass; `ps5-payload-dev/fetchpkg` is curl-based but targets desktop/Linux/Windows, not confirmed to run on PS5 itself. Community FTP/web-server payloads (`ftpsrv`, `websrv`) demonstrate BSD socket networking works on-device, but a full TLS-capable HTTP client stack on-device is not yet confirmed. This is the single biggest open technical risk for Milestone 2. |
| TLS / certificate verification | **UNVERIFIED** | Depends on porting libcurl + a TLS backend (OpenSSL or mbedTLS) to the PS5 sysroot. No PS5-specific TLS port was found in this research pass. Must be prototyped early (Milestone 1/2 spike) before committing to the download-manager design. |
| Custom CA support | **UNVERIFIED** | Contingent on the above; if libcurl ports cleanly, custom CA bundles are a standard `CURLOPT_CAINFO` concern, not a new risk. |
| JSON parsing | **Should be portable** | No PS5-specific dependency; a header-only/small-footprint C JSON library (e.g. cJSON) has no known PS5-blocking dependency, but has not been proven to build against the sysroot in this research pass. |
| ZIP extraction | **Should be portable, UNVERIFIED on-device** | Same caveat as JSON: miniz/zlib-style code has no PS5-specific dependency, but must be validated against the actual libc/sysroot. |
| Large files (>100 GB) | **UNVERIFIED — filesystem-dependent** | Depends on the target filesystem (internal storage vs exFAT/UFS USB mounts) supporting large files and PS5 syscalls supporting 64-bit offsets. Must be tested on real hardware with an actual >100 GB transfer; do not assume this works until verified. |
| PNG/JPEG decode | **Should be available via SDL_image** | SDL_image itself depends on libpng/libjpeg (or a "simplified" built-in decoder mode); no PS5-specific SDL_image port was located in this pass — only the base SDL2 port. This needs its own porting spike. |
| Persistent configuration | **Available** | Standard filesystem writes to app data directory; no PS5-specific blocker identified. |
| Free-space reporting | **UNVERIFIED** | Needs a PS5-specific statvfs-equivalent syscall; not confirmed in this research pass. Existing payloads (ftpsrv) imply working filesystem stat calls, but free-space reporting specifically must be verified against internal storage and USB mounts (which may report space differently). |

**Bottom line for Milestone 0:** the controller/UI/rendering path (SDL2) is
solid and has working precedent. The network+TLS+JSON+ZIP path — the entire
core of this application — is the largest unverified risk and must be proven
with a minimal spike (Milestone 1/2) before deep investment in the download
manager.

---

## 2. Reference projects (structural/UI reference)

All from the `ps5-payload-dev` organization unless noted:

- **`ps5-payload-dev/websrv`** — web server for jailbroken PS5s; reference for
  BSD socket networking, threading model, and general payload lifecycle.
- **`ps5-payload-dev/ftpsrv`** — FTP server; reference for file I/O patterns
  and storage path handling on PS5/USB mounts.
- **`ps5-payload-dev/shsrv`**, **`gdbsrv`**, **`elfldr`** — smaller reference
  payloads for process/loader lifecycle, not directly relevant to UI but
  useful for confirming toolchain conventions (`Makefile`/`CMakeLists.txt`
  patterns, `PS5_PAYLOAD_SDK` env var usage).
- **`ps5-payload-dev/SDL`** — the SDL2 port itself; primary reference for
  controller input and rendering.
- **`Rufidj/Nativehbl`** — native PS5 homebrew launcher built with SDL2 and
  ps5-payload-sdk. Best available **UI/structural reference** for a
  controller-navigable homebrew front-end (grid/list of items, focus
  handling), since it is one of the only community launcher UIs built on
  this exact toolchain.

No maintained open-source PS5 homebrew app was found that already does
authenticated HTTPS downloads with progress UI — this application will be
breaking new ground on the network/download side, reinforcing the need for
an early spike rather than assuming the happy path.

---

## 3. RomM API

RomM (target: 5.1+) exposes a REST API documented via OpenAPI:

- Swagger UI: `http(s)://<romm-host>/api/docs`
- ReDoc: `http(s)://<romm-host>/api/redoc`
- Raw spec: `http(s)://<romm-host>/openapi.json`

Docs site (`docs.romm.app`) and `deepwiki.com` were **not reachable from this
research environment** (blocked by network egress policy), so endpoint
details below are drawn from GitHub search results (PR descriptions, wiki)
rather than the full spec. **Action item for Milestone 2**: pull
`openapi.json` directly from a real RomM instance and diff it against this
document before writing the API client.

### Authentication

- RomM supports multiple auth modes: session cookies, HTTP Basic, OAuth2
  Password Bearer, and **Client API Tokens**.
- Client API Tokens are the correct mechanism for this app (matches the
  task's stated requirement): format `rmm_<token>`, sent as
  `Authorization: Bearer rmm_<token>`.
- Tokens are scoped (a subset of the owning user's permissions, chosen at
  creation) and hashed server-side; plaintext is never stored by RomM
  itself. Up to 25 tokens per user.
- Implication for us: we must never log the full `rmm_<token>` value
  (matches the task's security requirements) — only a redacted form (e.g.
  first/last 4 chars) in diagnostics.

### Core endpoints identified (UNVERIFIED against a live spec — confirm in Milestone 2)

| Purpose | Endpoint (best available evidence) | Notes |
|---|---|---|
| Platform discovery | `GET /api/platforms` (RomM convention) | Used to discover the PS5 platform ID before filtering ROMs |
| List/search/sort ROMs | `GET /api/roms` | Supports pagination (`with_total` param confirmed via changelog PRs), filtering/search. Exact query-param names for search/sort need confirming against a live spec. |
| ROM metadata detail | `GET /api/roms/{id}` (RomM convention) | Individual game metadata |
| Cover artwork | Served as media asset URLs on the ROM object (`box2d`/cover fields) | Exact path pattern to confirm against live spec |
| Single-file download | `GET /api/roms/{id}/content/{filename}` (RomM convention, "get a RomFile by ID and download that file by passing the ID and a filename") | Needs confirmation of exact path shape |
| Bulk / multi-ROM / collection download (ZIP) | `GET /api/roms/download?rom_ids=...` (or `collection_id=`, `virtual_collection_id=`, `smart_collection_id=` — exactly one of these) | Confirmed via RomM PR history; streams a server-built ZIP |
| Folder-based (multi-file) single game download | Same `mod_zip`-backed streaming path as bulk download | RomM's nginx frontend uses `mod_zip` (see §4) for any multi-file ZIP response, whether it's one folder-based game or a whole collection |

Since exact param/path names could not be independently verified against a
live OpenAPI spec in this pass, **the API client module must be built
against a real `openapi.json` pull in Milestone 2**, not against this table
alone.

---

## 4. Folder-download / ZIP streaming behavior

This is the most consequential finding of Milestone 0.

- RomM's backend does **not** pre-build ZIP files on disk. The bundled
  **nginx** front end is compiled with **`mod_zip`**
  (https://github.com/evanmiller/mod_zip), which streams a ZIP archive to
  the client by having the RomM backend return a small internal response
  listing the files to include (path/size/CRC32/name), and nginx assembles
  the ZIP framing on the fly, streaming file bytes directly from disk.
  RomM's own docs describe this as: "streams a zip archive over HTTP without
  ever materialising it on disk," so "the browser sees a zip download start
  immediately... regardless of folder size."
- Implication: entries are served as **stored (uncompressed)** ZIP members
  with **known sizes and CRC32 up front**, not deflate-compressed and not
  using a trailing data descriptor. This is favorable for our use case:
    - A ZIP central directory is not required before extraction can begin —
      each local file header carries the entry's real size and CRC because
      mod_zip knows them in advance. This means **sequential, incremental
      extraction while streaming is architecturally plausible**: we can
      parse each local file header as it arrives and stream its bytes
      straight to the destination file without buffering the whole
      response or waiting for the central directory at the end.
    - This lets us avoid needing ~2x the game's size on disk (one copy for
      the downloaded ZIP, one for the extracted folder) — a stated MVP goal.
  - **UNVERIFIED**: this must be confirmed against real HTTP responses from
    a live RomM server (byte-level inspection of local file header flags,
    specifically confirming the "general purpose bit flag 3" data-descriptor
    bit is NOT set, and that sizes in local headers are non-zero/accurate)
    before we build extraction logic around this assumption. If any RomM
    deployment falls back to a non-`mod_zip` code path (e.g. `mod_zip`
    unavailable in some Docker builds), the safe assumption above may not
    hold, and we must detect this and fall back to buffering.

### HTTP Range / resume

- `mod_zip` explicitly supports `Range`/`If-Range` for **resuming** a
  download, **but only when the CRC32 of every included file is already
  known to nginx** at request time — RomM's own module description says:
  "If you don't know the CRC-32, mod_zip will disable support for the Range
  header." Since RomM computes checksums when scanning the library and
  stores them, this is expected to be available in most cases, but:
  - **UNVERIFIED**: whether RomM always supplies CRC32 to `mod_zip` for
    every scanned file (e.g. freshly added files pending a rescan, or files
    added via unusual import paths might lack a computed checksum).
  - Practical rule for our download manager: **attempt Range-resume, but
    detect and gracefully handle a server that refuses/ignores the Range
    header** (falls back to restart) rather than assuming resume always
    works for folder-based downloads. This satisfies the task requirement:
    "Never pretend a folder transfer can resume when it must restart."
- For **single-file downloads** (the `.ffpkg`/`.exfat`/`.ffpfs`/`.ffpfsc`
  case, or a single ROM file), standard HTTP Range resume against a static
  file response is the normal case and considered reliable, standard
  behavior — no RomM-specific caveat found.

### Safe incremental extraction — summary risk statement

Streaming incremental extraction of RomM's folder-ZIP without a second full
copy on disk is **architecturally plausible and worth building toward**, but
is **not yet proven**. It depends on: (a) `mod_zip` always emitting
non-deflated, size-known-up-front entries (strong evidence, not yet
byte-verified), and (b) our extractor being strict about validating each
entry (size, path safety) before trusting it, per the filesystem-safety
requirements. If real-world testing (Milestone 4) finds cases that break
this assumption, the documented fallback is: buffer the ZIP to a temporary
file first, extract from that, and accept the 2x-space requirement with a
clear warning to the user before starting — never silently corrupt a
partial extraction.

---

## 5. Firmware / ecosystem context

- Target: jailbroken PS5, firmware 10.60 initially, with **KStuff Lite** and
  **ShadowMountPlus** present as separate, user-installed components. This
  app does not bundle, modify, or depend on their internals beyond writing
  output into their documented scan paths and, where a safe documented
  rescan mechanism exists, invoking it.
- This app targets **etaHEN**-class userland jailbreak environments
  (consistent with the `/data/etaHEN/games` destination paths specified in
  the task), which aligns with the `ps5-payload-dev` toolchain family used
  by the reference apps above.

---

## 6. Open risks carried into Milestone 1/2

1. **Network+TLS stack on PS5 is unproven.** No confirmed working port of
   libcurl + TLS backend was found for `ps5-payload-dev/sdk` in this
   research pass. This is the top risk for the whole project and should be
   the first hardware spike: get a single authenticated HTTPS GET working
   on real hardware before investing in the full download manager.
2. **RomM API endpoint shapes are best-effort, not verified against a live
   spec.** Must pull real `openapi.json` from a running RomM 5.1+ instance
   before writing the API client (Milestone 2).
3. **`mod_zip` stream structure (no compression, sizes known up front) is
   inferred from documentation, not byte-verified.** Must confirm with a
   real folder-game download before committing to zero-buffer incremental
   extraction.
4. **Range/resume support for folder downloads is conditional** on RomM
   having CRC32 available for every file; must be treated as "try, verify,
   fall back to restart" rather than guaranteed.
5. **Large-file (>100 GB) behavior is unverified** — both PS5 syscall/libc
   large-file support and target filesystem (exFAT USB vs internal) limits
   need real-hardware testing.
6. **Free-space reporting API on PS5 is unverified.**
7. **Image decoding (PNG/JPEG) path is unverified** — SDL_image or an
   equivalent has not been confirmed to build against the PS5 sysroot.
8. **`docs.romm.app` and `deepwiki.com` were unreachable from this research
   environment** (network egress policy blocked them); findings from these
   sources were reconstructed from search-result snippets and GitHub PR
   descriptions, which is a weaker source than the primary docs. Re-verify
   directly against docs.romm.app once that's reachable, or against a real
   deployed instance's `/api/docs`.

None of the above are blocking for Milestone 1 (project foundation, mock UI,
DualSense navigation), but items 1–3 must be resolved with real evidence
before Milestone 2 (RomM connection) is considered anything but experimental.

---

## 7. Recommendation

Proceed to **Milestone 1** (clean project structure, reproducible build,
mock-data UI, DualSense navigation) using `ps5-payload-dev/sdk` +
`ps5-payload-dev/SDL`. Before starting **Milestone 2**, run a narrowly-scoped
hardware spike whose only goal is: authenticate to a real RomM instance over
HTTPS with a Client API Token and fetch `/openapi.json`, from an on-device
payload. That spike will resolve risks #1 and #2 above and should gate
whether the rest of the architecture in this document holds.

Do not begin full download-manager implementation until that spike succeeds.
