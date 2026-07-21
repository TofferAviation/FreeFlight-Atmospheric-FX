# ADR-004 — Thread Ownership and XPLM Isolation

Status: **Accepted for Version 1 design**

## Context

X-Plane plugin callbacks share the simulator process and XPLM access is not a general-purpose thread-safe API. Blocking or unsafe work can reduce frame rate or destabilize the simulator.

## Decision

All XPLM calls and handles are owned by the X-Plane host thread. The host publishes immutable snapshots to a simulation worker. The worker owns mutable physical state and publishes immutable completed frames. The host never waits for worker completion.

File, network, replay compression, profile I/O, and companion transport run on an I/O worker.

## Consequences

- Host adapter is the sole XPLM boundary.
- Core contracts contain no XPLM handles or pointers.
- Commands and resets cross threads through bounded queues.
- Worker overruns reuse the previous completed frame and trigger quality reduction.
- Development builds require thread assertions.

## Rejected alternatives

- Calling DataRefs directly from effect modules: rejected due to coupling and thread risk.
- A single-threaded engine: rejected because persistent physics and rendering preparation cannot reliably fit the main-thread budget.
- Blocking the host until simulation catches up: rejected because X-Plane responsiveness is the product priority.