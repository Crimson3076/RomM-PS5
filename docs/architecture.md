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

**Milestone 1 update:** the findings below replace the Milestone 0 table,
which was based only on external docs/search results. `docs.romm.app` and
`deepwiki.com` are still unreachable from this environment, so instead we
cloned and read the actual backend source directly:
`https://github.com/rommapp/romm`, commit `2c0fb087959e9a1cc3365e573b77b021490e292d`
(`main` branch, 2026-09-04; `pyproject.toml` reports `version = "0.0.1"`,
i.e. this is unreleased `main`, not a tagged 5.x version — re-check against
whatever tag the user's actual RomM instance runs). This is ground truth for
that commit, not a live-server test — behavior must still be confirmed
against a real running instance before Milestone 2 is considered verified,
but it is far stronger evidence than the external-docs pass.

RomM exposes a REST API documented via OpenAPI, still expected at:
- Swagger UI: `http(s)://<romm-host>/api/docs`
- ReDoc: `http(s)://<romm-host>/api/redoc`
- Raw spec: `http(s)://<romm-host>/openapi.json`

### Authentication — CONFIRMED from source

`backend/handler/auth/hybrid_auth.py` and `backend/handler/auth/base_handler.py`:
- Client API Tokens are generated server-side as `"rmm_" + secrets.token_hex(32)`
  (a `rmm_` prefix followed by 64 hex characters).
- Sent as `Authorization: Bearer rmm_<64-hex-chars>`. The auth middleware
  detects the `rmm_` prefix on the bearer token specifically to route it to
  Client-API-Token verification (as opposed to an OAuth2 JWT bearer token,
  which is handled by a separate branch).
- Confirms the task's requirement directly: we must never log the full
  token value (redact to something like `rmm_ab12...ff90`).

### Platform discovery — CONFIRMED from source

- `GET /api/platforms` (`backend/endpoints/platform.py`) → `list[PlatformSchema]`.
  No platform-specific filter query param; the client fetches all platforms
  and picks the one whose slug is `ps5`.
- The PS5 platform's filesystem/IGDB slug is confirmed as the literal string
  `"ps5"` (`backend/handler/metadata/base_handler.py: PS5 = "ps5"`, used
  consistently across the metadata provider adapters). Use `PlatformSchema.id`
  where `fs_slug == "ps5"` (or `slug == "ps5"`) as the `platform_ids` filter
  value for `/api/roms`.

### Listing / searching / sorting ROMs — CONFIRMED from source

`GET /api/roms` (`backend/endpoints/roms/__init__.py`, `get_roms`), scope
`Scope.ROMS_READ`. Confirmed query parameters relevant to us:
- `platform_ids` (repeatable) — filter to PS5 by platform id from above.
- `search_term` — free-text search.
- `order_by` (string field name, empty = relevance-on-search / name
  otherwise) and `order_dir` (`asc`/`desc`).
- `limit` (1–10000, default 50) and `offset` — real offset pagination
  (`CustomLimitOffsetParams`), matches the task's pagination requirement.
- `with_total`, `with_char_index`, `with_filter_values`, `with_rom_id_index`
  — all `bool`, default `true`; **we should pass all of these as `false`**
  for a PS5-only browser, since we don't need the alphabet-jump index, the
  full filter-value lists (genres/companies/etc — not relevant to a
  single-platform PS5 library), or the full ordered id index that backs
  virtual scroll on RomM's own web UI. This avoids RomM computing
  library-wide sidecar data on every page request.
- `with_files` (bool, default `false`) — set `true` when we need each ROM's
  file list (needed to know file/folder format up front); otherwise the
  response only carries per-ROM aggregate stats.
- Response type `CustomLimitOffsetPage[SimpleRomSchema]`: `items`, `total`,
  `limit`, `offset`, plus the sidecar fields above.
- `SimpleRomSchema`/`RomSchema` (`backend/endpoints/responses/rom.py`)
  fields relevant to the library browser: `fs_name`, `fs_name_no_tags`,
  `fs_extension`, `fs_path`, **`fs_size_bytes`** (see below),
  `platform_display_name`, plus IGDB/Moby/etc metadata ids used for cover
  art lookups.
- `GET /api/roms/{id}` → `DetailedRomSchema` (full metadata detail screen).
  `GET /api/roms/{id}/simple` → lighter `SimpleRomSchema` (no eager user
  saves/states/notes/collections — prefer this for our detail screen unless
  we specifically need those fields).

### `fs_size_bytes` — CONFIRMED, corrects an implicit Milestone 0 assumption

`fs_size_bytes` is a plain `BigInteger` column on the `Rom` model
(`backend/models/rom.py`), populated from the sum of the underlying files'
real on-disk sizes as scanned from the filesystem. **It is the extracted /
uncompressed content size, not the size of any generated ZIP archive.**
- For a single-file ROM (or a single downloadable image format like
  `.ffpkg`), this is expected to equal the download's `Content-Length`
  exactly.
- For a folder-based, multi-file ROM (our PS5 case), the actual ZIP transfer
  will be `fs_size_bytes` plus small ZIP framing overhead (local file header
  + central directory entry per file, roughly 30–90 bytes each) — never
  less than `fs_size_bytes`. Use `fs_size_bytes` for space-check estimates
  and progress-bar totals, but **do not** treat it as a byte-exact expected
  transfer size for validation; use the response's real `Content-Length`
  (see below) for that instead.

### Cover artwork

Not yet independently re-verified against source in this pass (still an
open item); external-docs findings (box2d cover fields on the ROM object)
stand from Milestone 0. Confirm exact media URL fields against a live
`DetailedRomSchema`/`SimpleRomSchema` response in Milestone 2.

### Single-file and folder-based download — CONFIRMED from source, and this materially changes the Milestone 0 picture

Endpoint: `GET /api/roms/{id}/content/{file_name}` (also a `HEAD` variant at
the same path), scope `Scope.ROMS_READ` (`backend/endpoints/roms/__init__.py:
get_rom_content` / `head_rom_content`). `file_name` is only the desired
download/zip display name; RomM selects the actual file(s) to serve from the
ROM's own file list server-side (optionally narrowed with a `file_ids=`
comma-separated query param for multi-part ROMs).

Behavior branches on how many files the ROM has on disk, **and (for the
multi-file case) whether the request carries a `Range` header**:

1. **Single file** (`len(files) == 1`, e.g. a `.ffpkg`/`.exfat` image or a
   ROM that happens to be one file): RomM returns a `FileRedirectResponse`,
   which sets `X-Accel-Redirect` and lets nginx serve the real file directly
   from `/library/<full_path>`. This is **plain nginx static file serving**:
   real `Content-Length`, `Accept-Ranges: bytes`, and full, reliable HTTP
   Range/resume support — no RomM-specific caveats. This is our
   highest-confidence, lowest-risk download path.

2. **Multi-file / folder-based ROM, no `Range` header on the request**
   (the default first request in most clients): RomM returns a custom
   `ZipResponse` (`backend/utils/nginx.py`) with header `X-Archive-Files: zip`
   and a body listing each file as `<crc32> <size_bytes> <encoded_location>
   <filename>` (the `mod_zip` protocol,
   https://github.com/evanmiller/mod_zip). **RomM always sets `crc32` to
   `None` (`-`) here** — the code comment explicitly says the CRC stored in
   the DB is "for the uncompressed content" and is not reused here. Per
   `mod_zip`'s own contract, **omitting the CRC disables Range-header
   support entirely** for this response — this is not a maybe, RomM's code
   guarantees a fresh streamed folder-download can never be resumed.
   `Content-Length` **is** still provided correctly (nginx/mod_zip computes
   it from the given sizes, independent of the CRC), so progress-by-total is
   fine — resume is what's unavailable.
   - Entries are STORED-equivalent (sizes known upfront, no data-descriptor
     dependency), so our incremental-extraction-while-streaming plan from
     Milestone 0 still holds for this path specifically.

3. **Multi-file / folder-based ROM, request carries a `Range` header**:
   RomM does **not** use the live streaming path at all. It calls
   `resolve_cached_zip()` (`backend/utils/zip_cache.py`), which:
   - Computes a deterministic cache key from the file list + mtimes.
   - If a cached ZIP already exists on the server for that key, redirects to
     it immediately.
   - Otherwise **synchronously builds a complete, standard ZIP_STORED
     archive on the RomM server's own disk** (via Python's `zipfile`,
     locked per-namespace against concurrent builds, written to a temp file
     and atomically renamed), under `ZIP_CACHE_PATH`, with a TTL of 48h
     (12h if the resulting zip is over 8 GB).
   - Serves the finished cache file the same way as case 1 — real
     `Content-Length`, `Accept-Ranges: bytes`, full Range/206 support.
   - If the build fails for any reason (e.g. the RomM server's own disk is
     full), `resolve_cached_zip()` returns `None` and RomM **silently falls
     back to case 2** (the non-resumable live stream) even though the
     client asked for a Range — so a 200 with no `Accept-Ranges`/
     `Content-Range` can come back even when we requested a range, and we
     must detect and treat that as "not resumable," not error.
   - A cheap way to check current resumability without paying the build
     cost: the `HEAD` variant, when the ROM is multi-file, returns
     `Content-Length` + `Accept-Ranges: bytes` **only if a cache entry
     already exists**; otherwise it returns bare headers with neither. Our
     download manager can use `HEAD` to decide whether to request a Range
     immediately or expect a first-attempt build delay.

### This corrects the Milestone 0 claim that folder ZIPs are "never materialized on disk"

That is only true for the no-Range/default path (case 2 above). **Folder
downloads are resumable in RomM only by paying a real cost: the RomM
*server* must build and temporarily store a full, second copy of the game
folder as a real ZIP file on its own disk** before any resumable bytes are
sent — the "avoid a second full-size copy" problem isn't eliminated for
folder games, it's moved from the PS5 client's storage to the RomM server's
storage, and only for downloads that request a Range.

**New, higher-priority risk for this project**: for a very large PS5 game
folder (the task's own >100 GB case), forcing the Range/cache-build path to
get resume support means the RomM server must synchronously zip 100+ GB
before responding — this could take minutes, is a real risk of client/proxy
timeouts on the *first* byte, and requires 100+ GB of *free space on the
RomM server itself*, which we have no visibility into or control over from
the PS5 client. Practical policy for our download manager, pending real
testing in Milestone 4:
- Prefer the plain, non-Range streamed download (case 2) for the initial
  attempt of a folder-based game, and accept that **a folder-game download
  interrupted mid-transfer must restart from zero** unless we have separate
  evidence (a `HEAD` check showing an existing cache entry) that a resumable
  cached copy is already sitting on the server.
- Never claim or imply to the user that a folder-game transfer can resume
  by default. This matches the task's explicit requirement.
- Treat "request a Range and hope the server already cached it" as an
  optional, clearly-labeled best-effort path, not the default behavior.

### Bulk / collection download (not used by the MVP, documented for completeness)

`GET /api/roms/download?rom_ids=1,2,3` (or `platform_id=` / `collection_id=`
/ `virtual_collection_id=` / `smart_collection_id=`, exactly one selector) —
same two-path logic as above, capped at `BULK_CACHE_MAX_ROMS = 100` ROMs for
the cache-build path. Not needed for single-game downloads; documented in
case a future "download whole library" feature is considered — which is out
of scope for the MVP.

---

## 4. Folder-download / ZIP streaming behavior — summary

This is the most consequential set of findings from Milestone 0/1 research,
now backed by source rather than external docs. See §3 above for the full,
source-cited detail; this section is the short version for quick reference.

- **Live stream (no Range)**: `mod_zip`, STORED-equivalent entries, sizes
  known upfront (safe for incremental extraction), `Content-Length` present,
  **CRC32 deliberately omitted by RomM → Range/resume never works on this
  path**, confirmed from source (`ZipContentLine(crc32=None, ...)` in
  `backend/endpoints/roms/__init__.py`).
- **Range-requested path**: RomM builds a complete, real ZIP_STORED archive
  **on its own server disk** (not the PS5 client's) via Python's `zipfile`,
  then serves it as a normal static file with full Range support. This is
  the only resumable folder-download path, and it shifts the "double
  storage" cost to the RomM server rather than eliminating it, with a real
  risk of first-byte timeouts on very large folders.
- **Single-file downloads** (including `.ffpkg`/`.exfat`/`.ffpfs`/`.ffpfsc`
  images and any ROM that happens to be one file) always go through plain
  nginx static file serving — reliable `Content-Length` and Range/resume,
  no caveats.

### Safe incremental extraction — summary risk statement

Streaming incremental extraction of a live-streamed folder ZIP (case 2)
without a second full copy on disk remains **architecturally sound and
worth building toward** — entry sizes are always known upfront in both the
live-stream and server-cached-zip cases, so a strict, validating streaming
extractor (checking size, path safety, and rejecting anything unexpected
before trusting an entry) should work for either. This is still **not
byte-verified against a real server response** in this research pass — that
remains a Milestone 2 hardware/integration-test action item. If real-world
testing finds cases that break this assumption, the documented fallback is:
buffer the ZIP to a temporary file first, extract from that, and accept the
2x-space requirement with a clear warning to the user before starting —
never silently corrupt a partial extraction.

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

## 5a. PS5 cross-compilation milestone — SDL corrected, real toolchain verified

This section supersedes the SDL-related claims in §1 above ("Rendering
surface for UI: **Available**" via `ps5-payload-dev/SDL`) with what was
actually found when the toolchain was bootstrapped for real.

- **The SDK was bootstrapped and pinned.** `ps5-payload-dev/sdk` git tag
  `v0.43` was cloned and built from source with `clang-18`/`lld-18`
  (`make DESTDIR=/opt/ps5-payload-sdk install`), and its own
  `samples/hello_world` was built and confirmed to produce a valid PS5 ELF
  against that install. See `docs/building.md` for exact, tested commands.
- **SDL is not part of the SDK, and no sample uses it.** The SDK's own
  `samples/` directory (23 samples as of `v0.43`: `hello_world`,
  `hello_cxx`, `hwinfo`, `notify`, `browser`, `install_app`, etc.) contains
  **no SDL example**, and `ps5-payload-dev/SDL` (the separate SDL2 port
  identified in §1) is not referenced by, bundled with, or built by this
  SDK. §1's "Available" rating for SDL rendering was based on that port's
  existence in the `ps5-payload-dev` GitHub organization, not on it being
  proven to build against this SDK — that gap is now closed by direct
  observation, and the honest answer is: **still unverified, and not
  pursued this milestone.**
- **The real, maintained-in-this-SDK approach for "show something on
  screen" is a system notification toast**, via `sceNotificationSend`
  (`libSceNotification.sprx`) — this is exactly what the SDK's own
  `samples/notify/main.c` (contributed 2025, credited to LightningMods —
  the same author named in this project's own target-environment
  description) does, and what this project's PS5 target
  (`src/ps5/main_ps5.c`) now does too. It is a real, visible, on-screen
  result, just not a custom-rendered UI screen.
- **Controller input is architecturally real but only partially usable
  yet.** `sce_stubs/libScePad.so` in the pinned SDK genuinely exports
  `scePadInit`, `scePadOpen`, `scePadReadState`, `scePadClose`, and 126
  other `scePad*` symbols — confirmed by inspecting the stub library
  directly, not inferred. However, unlike `sceNotificationSend` (whose
  exact signature came from the SDK's own sample source), **none of the
  ScePad function signatures or the `ScePadData` output struct's field
  layout are published anywhere in this SDK.** `main_ps5.c` calls
  `scePadInit`/`scePadOpen`/`scePadClose` (scalar arguments only, following
  the widely-published PS4/PS5 homebrew convention) to prove real linkage
  and obtain a real pad handle, but deliberately does **not** call
  `scePadReadState` — passing a hand-typed, unverified struct to a
  kernel-adjacent call that likely writes into it based on an assumed size
  is a real crash risk this project has no way to check without hardware.
  Reading actual button/stick state is deferred to a future milestone,
  once that struct layout can be sourced from a trustworthy reference and
  confirmed on real hardware.
- **GNM (the PS4/PS5 GPU driver, `libSceGnmDriver.so`) was not
  attempted.** It is present as a real stub in the SDK (confirming raw
  framebuffer/GPU access is *architecturally* possible), but no sample in
  this SDK demonstrates using it, it is known to be complex and largely
  undocumented for homebrew, and attempting it without a working reference
  would risk producing something that looks like real rendering but isn't
  verified — exactly the "fake platform stub" this project's own
  instructions say not to produce. Left as a genuinely open research item
  for whenever a real custom-rendered UI on PS5 is prioritized.
- **Practical consequence for this project's architecture**: the PS5
  target has no UI/rendering module at all right now, by design — see
  `CMakeLists.txt` and `docs/building.md`. `rommps5_ui` (SDL) remains
  strictly host-only. When real on-console UI is prioritized, the two
  live options based on actual findings are (a) invest in verifying
  `ps5-payload-dev/SDL` against this exact SDK from first principles, or
  (b) build a UI out of a sequence of `sceNotificationSend` toasts plus
  whatever `SceSystemService`/`SceImeDialog`-style system UI primitives
  the stub set exposes (`libSceImeDialog.so` is present, for example) —
  neither has been evaluated in depth; this is a decision for a future
  milestone, not one made here.

---

## 6. Open risks carried into Milestone 1/2

1. **Network+TLS stack on PS5 is unproven.** No confirmed working port of
   libcurl + TLS backend was found for `ps5-payload-dev/sdk` in this
   research pass. This is the top risk for the whole project and should be
   the first hardware spike: get a single authenticated HTTPS GET working
   on real hardware before investing in the full download manager.
2. **RomM API endpoint shapes are now source-verified (§3) against
   `rommapp/romm` @ `2c0fb08` (main, 2026-09-04), not just external docs —
   a significant confidence upgrade from Milestone 0.** Still not verified
   against a live running instance or a tagged 5.x release; re-confirm
   `platform`/`roms` query-param names and response shapes against the
   user's actual RomM version's `/openapi.json` before finalizing the API
   client in Milestone 2, since `main` can drift from any given release tag.
3. **`mod_zip` stream structure is now source-confirmed** (§3/§4): STORED,
   sizes known upfront, `Content-Length` present, CRC32 deliberately
   omitted by RomM on the live-stream path. Still not byte-verified against
   a real HTTP response in this pass (no live server available) — do that
   in Milestone 2/4 before finalizing the streaming extractor.
4. **Range/resume support for folder downloads is now precisely
   characterized, and the picture is worse than Milestone 0 assumed**:
   a fresh live-streamed folder download can *never* resume (RomM always
   omits CRC32 on that path), and resume is only possible by triggering
   RomM's server-side cache-build (§3), which materializes a full second
   copy of the game on the *RomM server's* disk and risks first-byte
   timeouts for very large (>100 GB) folders. Our download manager must
   default to "folder downloads restart from zero on interruption" and
   treat Range-triggered resume as an optional, best-effort path — never
   promise resume by default for folder-based games.
5. **Large-file (>100 GB) behavior is unverified** — both PS5 syscall/libc
   large-file support and target filesystem (exFAT USB vs internal) limits
   need real-hardware testing. Now compounded by risk #4: a >100 GB folder
   game requesting Range-resume could force the RomM server to spend
   minutes synchronously building a 100+ GB zip before the first byte
   arrives, which may exceed client/proxy timeouts independent of anything
   on the PS5 side.
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
