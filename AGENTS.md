# bb-modding

Read DEVELOPERS.md, docs/SETUP.md and docs/CONSOLIDATION.md. Use PowerShell on Windows;
PowerShell 7 is preferred and required for map placement export. Work from this root.
Framework source is src/host, SDK is sdk, and active helpers are src/scripts.
The single active runtime checkout is tools/bloodborne_pc, prepared from tools.lock.json
and src/integration/bbport-modhost.patch. Edit canonical source, then reprepare/build;
do not independently edit the materialized host/SDK copies in the runtime checkout.

Separate existing mod projects remain under ../modding/mods and the independent
../modding/bb-modding-adaptive-triggers and ../modding/bb-modding-difficulty repositories. Use .\bb.ps1 build-mod <project> --test.
New personal projects can use ignored mods/. Duplicate projects, legacy tools/references
and live WIP trees were retired. Private Git/source/evidence archives are under
.local/archive/source-history; original conflict snapshots are in .local/consolidation.
Archives are historical recovery material, not an alternate active workflow.

Game and installed runtime are external read-only inputs during setup; paths live in
ignored .local/workspace.json. Never infer a path from the developer's machine.
Game binaries/packages, recovered assets/code, local catalogs, generated compatibility
profiles, downloaded tools, saves/settings and private schemas must not be committed.
Preserve private reference inputs and content item-ID allocations under .local.
Run python src/scripts/audit_publication.py before staging. Review provenance manually;
an audit pass is repository hygiene, not legal clearance. Retain upstream licences.

Use .agents/skills/bb-modding/SKILL.md for setup/development. Build/test separately from
deployment. Do not deploy unless requested; retain the hash-checked rollback backup.
The current pinned bridge supports shared input/text, overlay, reload, overrides and pad
effects, but native services remain profile/capability gated. Record synthetic, build,
title-screen and gameplay validation distinctly. See docs/VALIDATION.md.

For script research, run .\bb.ps1 annotate after decompilation. Start with ignored
game-unpacked/annotated/search.tsv and adjacent evidence JSON. Read
docs/SCRIPT-REFERENCES.md for limitations. Preserve original archive paths/symbols;
labels and static associations are not proof of gameplay meaning or live execution.
The annotated readable/ tree includes scope-checked local-name substitutions; each
script's JSON records reversible edits and evidence. Unsupported syntax keeps original
names. These copies are research references, not deployable scripts.
