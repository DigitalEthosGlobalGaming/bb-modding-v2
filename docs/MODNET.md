# ModNet (multiplayer mods)

Scopes (`mod.toml` `[multiplayer] scope`):

* `client`: local only; reloads freely.
* `session`: every peer must run the same `id` + `protocol`.
* `host-authoritative`: the room owner runs the logic, peers mirror it.

Handshake: each side shares a hash of its session/host-authoritative mod set
(`modhost_modset_hash`) in the room; on join the host compares `id` + `protocol` per mod and
raises `BBM_EV_MODSET_MISMATCH` with a readable diff. Session mods hot reload only outside a
session, or inside a dev session (`bbm_session_info.dev_session`) with the same `protocol`.

Frames: magic `BBMD`, version, channel id (hash of mod id + name), sequence, flags
(reliable/ordered), payload up to 1,200 bytes. Budget: 16 KB/s per peer across all mods
(token bucket); over budget returns `BBM_ERR_BUDGET` and drops. Reliable traffic uses shadNet
room messages (rate limited by the server; to be confirmed with operators); unreliable uses
the direct UDP path.
