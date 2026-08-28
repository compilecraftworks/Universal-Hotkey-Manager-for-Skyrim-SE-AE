# Architecture

UHM separates discovery, interpretation, normalization, and presentation so the
same evidence can be regression-tested outside Skyrim.

1. **Configuration and archive scanners** discover supported loose and virtual
   settings across the active Data tree. They associate compound keyboard,
   gamepad, and modifier fields structurally instead of matching a mod name.
2. **Papyrus/PEX analysis** follows values through properties, MCM state,
   custom getter helpers, and registration calls so indirect key settings keep
   their action identity.
3. **Native analysis and observers** combine bounded DLL evidence with active
   CommonLib/SKSE input-handler and registration evidence. Static candidates
   without active evidence are not published as usable hotkeys.
4. **Registry and classifiers** normalize key-code spaces, devices, sources,
   owners, actions, categories, and input contexts before evaluating overlaps
   and confirmed conflicts.
5. **Source-aware editors** encode supported changes back to each source's own
   format. Read-only or unverified sources are never rewritten as a fallback.
6. **Native IMenu UI** presents the registry through the device map and sortable
   manager without changing the underlying detection result.

## Trust model

- `Confirmed`: the key and its active source are directly supported by runtime,
  registration, control-map, or verified configuration evidence.
- `Inferred`: a real key value is supported, while owner, action, or context is
  recovered from nearby structural evidence.
- `Candidate`: static text or machine-code evidence resembles an input binding
  but is not sufficient by itself to publish an active hotkey.

Candidates remain available to the analysis pipeline and tests, but the user
interface does not present unresolved static candidates as active bindings.
