# Research and review

Sources checked 18 October 2026. This skill synthesizes their principles; it does not reproduce their manuals. The following are game UI review criteria, not claims that a particular implementation complies.

- [Xbox Accessibility Guideline 112: UI navigation](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/112): predictable focus, consistent controls and positions, keyboard/digital navigation, and a clear return route. For linear menus consider wrapping; do not force a wrap rule onto spatial grids. Test navigation after layout/scale changes.
- [XAG 114: Context for UI](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/114): labels and help should explain a control's purpose before activation and remain associated with it. Indicate external destinations and changes of context.
- [XAG 101: Text display](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/101): review text readability for the actual platform, viewing distance, scaling and language. Consult the current guideline for platform-specific sizing; do not apply a single desktop pixel size everywhere.
- [XAG 102: Contrast](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/102): standard informative text/elements should reach 4.5:1; large text/elements and inactive text have 3:1 criteria. Measure against the least favorable area of a variable background, including moving gameplay. A translucent black panel is not proof of sufficient contrast.
- [XAG 107: Input](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/107): support digital navigation, single non-simultaneous interactions, and remappable actions. Rebinding alone does not remove difficult hold/repeat requirements.
- [Unity: Navigation events](https://docs.unity.com/en-us/engine/6000.0/manual/uitoolkits/uielements/uie-events/reference/uie-navigation-events): an engine example of separate move/submit/cancel events. Apply the separation of intent to other engines; do not assume Unity APIs exist in them.

## Review a working build

| Area | Exercise | Evidence to retain |
| --- | --- | --- |
| Entry and assets | Open every supported entry point; repeat after restart | Real screen capture, resolved movie/prefab path, absence of missing-asset errors |
| Decision clarity | Read title, selected item, current value, help and action result | Capture with the important state visible |
| Navigation | First/last row, tabs, nested screen, modal, Back/reopen | Actual focus/selection and return behavior |
| Devices | Keyboard alone, mouse alone, controller alone, then switch while open | Each device completes the same task without gameplay leakage |
| Layout | Target resolutions/scales, long labels, scrolled list, empty list | Cropping, contrast and hit targets checked in rendered output |
| Settings | Change, cancel/apply, reopen; restart when persistence is intended | Accepted/current value and storage behavior |
| Lifecycle | Add/remove contribution, unload/reload with a native screen open | No stale callback, dangling string, focus trap or incorrect owner action |

A representative sample can establish a shared primitive; it cannot prove every distinct inventory, map, text field, dialog or slider works. Track coverage by screen and control type.
