# Architecture Decision Records

Architecture Decision Records capture decisions that affect ownership, module boundaries, performance, platform strategy, or future extensibility.

## Current decisions

- [ADR-001 — Authoritative Live Process](ADR-001-authoritative-process.md)
- [ADR-002 — Persistent Simulation Representation](ADR-002-simulation-representation.md)
- [ADR-003 — Renderer Independence](ADR-003-renderer-independence.md)
- [ADR-004 — Thread Ownership and XPLM Isolation](ADR-004-thread-ownership.md)
- [ADR-005 — Weather Authority and Provenance](ADR-005-weather-authority.md)
- [ADR-006 — Platform and Graphics Strategy](ADR-006-platform-strategy.md)
- [ADR-007 — Version 1 Scope and Proof Strategy](ADR-007-version-one-scope.md)
- [ADR-008 — ACF-Derived Aircraft Geometry Authority](ADR-008-acf-geometry-authority.md)

## ADR policy

Create or amend an ADR before implementing a change that:

- Moves authoritative state between processes or modules.
- Changes thread ownership or permits new XPLM access.
- Couples simulation to a renderer or platform API.
- Replaces the persistent simulation representation.
- Changes weather authority or provenance rules.
- Changes aircraft-geometry authority or override precedence.
- Adds a mandatory graphics technology.
- Expands Version 1 scope beyond the charter.
- Changes compatibility or fallback strategy.

## Status values

- **Proposed** — under review.
- **Accepted** — active design rule.
- **Superseded** — replaced by a newer ADR.
- **Deprecated** — retained for history but no longer recommended.
- **Rejected** — evaluated and not adopted.

An ADR is immutable after acceptance except for status, links, and minor clarifications. A materially different decision requires a new ADR that supersedes the old one.
