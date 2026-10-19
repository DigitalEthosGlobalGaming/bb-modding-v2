> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Validation and evidence

From the workspace root:

```powershell
. .\modding\dev-env.ps1
cargo test --manifest-path .\modding\bloodborne_pc\third_party\bbmod\Cargo.toml -p bbmod
.\modding\mods\hooks-testing-mod\build.ps1 -Test
```

The example build packages two DLLs without deploying. Its Rust test invocation
checks compilation (the sample crates have no unit tests), then its native harness
loads both real DLLs against synthetic player memory. The harness exercises shared
player/equipment observations, typed item-use/attack/inventory payload fanout, same-label widget
routing, cross-mod scale claims, isolated unload
and 20 reloads without duplicate subscribers. It never attaches to a running game.
The item-use/attack payload tests bypass their native producers; the producers have
separate native hook tests and need in-game observation before claiming live validation.
The inventory summary dispatch test likewise exercises subscriber decoding rather
than the inventory resolver; its own synthetic memory tests cover that resolver.

For the shared UI example:

```powershell
.\modding\bloodborne_pc\third_party\bbmod\examples\ui-examples\build.ps1 -Test
```

This runs state-format unit tests and an actual DLL harness with fake widgets,
including another owner in the same slot and 20 release/re-add/reload cycles.

After building Inspector, Adaptive Triggers and UI Examples, run their combined
shared-backend smoke test:

```powershell
.\modding\test-shared-ui-mods.ps1
```

This loads all three actual DLLs with temporary manifests and no Inspector control
directory. The fake backend enables scope IDs and expanding headers, so it tests
the shared UI path rather than legacy panel fallback. It checks six sections in
two windows, five Inspector/Adaptive reloads each, and UI Examples unloading to
leave four sections before loading back to six. Widgets return no actions, no
scans are requested, and pad output is an unsupported dummy backend. This does
not attach to the game or claim visual/gameplay validation.

The host's shared-service test separately exercises both owners, identity changes,
modifier composition, stale expected values, unload/reload and unsupported layouts.
Run the native CTest suite from the configured `out/native-framework` build after
compiling it. Use `ctest -N` to inspect actual available test names rather than
guessing targets. The full workspace verifier is `modding/verify-framework.ps1`.

## In-game acceptance procedure

1. Install the matching built runtime with a rollback copy, then deploy the examples
   with `build.ps1 -Deploy`. Start `modding/Play Modded Bloodborne.cmd` and load a
   character. Both mods should appear in F1's Mods list and in `hooks.testing`.
2. With experiments disabled, change HP or stamina through normal gameplay. Compare
   A and B log entries for the same previous/current snapshots. Counts can differ
   if one mod loaded later; compare the common interval.
3. Unload A, repeat a change, and verify B still receives it. Reload A, then unload B
   and repeat. Confirm remaining UI contribution works and widget actions affect
   the named mod, despite identical button labels.
4. In a safe location, explicitly click an adjustment button. Check the logged
   requested/actual delta and resulting snapshot. These direct adjustments are not
   timed experiments and are not automatically rolled back.
5. Enable A's scale claim, then B's. Test a suitable explicit SDK adjustment and
   compare against the host's documented integer composition. A -1 HP request can
   round to zero with a 50% claim; this is not evidence of a broken hook.
6. Release/unload one owner and verify the other's claim remains. Reload should
   retain the player-change counter but leave that mod's UI-enabled mutation claim
   disabled. An explicit configuration opt-in deliberately reapplies on load.
7. Travel/load another character with experiments inactive. Verify identity changes
   invalidate old snapshots and the service resumes on the new current player.

Game interaction is performed by the user in this workspace. Do not label these
steps passed merely because a DLL builds or the title screen opens. Record actual
checks and logs separately from planned acceptance steps.
