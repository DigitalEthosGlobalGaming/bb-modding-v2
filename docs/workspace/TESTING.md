# Validation and evidence

From the toolkit root:

```powershell
.\src/scripts/test.ps1 -Native
.\bb.ps1 build-mod ../modding/mods/hooks-testing-mod --test
.\bb.ps1 build-mod ../modding/mods/item-examples --test
.\bb.ps1 build-mod ../modding/mods/character-observer --test
```

The toolkit checks Python helpers, the Rust workspace, the native host tests and
publication boundaries. The external hook harness loads both real DLLs against
synthetic player memory, exercises service fanout and performs 20 reloads.
The item diagnostic harness loads its real DLL and compiled content overlay and
performs ten reloads. These checks never attach to a running game and do not
establish native producer or gameplay behavior. Inspector is excluded.

Private asset/reference tests are opt-in: set `BB_GAME_ROOT` to the selected
`uroot` and `BB_OBSERVER_MOD` to the retained observer project before Python tests.
See [VALIDATION.md](../VALIDATION.md) for actual evidence and limits.

## In-game acceptance procedure

1. Install the matching built runtime with a rollback copy, then deploy the examples
   using the current deployment guide. Start the selected runtime and load a
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
