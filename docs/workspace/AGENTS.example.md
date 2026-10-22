# Example instructions for an independent mod

- Read the toolkit AGENTS.md, docs/MODDING.md, docs/RUST_SDK.md and current API.
- Use PowerShell; source `bb-modding/src/scripts/dev-env.ps1` when needed.
- Depend on canonical `bb-modding/sdk/rust/bbmod`; enable `local-catalogs` only
  when generated private lookups are needed.
- Keep sources in this independent project. Use `bb.ps1 build-mod PROJECT --test`
  from the toolkit to build separately. Deployment requires an explicit request.
- Preserve game inputs, saves, settings and persistent content allocation IDs.
- Use host-owned hooks, workers, callbacks and resources; test unload/reload.
- Check backend capabilities; SDK declarations do not prove runtime support.
- Record synthetic, build, title-screen and gameplay checks separately.
- Keep recovered game code, private schemas and generated references out of public source.
