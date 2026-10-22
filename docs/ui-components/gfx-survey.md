# Bloodborne GFX candidate survey

Inspected 22 October 2026 against the configured European GOTY 1.09 game.
This is static asset evidence and an investigation shortlist, not a claim that
these movies accept arbitrary mod data or are implemented SDK components.

## Coverage and evidence

The extracted game contains 128 loose `.gfx` files: 112 distinct byte streams
and 105 distinct basenames. Twenty-one files are font libraries; localized and
trial variants account for other repetition. The survey includes every loose
GFX below the configured game root, but does not recursively unpack containers
looking for additional embedded movies.

The canonical bounded inspector parsed all 128 without reported frame warnings.
A reviewed read-only parser from the user-supplied `bloodborne-gfx-modding.zip`
added named instance trees, text metadata and cross-file indexing. External-image
names agreed between the parsers for all 128. Original SHA-256 hashes were checked
before and after reading and remained unchanged. The supplied skill was read as
research material, not installed or treated as repository instructions.

Private evidence is under `.local/ui-research/gfx-survey-20261022T075934314268Z`:
the manifest records source/tool hashes, per-file results and limitations; the
movies directory contains structural reports and named trees. Recovered game
assets, text and script exports remain private.

## Candidates worth investigating

| Family | Observed structure | Potential SDK use and next question |
| --- | --- | --- |
| `weaponupgrade` | One movie contains category entries, seven weapon-list row instances, status/detail regions, title/subtitle, cost and status bar | Strong integrated reference for the first list/detail Screen. Can the native factory/providers be separated from upgrade-specific operations and driven by mod-owned data? |
| `inventory`, `depository`, `equipment` | Title/subtitle, category/slot groups, player/status regions and repository state; `inventory` has nine category instances | Candidate Screen shell and category navigation. Trace how separate list/detail movies are mounted and how shoulder actions select categories. |
| `itemlist_weapon`, `itemlist_protector`, `itemlist_normalitem`, `itemlist_gem` | Seven repeated row instances per list group; weapon list includes `ScrollBarV` with bar/arrows and row name/icon/count/selection/gray/equipped fields | Bounded selectable list, recycled rows and scroll reveal. Seven authored rows do not prove a generic virtual-list provider already exists. |
| `itemstatus_*`, `itemdetailtext` | Separate item-status/detail movies; detailed weapon variants and a named description region | DetailsPanel and rich item information. Identify text/icon/stat binding and which fields can be omitted without retaining game-specific behavior. |
| `optionsetting`, `channelsearchcondition` | Settings rows contain `Widgets.ComboBox` and `Widgets.DropdownList`; dropdown groups have six item instances | Choice/Dropdown, focused/disabled states and nested input capture. The existing verified command-list adapter uses only a narrower subset. |
| `slider`, `chrmake_multislider`, `colorselect` | Small slider has `Bar`, minimum/maximum labels and text; multi-slider has seven slider roots; color selection has forty palette entries plus a slider region | Slider, grouped adjustment and later ColorPicker. Trace bounds, change/commit/cancel and focus before exposing public controls. |
| `messagebox`, `ingamemessagebox`, `systemdialog` | Message text and two selectable button instances | ConfirmationDialog and modal ownership. Verify return values, capture and restoration of opener focus. |
| `numselect`, `itemquantityselect`, `largenumselect` | Numeric controls; item quantity movie also has name/icon/cost/status regions | Bounded number editor and QuantityDialog. Separate generic value editing from inventory transactions. |
| `detailkeyguide`, `iconhelp` | Thirteen paired text entries in the key guide; fifty icon-help entries | ActionBar, contextual help and device-correct prompts. Identify runtime prompt selection and localization. |
| `fe`, `remotepcfe`, `caption`, `summonmessage` | Main HUD includes player health/stamina/item groups, five condition gauges, three boss rows, five acquisition rows, enemy/remote tags and notices | Passive HUD mounts, bars/status rows and notifications. Research mounting/update lifetime separately from foreground Screen factories. |
| `secondaryshortcut` | Six item slots with icon, count, cursor, gray and empty-item children | QuickSlot/ShortcutStrip reference; input capture is a separate contract. |
| `trial_userguide`, `common_base`, `black`, `fade` | Trial user guide has only a named `Top`; base/fade assets have little existing control structure | Possible minimal loading experiments or backgrounds. They are not ready-made component hosts, and filenames do not prove a reachable or unused retail slot. |

## Important architectural findings

`inventory` is primarily a shell, while the `itemlist_*` and `itemstatus_*`
families expose separate pieces. This is evidence worth pursuing for native
composition, rather than expanding the five-slot settings list into an inventory.
`weaponupgrade` presents several of those pieces together and gives us a useful
comparison for creation order, providers, focus and shared resource lifetime.

Inventory/list movies include `KG_L1` and `KG_R1` external images. This confirms
available prompt assets, not the semantic input route or a callable TabView API.
Similarly, a 1920x1080 stage is also used by small controls: stage dimensions
alone do not prove full-screen coverage or flexible layout.

Preserve named paths, symbols, imports, state-frame numbering and external image
exports when making generated overlays. Cloning art or increasing row placements
does not register native controls, focus or data providers. Positions, clipping,
visibility and animation transforms remain part of the interaction contract.

## Learnings from the supplied bundle

The most useful additions are a readable named-instance tree, a cross-movie
dependency index, and checking an edited movie against its original contract.
These can extend the canonical inspector after their behavior is specified.

Do not adopt the supplied checker as a complete compatibility gate: an in-memory
test changing label frame numbers still passed its contract comparison. Its tree
combines placements across the whole timeline, does not model removals, and its
checker does not compare alpha, transforms, child-frame counts or masks. Those
limitations matter directly to the paging defect already observed in-game.
The canonical inspector retains color transforms and removal records; a future
contract check should build on that evidence and distinguish allowed edits.

Other bundle claims remain hypotheses or sample-specific observations. An ABC
string-pool match is not proof of an executed API call (`Timer` also occurs in a
HUD class/field name). No detected `ExternalInterface` string is not proof that
all behavior lives in C++. The proposed custom ActionScript bridge and trial
screen entry need native integration experiments. Claimed byte-identical JPEXS
round trips and rebuilt DCX gameplay loading were not established by this survey.

## Next experiment

1. Trace the native composition of the inventory shell and weapon list/detail
   movies; compare the integrated weapon-upgrade presentation. Establish title,
   region mounting, factory/provider lifetime and Back before changing assets.
2. Prove a read-only list/detail Screen with deterministic mod-owned entries.
   No upgrade/equip/consume callbacks should be inherited from a gameplay screen.
3. Add category switching and L1/R1 only after the input and focus route is known.
   Retain per-category selection and correct prompts. Then add a native modal.
4. Research `fe` mounting independently for the first passive owned HUD group,
   using the same publication/ownership rules but no implicit input capture.

Related upstream reading: [ERNativeUI native UI research](https://github.com/Flammrock/ERNativeUI/blob/main/docs/research/README.md)
and its [custom-tab case study](https://github.com/Flammrock/ERNativeUI/blob/main/docs/research/case-studies/custom-tabs.md)
provide comparison methods for newer FromSoftware UI. Their Elden Ring layouts,
addresses and ABI are not Bloodborne bindings.
