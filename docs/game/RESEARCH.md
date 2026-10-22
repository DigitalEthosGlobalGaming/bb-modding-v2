# Investigating an interaction

[Guide index](README.md)

Start with a concrete player action and trace outward. A broad search for “door” or
“note” can produce unrelated animation, AI, text and map references.

## Reference layers

The following archive families are present in the local reference export. Their
presence was checked while writing this guide. The roles below describe research
entry points; they do not establish which files implement an individual example.

| Entry point under `game-unpacked/raw/dvdroot_ps4/` | Investigate |
| --- | --- |
| `map/mapstudio/` (`.msb`) | Spatial placements and associations for the location |
| `script/talk/` (`.talkesdbnd` members, `.esd`) | Dialogue state and interaction logic candidates |
| `event/` (`.emevd`) | Event conditions and state changes associated with a map |
| `msg/` (localized `.fmg` members) | Display text and dialogue text candidates |
| `param/` | Relevant parameter records, such as an item definition |
| `script/` and character action references | AI/action behavior when the investigated feature calls for it |

The [annotation search](../SCRIPT-REFERENCES.md) indexes recovered Lua/HavokScript;
it is not a universal search over dialogue ESD, event bytecode, maps and text. A missing
match there does not establish that a game feature is absent. Extraction does not
make every binary format directly readable or editable; use a tool with explicit
support for the format and version being inspected.

## Work from observation to evidence

1. **Choose one instance.** Name the location, nearby landmark and exact player action.
2. **Record conditions.** Include game version, progression, relevant choices/items,
   online state and any active mods. Protect the original save and assets.
3. **Describe the result.** Capture prompt, response, reward and any visible change.
   Compare repetition, travel and reload only when those cases are actually tested.
4. **Find candidates.** Use spatial context, text fragments and original archive
   paths. Keep alternate matches; localized text and duplicated records can mislead.
5. **Trace relationships.** Follow references between the interaction, condition,
   response and result. Record archive/member identity and IDs in private notes.
6. **Label conclusions.** Distinguish observed behavior, static associations and
   hypotheses. A plausible static path does not prove live execution.
7. **Publish an authored explanation.** Summarize the mechanism in your own words.
   Keep recovered code, extracted text tables, dumps and evidence exports private.

Use a separate overlay and rollback workflow if an investigation eventually needs
a controlled modification. These documentation pages do not require deployment.

## Record for a new example

Use this structure in a focused page; leave unknowns explicitly unresolved.

```text
Pattern and example:
Player-facing behavior:
Design interpretation:
Source or local observation:
Version and preconditions:
Steps and actual result:
Repeat / travel / reload cases checked:
Implementation findings:
Private evidence location (keep machine paths out of public docs):
Unresolved questions and alternate explanations:
```

## Publication and validation

Keep private evidence under ignored `game-unpacked/` or `.local/`; follow
[provenance rules](../PROVENANCE.md). These pages should contain authored explanations
and focused references, rather than recovered scripts or bulk catalogs. Before staging:

```powershell
python src/scripts/audit_publication.py
```

A documentation/link check validates the documentation. Static inspection validates
only the relationships actually inspected. A title-screen launch does not validate
a door conversation, note interaction or quest transition. See
[validation guidance](../VALIDATION.md).
