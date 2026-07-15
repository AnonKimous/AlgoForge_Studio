# Compatibility probe

This package is an opaque third-party-style algorithm adapter.

Its plugin owns the runtime state and exposes only `IAlgorithmCompatibilityExecutor`.
The mainline does not read or write an algorithm container for this execution path.
