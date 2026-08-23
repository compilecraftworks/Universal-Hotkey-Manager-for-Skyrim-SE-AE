# Architecture

UHI separates detection from presentation so every detector can be tested outside Skyrim.

1. **Scanners** parse persistent configuration such as INI, JSON and TOML.
2. **Observers** capture runtime registrations and input activity inside Skyrim.
3. **Registry** normalizes records and identifies exact-key conflicts.
4. **Adapters** attach product-specific meaning to otherwise generic settings.
5. **Reporters/UI** expose the same registry as JSON, HTML and an in-game view.

## Trust model

- `Confirmed`: the owner and action are explicitly described by a registration or supported configuration field.
- `Inferred`: the key is real but owner or action is inferred from context.
- `Candidate`: text resembles a key setting and requires confirmation.

Unknowns remain visible. They are never silently discarded.
