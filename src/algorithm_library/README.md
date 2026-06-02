# Algorithm Library

This folder is the home of concrete algorithm packages and ordinary algorithm contracts.

## Responsibilities

- Ordinary solver implementations.
- Compliance descriptor contracts.
- Package-level type aliases for ordinary algorithm management.
- Ordinary packages such as the camera package.

## Notes

- The manager layer stays in `algorithm`.
- Entity-level algorithms are promoted into `orchestration_entity`.
- A single orchestration entity can compose multiple algorithm packages into one ordered pipeline and reuse lower-level compliance descriptors as the source material for that composition.
- Package-scoped aliases live in the final composite compliance descriptor so the same source container name can be remapped per package.
- `AlgorithmMng_ResolveContainerName` is the manager-side helper that applies those aliases during container lookup, and it can resolve names directly from `PhysicsAlgorithmRequest`.
