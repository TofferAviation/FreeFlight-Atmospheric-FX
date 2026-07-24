# Offline Replay Runner Implementation Milestone

Status: **Implemented; Windows CI validation pending**

The first command-line replay harness is now part of `engine/v1`.

Implemented components:

- `.ffar` validation through the existing replay reader;
- geodetic-to-engine-world normalization;
- pause/replay-aware deterministic physics clock;
- local-origin rebase detection;
- low-speed aerodynamic validity filtering;
- dew-point interpolation and diagnostic ice-relative humidity;
- propulsion normalization;
- deterministic output hashing;
- summary and CSV exporters;
- synthetic determinism, pause, replay, rebase, and validity tests;
- dedicated Windows artifact workflow.

This milestone intentionally contains no contrail physics or rendering. It creates the repeatable input and regression boundary those systems will use next.
