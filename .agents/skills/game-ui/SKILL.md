---
name: game-ui
description: Design, implement, and verify professional game menus, HUDs, inventories, settings, and native mod interfaces. Use for game UI work involving visual hierarchy, interaction, input, or engine integration; not ordinary websites or debugging overlays alone.
---

# Game UI design and integration

Build interfaces that help players make decisions, fit the game's visual language, and work through the complete interaction loop. Use the existing engine and assets when extending a game. A debug panel or a static mockup does not establish a working native interface.

## Establish the player task and the integration seam

Identify the player's decision, information needed, entry points, exit/back behavior, and supported input devices. Inspect adjacent real game screens and their fonts, spacing, framing, icons, sounds, focus treatment and navigation conventions. Reuse their design grammar; avoid turning a game menu into a dense developer dashboard.

For an existing game or mod framework, trace the actual chain: input -> screen/step creation -> movie/prefab/widget resolution -> control binding -> focus -> action -> visible state update -> return. Keep logical screen IDs separate from engine asset names. Verify assets, control names and callback signatures from evidence. A constructor event is not proof that a screen rendered.

Read [native integration](references/native-integration.md) when extending an existing engine, hooking UI, sharing menu services across mods, or diagnosing a visible control that does nothing.

## Shape the interface

- Give each screen a clear title and a useful initial focus. Put the primary task ahead of diagnostics. Group settings by player purpose; put explanatory detail in a stable contextual area rather than appending long prose to every row.
- Use a small, consistent set of spacing and type roles. Align labels, current values and selection markers. Keep the interactive region visually distinct from the game behind it.
- Use icons with labels where their meaning is not established. Cover normal, focused, hovered, pressed, disabled, pending, empty and error states where applicable. Show why an unavailable action cannot run.
- Match the control to the value: toggle for a boolean, bounded slider or stepper for quantities, choice list for enumerations, action button for a command. Avoid long cycles of values without a way to see the alternatives.
- Make changes observable. Show the accepted value, pending operation, failure or next step. State whether settings apply immediately or on Apply/restart; preserve pending edits and Back semantics deliberately.
- Keep large lists usable through scrolling, categories, filtering or search as appropriate. Restore focus and scroll position on return. Stable IDs must survive label changes and list reordering.
- Design for localization, long labels, larger text, display scaling and both desk and television viewing. Inspect the actual presentation at target resolution; engine coordinate sizes are not screen pixel sizes.

Read [research and review](references/research-and-review.md) for source-backed accessibility criteria, source links and a focused review matrix. Treat reference thresholds as review criteria, not an unearned claim of accessibility compliance.

## Make input coherent

Use semantic UI actions (Navigate, Confirm, Back, ChangeTab, Adjust, Scroll) with remappable physical bindings. Keep keyboard, pointer and controller behavior consistent. Do not require pointer emulation for keyboard access.

Maintain an explicit focus owner and modal stack. Directional/Tab navigation should follow visible layout; hidden and inactive controls must not trap focus. Moving the mouse should update hover deliberately without continually stealing keyboard/controller focus. Back closes the top modal or returns one level, restoring the previous selection.

Hit-test transformed, clipped, visible controls in the renderer's coordinate space. Distinguish hover, focus and activation. Scrolling changes the intended panel; a click should not also perform a gameplay attack. Release capture and held inputs on close, focus loss and device changes. Test device switching during an open menu.

For dynamic contributions, define what happens when the focused row is removed, a mod reloads, the list becomes empty, or an action becomes unavailable. Preserve focus by stable ID where possible and choose a nearby valid fallback.

## Implement and prove one vertical slice first

Before migrating many screens, complete entry -> visible content -> navigation -> action -> feedback -> Back -> reopen. Verify that slice in the actual game. Then migrate remaining screens while preserving each screen's useful functions; links to an old debug panel do not count as native migration.

Separate semantic definitions/state from rendering and input adapters so several mods share engine hooks and controls. UI callbacks should enqueue validated actions at the correct engine/host sync point. Keep ownership and stale-action validation across unload/reload.

Use targeted tests for navigation state, stale IDs, coordinate transforms, action delivery and lifetime invariants. Use actual rendered/native interaction evidence for layout, asset binding and controller behavior. A fake engine can prove ABI mechanics but cannot prove the native movie exists or accepts the controls.

Report what actually rendered, which actions and input devices were exercised, and remaining gaps. When a live session is unavailable, preserve a concrete build and verification procedure and mark live requirements incomplete.
