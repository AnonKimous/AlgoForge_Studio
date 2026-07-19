# AlgoForge Studio Vision

AlgoForge Studio is intended to be the underlying infrastructure for algorithm
runtime and assembly: unified scheduling, composable execution, observable
debugging, and stable integration.

The long-term goal is to lower the barrier for developing, validating, and
iterating complex algorithms. Business code prepares data, while `Agent` and
the scheduling system own runtime execution and integration.

The current `pipeline` validates staged organization through the scheduler.
Future work may extend the wrapper with routing and decision-making so that
multiple algorithm paths can be selected from container state and sampling
results.

Near-term priorities are:

- stabilize the visual tools and memory-management work;
- improve pipeline editing and default descriptor authoring;
- improve SDK integration for external projects;
- keep runtime package contracts and execution preferences explicit.

The project is early-stage. Compatibility and reproducibility take priority
over preserving every experimental capability, so changes should keep the
`sdk -> agentmanager -> agent -> algomanager -> runtimesys`
chain explicit and validate representative algorithms through the runner.
