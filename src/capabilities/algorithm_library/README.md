# Algorithm Library Capability Bundles

This directory is reserved for concrete algorithm package capability bundles.

## Notes

- It lives under `src/capabilities` because package bundles are not strict
  main-trunk layer hops.
- Manager-facing container-manifest resolution starts here.
- `algorithm_management` resolves manifest names under this directory and
  creates real runtime containers from the matched manifest JSON.
- The same manifest may optionally carry a `reflector` section so the manager
  can build an algorithm reflector from manifest data instead of legacy
  descriptor code.
- Keep runtime agent management in `src/agent_management`.

## Reflector Snippet

```json
{
  "reflector": {
    "average_position": {
      "from": ["vertex_array"],
      "filter": "average_position"
    },
    "vertex": {
      "from": ["vertex"]
    },
    "triangle": {
      "from": ["vertex_a", "vertex_b", "vertex_c"],
      "filter": "triangle"
    }
  }
}
```
