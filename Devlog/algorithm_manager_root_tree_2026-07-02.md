# algorithmManager root tree

Date: 2026-07-02

## Goal

Make `algorithmManager` the only public entry point for the algorithm layer.
Upper layers should not know the internal `scheduler` or `catalog`
structure. They only call the root build, submit, schedule, and execution
interfaces exposed by `algorithmManager`.

## Public / Internal Shape

```mermaid
graph TD
  ROOT["algorithmManager"]
  ROOT --> API["public build / submit / schedule / execute APIs"]
  ROOT --> OWNED["returned owned algorithm object"]

  ROOT -.internal implementation.-> SCH["scheduler"]
  ROOT -.internal implementation.-> CAT["catalog"]

  SCH --> SCH1["stage analysis"]
  SCH --> SCH2["pack / sync / submit policy"]
  SCH --> SCH3["pipeline runtime state"]
  SCH --> SCH4["tick orchestration"]

  CAT --> CAT1["package location resolution"]
  CAT --> CAT2["manifest / abi / protocol / types"]
  CAT --> CAT3["package load / decompose / plugin load"]
  CAT --> CAT4["default bindings / transfer map"]
  CAT --> CAT5["build algorithm obj"]
```

## Rules

- The only public root for the algorithm layer is `algorithmManager`.
- Build, submit, schedule, and execution helpers are public APIs of
  `algorithmManager`, not child modules.
- `scheduler` and `catalog` stay internal.
- `scheduler` and `catalog` stay independent.
- After execution, `algorithmManager` returns the object structure it owns.
- `AlgorithmObject` ownership is managed below the root API; the root does not
  become a storage layer.

## Migration Note

This document is the target shape for `algorithm_management`. Code that still
reaches into internal subtrees from upper layers should be pulled back to the
root interface first, then the lower-layer implementation can keep using the
internal subtrees as needed.
