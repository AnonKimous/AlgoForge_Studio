# PhysX lance compatibility demo

The plugin owns an opaque PhysX scene. It advances a capsule-shaped lance and a static box through `IAlgorithmCompatibilityExecutor`.

The mainline receives only control bits and debug signals. PhysX objects are not decomposed into algorithm containers.
