# bb-modding

Read README.md and docs/SETUP.md for the portable workflow. Use PowerShell on Windows.
Read docs/CONSOLIDATION.md when continuing local WIP. The complete former development
tree is preserved under .local/work-in-progress/modding; the port still has unresolved
merges. Relative compatibility junctions live under .local/work-in-progress. Do not follow
them during recursive cleanup. Separate mod projects remain outside the toolkit.
The game and installed runtime are external read-only inputs during setup; paths live in
ignored .local/workspace.json. Never infer a path from the developer's machine.

Game binaries/packages, extracted/decompiled assets, local catalogs, generated compatibility
profiles, downloaded tools, saves, settings and private disassembly must not be committed.
Run python src/scripts/audit_publication.py before staging/publishing. An audit pass is a
repository hygiene check, not legal clearance. Preserve upstream licences and attribution.

Use .agents/skills/bb-modding/SKILL.md for setup and mod development. Setup is separate from
deployment. Build and test separately; deployment creates a hash-checked rollback backup.
Do not deploy unless the user requests it. Keep personal work in ignored mods/ projects.

Check runtime capabilities: SDK declarations do not establish backend implementation.
The pinned legacy integration does not provide all newer services. Record synthetic,
build, title-screen and gameplay validation distinctly.
