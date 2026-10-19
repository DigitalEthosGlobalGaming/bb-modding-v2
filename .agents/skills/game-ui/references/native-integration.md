# Native game UI integration

Use this reference for engine reuse and mod framework adapters.

## Trace the real engine contract

Identify each role separately: logical screen key, asset/movie name, root instance name, dialog factory, list/control bindings, localization/caption descriptors, input context, and ownership. The same string may serve several engine roles. Do not put an arbitrary registry path into an asset-name field unless a resolver supplies the corresponding asset.

Confirm structure offsets, calling convention, stack arguments, hidden return pointers and refcount rules against real call sites. Fingerprints protect against known code mismatches; they do not prove the adapter's semantics. Reusing a stock dialog factory may require its stock movie hierarchy, named controls and initialization steps as well as its vtable.

For a nonresponsive button, inspect evidence in order: input reached control, selection changed, callback ran, factory returned valid screen, asset opened successfully, root became visible, controls bound and focused, action dispatched, feedback rendered. Stop guessing at the first missing link and investigate it.

A cloned selectable slot may still need icon ID/data binding, timeline state, a new placement depth, named child controls and navigation registration. Cloning geometry alone does not prove the new entry has a visible icon.

## Share semantics and own lifetime

Keep screen/action/value definitions in a host-owned registry with stable IDs, namespaced keys, deterministic order, versioning and owner cleanup. Mods contribute intent; a shared adapter owns hooks, native allocation, strings and input. Avoid one competing hook/asset replacement per mod.

Native callbacks must not retain pointers into a DLL that may unload. Copy strings and payloads into host/engine lifetime storage, enqueue numeric action IDs, then revalidate owner, generation and enabled state before execution. An open native view needs a defined snapshot/rebuild policy. Do not silently call a newly loaded action through an old visual row.

Keep authored game assets immutable; generate overlays separately. Test resolver normalization, case, prefixes, nested paths, long names and unload behavior. Validate dependencies such as fonts/textures rather than copying only the top-level movie.

## Integration evidence ladder

1. Registry tests prove declarations and ownership.
2. Synthetic ABI tests prove memory/callback mechanics under their modeled assumptions.
3. Offline asset and executable inspection prove specific names/layouts/call sites.
4. Runtime resolution logs prove the expected assets were requested and found.
5. Rendered native interaction proves visibility, focus, action, feedback and Back.

Use each result only for its scope. In particular, step creation can precede a failed movie load. Do not describe a menu as working based on registration or constructor events.
