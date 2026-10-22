# Automatic script references

Setup/extract automatically runs `src/scripts/annotate_scripts.py` after decompilation
and catalog generation. This uses Python only: no model, API key or external AI service.
To process an existing reference export without extracting again:

```powershell
.\bb.ps1 annotate
```

Output is private, ignored `game-unpacked/annotated/`. Start with `search.tsv` to find
scripts by original path, symbol-derived label, functions, registered goal IDs, constants
and selected numeric API arguments. `index.json` contains full records. Each annotated
Lua/HKS file has an adjacent `.json` with original source line numbers, calls, constants,
goal registrations, think-row IDs, duplicate-content paths and definition candidates.
The script body at its original relative path is byte-preserved after an added UTF-8
comment header. A separate `readable/` tree contains copies with selected decompiler
locals and parameters renamed. These are research references, not mod build inputs;
headers change line numbering in both trees. Metadata lines refer to the original body.

For example, a file named `010100_logic.lua` may expose a named caution-logic entry point.
Its label comes from that symbol. Its original archive path remains the primary identity:
the same filename or function can occur in several map archives. No gameplay meaning is
assigned from a filename, model number or numeric ID alone.

## Local variables and parameters

The dependency-free `src/scripts/lua_readability.py` pass parses Lua expressions, blocks,
function parameters and local bindings before suggesting names. Only generated names
matching `fNN_argNN` or `fNN_localNN` are candidates. Examples of supported rules:

| Evidence | Suggested name |
| --- | --- |
| Receiver of recognized AI methods such as `GetEventRequest` | `ai` |
| Receiver of goal methods such as `ClearSubGoal` | `goal` |
| Result of `GetEventRequest()` | `event_request` |
| Result of `GetNumber(7)` | `ai_number_7` |
| Result of `HasSpecialEffectId(TARGET_SELF, 5410)` | `has_special_effect_id_target_self_5410` |
| Numeric for-loop variable | `loop_index` |

Reviewed 1.09 helper contracts also supply roles from call arguments:
`Common_Clear_Param` identifies action-weight, action-handler and action-parameter tables;
`Common_Battle_Activate` identifies AI, goal, those tables and the after-action callback;
`REGIST_FUNC` identifies AI, goal and callback arguments and returns a registered closure.
For example, empty tables passed into these helpers become `action_weights`,
`action_handlers` and `action_parameters`, rather than receiving a generic table name.
The spelling in result labels for `GetRandam_Int`/`GetRandam_Float` becomes `random_*`;
the original API spelling is unchanged.

These contracts were reviewed against the extracted `aicommon` helper implementations;
they are authored naming rules, not automatic discovery of arbitrary function semantics.
They assume the baseline helpers. Local shadows, same-file global replacements, wrong
argument counts and conflicting role hints suppress these rules. Cross-file/runtime
helper replacements cannot be resolved statically. Each inferred role records its helper,
argument position and call-site source line in the rename evidence.

Getter/predicate result names derive from method spelling and direct numeric/constant
arguments. They describe API usage rather than prove a value's type or gameplay role.
Existing descriptive names, globals, field/method names and game entry points are
preserved. Reassigned bindings, ambiguous multi-return initializers, unknown values and
conflicting AI/goal receiver roles are left alone. New names are unique across the entire
source file; numeric suffixes avoid capturing existing locals or globals.

Resolution handles shadowing, separate branches, loop locals, repeat/until conditions,
local initializers, recursive local functions and captured outer locals. It does not
perform general data-flow/type inference or infer meanings for arbitrary numeric values.
Unsupported syntax makes the whole file's readable body fall back to the original,
with `readability.status = unsupported` and a reason. It does not block the other scripts.

Each script's JSON includes `readable_path` and a `readability` record: original/new names,
binding kind, declaration line, occurrence count, rule evidence and reversible edits.
Edit offsets are zero-based Python character offsets in the original decoded UTF-8
body, not byte offsets or offsets into the header-prefixed file. `search.tsv` also indexes
the new variable names and readable-copy paths.

Before accepting edits, the pass reparses the rewritten body and compares tokens with
local identifiers normalized to binding IDs. This checks that bindings, global/field
names, operators and literal tokens remain the same. Comments, strings, whitespace and
line endings are preserved by span-only substitutions. This is lexical alpha-equivalence,
not a proof of runtime equivalence: debugging/reflection can observe local variable names.
No game code is executed, recompiled or deployed by the annotation pipeline.

## Optional placement associations

If you have a baseline MSB Enemy placement export (the character-observer project's
`data/map-placements.json` format), supply it explicitly:

```powershell
.\bb.ps1 annotate --placements 'D:\MyObserver\data\map-placements.json'
```

The exporter is a separate workflow; annotation does not launch Smithbox or generate map
placements. Every listed MSB source must exist in `game-unpacked/raw` and match its hash.
LUAINFO registrations are joined to named functions within the same archive. Goal IDs
are joined to the checked 1.09 NpcThinkParam layout, retaining duplicate-row alternatives.
Optional placements supply map, character model, NPC parameter ID and think ID. These are
baseline associations, not live actor bindings. Common goals may have map-specific
overrides; associations retain the originating archive and must not be read as proof of
execution. Missing inputs are reported rather than replaced with guessed names.
Character action scripts named `cNNNN.hks` are also associated with placements by model ID.

## Evidence and limits

- A lexer excludes comments and strings when extracting named function declarations,
  parenthesized call occurrences and uppercase constants. It does not validate Lua or
  resolve scope, dynamic dispatch, aliases, table-assigned functions or string-call syntax.
- Call definition candidates preserve all exact global-name matches across the export;
  they do not prove runtime call targets. Local named functions are listed but not used
  as global candidates.
- Selected literal API arguments identify special-effect IDs, AI number slots and timer
  slots. Their gameplay meanings remain null/unresolved. This is not a complete list of
  every numeric reference, and variable-valued arguments are not evaluated.
- Labels split existing symbols into readable text. Comments report observed syntax
  and data relationships, not natural-language interpretations of combat behavior.
- Decompile report hashes/statuses are checked when available. Missing/failed/mismatched
  outputs are excluded and produce a nonzero exit status. Without a report, provenance
  is explicitly marked unverified. Successful extraction is not semantic equivalence.

## Reruns and preservation

Analysis is cached by script hash and both annotator/readability implementation hashes. Context sources are
reread so changed registrations or placements update associations. Unchanged output files
are not rewritten. The manifest records output hashes; edited or conflicting unowned
outputs cause an error instead of being overwritten. Removed sources remain on disk but
are excluded from the current index. There is no recursive cleanup or game deployment.

Use a new output folder if a previous output tree was manually edited or interrupted
before its manifest was committed:

```powershell
python src/scripts/annotate_scripts.py --output game-unpacked/annotated-review
```

Do not publish annotated scripts, metadata, indexes or other game-derived output. Keep
them under ignored `game-unpacked/`. Only the generic generator, tests and documentation
belong in public source. Run `python src/scripts/audit_publication.py` before staging.
