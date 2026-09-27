# Faster shared-model transfers

Release: `2.1.0-actors-preview.4`, branch `claude/mmd-importer-performance-87a36d`. Transfer protocol 2 requires matching updated addons/native modules on the server and clients. Native modules are installed separately, never transferred with models.

## Changes

- Replace the 32 KiB stop-and-wait loop with 48 KiB chunks and a 144 KiB sliding window per peer. Acknowledgements replenish the window while transmission continues. The bound retains headroom for normal gameplay within [Source's reliable-message limits](https://wiki.facepunch.com/gmod/net.Start).
- Cache lossless XPRESS-Huffman packets beside source files. Packing, decompression and verification run outside the engine/physics worker pool using the [Windows Compression API](https://learn.microsoft.com/en-us/windows/win32/cmpapi/using-the-compression-api). Incompressible blocks are stored unchanged. Transfer sidecars are removed with their model or unused texture.
- Rebuild the Source material GMA locally from downloaded textures instead of sending a second representation of those textures. The client must match the server's exact SHA-256. It downloads the archive if reconstruction fails or differs. All raw models, texture bytes, geometry and shader parameters retain their original identities.
- Validate bounded blocks, exact lengths, SHA-256 and archive contents before atomic publication. Progress reports include throughput; cancellation releases staging state. Late packets from canceled transfers cannot cancel a newer request. Already verified files are reused.
- Keep a bounded transfer history and expose `mmdhl.GetTransferDiagnostics(player)` (the player argument applies on the server). Diagnostics include wire/raw/generated/cache bytes, elapsed time and phase. Elapsed time uses the monotonic clock, including time spent inside a frame.

The owned launcher also waits for server readiness before connecting and disconnects clients before stopping them, avoiding startup challenges and ghost player slots. No firewall settings, global network rates or router configuration were changed.

## Observed results

Windows x64 dedicated server plus two real, non-bot clients on the same machine/LAN, at 2560×1440 with the enabled addons. One client had admin publication rights; the second did not. The March 7th fixture was used with the same asset hash as the actor validation.

| Check | Result |
| --- | --- |
| Admin upload to an empty raw-asset cache | 16.26 MB restored from 14.79 MB wire payload in 25.28 s |
| First cold actor package | 141.51 MB available from 14.88 MB wire payload in 27.10 s |
| Automatic Citizen/player/arms/Combine downloads | 29.39 s total, 15.38 MB wire payload |
| Source material archive generated locally | 124.93 MB; exact server hash matched |
| Cached model request | 0.32 s end to end, zero model payload |
| Concurrent re-download of a 5.33 MB texture | Both clients passed; 9.27 s and 8.88 s after retry |
| Full package verification | All 25 files, 143.15 MB including the additional ragdoll carrier, matched SHA-256 |
| Spawn from received/generated files | Native prop_ragdoll, 18 physics objects, 58 exposed bones |

The first cold package used approximately **89.5% less network payload** than its uncompressed files. The upload is mostly already compressed PNG data, so it benefits less. These measurements describe this installation, not WAN bandwidth guarantees. No direct before/after timing ratio is claimed against the earlier four-minute observation.

Raw evidence: ignored `validation/transfer-speed.json`, owned-session RPC captures, `validation/transfer-tests.log`, and `validation/transfer-build.log`. The upload record is `validation/server-server-1790102362790.json`. User models and captures are excluded from release packages.

## Correctness checks

- Native tests: 15/15 passed; the actor/sharing test was rerun after the final native change. Coverage includes multi-block lossless round trips, incompressible data, empty files, invalid sizes, truncated/corrupt packets, trailing bytes and sidecar deletion.
- `tests/test_sharing_transport.py` executes the actual Lua state machine with delayed peers: upload/download, window bounds, cancellation and immediate retry, stale packets, cache reuse, integrity failure, local archive generation and download fallback.
- `tests/test_qol_lua.py` passes the existing syntax, LOD, appearance and corpse-ownership regressions.
- Real clients remained connected during concurrent transfers. One transfer was canceled while data was in flight and immediately retried; both clients subsequently passed complete file-hash verification.
- A received model was spawned and removed using the existing native factory. Existing simulation behavior was not changed by this transfer update.

The physics-performance issue is marked **addressed**, as accepted by the user on 2026-09-22. Existing frame-time reports remain unchanged. The preview is authorized for installation and hands-on testing; previous release packages and the installed-release backup remain available.

## Later changes: request limits and upload verdicts (2026-09-26)

After the second Codex review, a peer can no longer make the server hash files on demand, and every request gets an answer:

- **Requests.** A model or prop request is refused (`share.error.busy`) while that peer already has a transfer, before any file is hashed. A manifest is kept for a minute and dropped when an approval changes, a Workshop install completes, or a listed file's size changes. A new one is computed at most once a second per player and four times a second in all (`share.error.throttled`). Clients ask again after a busy or throttled refusal, one second apart, up to ten times.
- **Catalog.** The catalog request is answered at most every 5 s per player and only sends; it no longer registers actors or rewrites `approved.json`.
- **Offers.** A client declines an offer that does not answer its current request (the late answer to a cancelled one), and the server releases it.
- **Uploads.** The server answers `checking`, then `done` only after it has loaded and approved the upload, or an error the uploader still receives. The uploader waits up to 3 minutes while the server checks. A refused prop upload ends the placement that waited for it.
- **Withdrawals.** Approvals that end (a model forgotten, a Workshop package gone) reach clients through `mmdhl_catalog_forget`, up to 128 ids per message, and leave their libraries.
- **`approved.json`.** Well-formed approvals of a damaged or partial file are kept, the rest dropped, and the original is copied to `approved.invalid.json`.
- **Native side.** Starting a download needs the file's size plus its packet plus 512 MiB of free disk space. A transfer sidecar is reused only after it has once unpacked to its digest in this process; a damaged one is rebuilt.

Tests: `tests/test_sharing_requests.py` (refusals, memoization, throttling, retries, late offers, upload verdicts, withdrawals, `approved.json`) and the native `animated_carriers_and_sharing` sidecar repair check.
