# Provenance and publication boundary

This is a fresh source tree, with no copied Git history. The selected framework import
comes from DigitalEthosGlobalGaming/bb-modding commit
`ae2b527` (full commit and per-file mapping in IMPORT-MANIFEST.json).
Framework host/tests/scripts retain GPL-2.0-or-later; SDK/examples/templates retain
MIT OR Apache-2.0. COPYING, LICENSE-MIT, LICENSE-APACHE and BB-TOOLS-LICENSE.txt preserve
the supplied notices. See LICENSING.md for the existing linking/licensing caveats.

Local workspace extraction, decompilation and content scripts were adapted from the
developer's authored tooling. Archive/FMG helpers were selectively adapted from
Supermedo/bloodborne_pc `scripts/quit_menu.py` under GPL-2.0-or-later; its menu patches,
game-derived payloads and the Inspector schemas were not imported. The port integration
patch is GPL source from the framework repository. Tool versions/hashes/URLs are recorded
in src/scripts/tools.lock.json. Downloaded tools retain their own licences.

Omitted from source control:

- Packages, game binaries, stock archives, recovered scripts and extracted assets.
- Generated item/NPC/action catalogs, private bytecode and generated compatibility profiles.
- Inspector/Openborne parameter dumps and bulk community tables containing recovered labels.
- Downloaded tool trees/binaries, private disassembly, saves, settings, caches and local paths.
- Personal mod projects, generated overlays, deployment stages and rollback copies.

Compatibility code necessarily contains interface facts such as addresses, field offsets,
IDs and small authored instruction checks. Two existing bulk profile headers generated
from a private ELF were replaced with address/length/hash manifests and local generators.
The original recovered bytes are absent from tracked profile source. This is a conservative
publication boundary; it is not a claim that every modding activity is lawful in every
jurisdiction or that licences for unrelated material have been granted.

`audit_publication.py` checks Git publication candidates, including force-staged ignored
files, against the private-root/extension/generated-file rules. It also rejects oversized
files and common machine-specific paths. Review new source provenance manually: extension
filters cannot discover every copied text snippet or settle redistribution rights.
Do not publish until the audit and that review pass. No remote repository or release is
created by setup.
