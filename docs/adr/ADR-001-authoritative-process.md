# ADR-001 — Authoritative Live Process

Status: **Accepted for Version 1 design**

## Context

The product contains an X-Plane plugin and a standalone companion application. Simulation correctness must not depend on UI availability, network state, or companion lifecycle.

## Decision

The X-Plane plugin owns authoritative live atmospheric simulation state. The companion application is a configuration, diagnostics, profile-management, replay-management, and update surface.

The companion communicates through a versioned protocol. Commands are validated and applied atomically at simulation tick boundaries. Companion disconnection leaves the last valid configuration active.

## Consequences

- The plugin must contain the complete live simulation core.
- The companion cannot be in the frame-critical path.
- Configuration and profile formats require versioning and validation.
- Headless and offline tools may reuse the core library without changing live ownership.
- Closing the companion does not stop effects.

## Rejected alternatives

- Companion-owned live simulation: rejected due to process latency, synchronization, deployment, and failure-mode complexity.
- Shared mutable simulation state between processes: rejected due to ownership ambiguity and corruption risk.