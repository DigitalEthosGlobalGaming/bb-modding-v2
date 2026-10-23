# Licensing

| Part | Licence | Why |
|---|---|---|
| `src/host/` | GPL-2.0-or-later | Links into bb-probe.exe, which is GPL-2.0-or-later. |
| `sdk/include/bbmod.h`, `sdk/rust/**`, `docs/examples/**` | MIT OR Apache-2.0 | A mod loaded through the C ABI should be free to choose its own licence. |
| `src/tests/` | GPL-2.0-or-later | Test harness links the host. |
| `src/manager/`, `src/installer/` | MIT | The Mod Manager and the installer are original code built on the MIT/Apache SDK. Use, change and redistribute them freely, with no warranty. |

Open question for upstream: confirm the bbport maintainers accept a plugin exception that
treats mods loaded through `bbmod.h` as separate works. Until then, assume the conservative
reading and ask before distributing closed-source mods.

Full texts are included as `COPYING`, `LICENSE-MIT`, and `LICENSE-APACHE`.
Templates follow the SDK licence. Build/integration scripts and project docs
follow GPL-2.0-or-later unless a file has its own notice. Preserve all notices.

`docs/BB-TOOLS-LICENSE.txt` preserves the MIT notice for player layout evidence.
Inspector and its generated Openborne/data tables are omitted pending
redistribution review; the original local source remains untouched.

The installer embeds the patched bbport runtime (`bb-probe.exe` and its libraries) as its payload. That runtime keeps
its own licences (GPL-2.0-or-later and others); the installer's MIT licence does not apply to it, and its source is the
pinned bbport checkout in `src/scripts/tools.lock.json` plus `src/integration/bbport-modhost.patch`.

Zydis and Zycore use MIT licences; distributions include their sources and
licence texts. The XML test fixture comes from bbport's GPL-2.0-or-later tree
and retains patch author metadata. It is not installed as a gameplay patch.
