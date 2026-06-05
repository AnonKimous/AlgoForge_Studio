# Capability Modules

This directory groups modules that are intentionally outside the strict
main-trunk layer chain.

## Rules

- Capability modules may aggregate lower-level contracts for domain-specific work.
- Capability modules must not create upward dependencies into strict trunk layers.
- Optional capabilities must still be linked explicitly by any consumer.

## Current Groups

- `agent`: cross-layer agent data object and package hook contracts.
- `algorithm_library`: reserved home for concrete algorithm package capability bundles.
- `sidecar`: optional external-format and adapter capabilities.
