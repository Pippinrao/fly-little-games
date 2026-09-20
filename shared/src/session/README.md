# Nearby code map after consolidation

The current product milestone is defined only by
[docs/nearby](../../../docs/nearby/README.md).

The existing `engine/`, `link/`, `ports/`, `recovery/`, `content/` and wire modules
implement parts of the earlier full session design. They are retained with their
tests and history. Their existence does not establish a working product path,
and their entire unfinished feature set is not a prerequisite for the MVP.

The planned `lan_mvp/` coordinator will own the Android-host/Harmony-guest path,
using the existing QUIC C ABI and real runtime directly. It must replace the
active nearby product composition, rather than run beside another session engine
or another core-step loop. No `lan_mvp/` implementation is claimed by this file.

Use the current design's code map before changing this directory. Preserve the
shared/platform boundary; do not add game-specific product branches or copy
test fixtures into production providers.
