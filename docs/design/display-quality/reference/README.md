# Display-quality reference exports

These files freeze the visual inputs at source commit `52d8affadd64dae1edb8a952b84e11d91d301651`. The reviewed prototype file is 81,687 LF-encoded bytes with SHA-256 `c79d6b85f61c7075e10e82668965f65b98c0cb132da3ff09267ae1ab907b4491`.

`prototype-*` files are deterministic browser exports of the committed HTML prototype at the two named CSS-pixel viewports. They document intent only. They are not Android dp goldens and must not be used as pixel-perfect Android acceptance thresholds. `metadata.json` records the viewport, font scale, exact state, scroll position, and SHA-256 for every export.

`renderer-*` files are new screenshots of the unmodified current app on the named `MIT_Phone_API35` AVD. They establish the pre-ownership-change renderer appearance. The AVD is an emulator and is not physical-device evidence. The gameplay capture is intentionally paused for an immutable visible frame, but it was not loaded from a fixed save state; later certification must use the hashed fixtures in the acceptance manifest.

All files with a `.png` suffix have the PNG signature `89 50 4e 47 0d 0a 1a 0a`. The browser capture transport returned JFIF bytes, so those pixels were decoded and re-encoded as PNG without resize or crop; the original transport responses remain ignored local artifacts.

The capture session initially served an automatic CRLF worktree representation (83,653 bytes, SHA-256 `422a2ab83b2b1a2298c8c826e6a47de89ea43e0bef17ecae40018ae804a119c9`). Before commit, the tracked file was restored byte-for-byte to the reviewed LF representation. A same-tab, same-viewport A/B/A load of CRLF/LF/CRLF produced identical semantic DOM snapshots and byte-identical screenshot transport output, so the line-ending transport did not alter the captured state or pixels. `metadata.json` keeps the reviewed-source and capture-transport identities separate.
