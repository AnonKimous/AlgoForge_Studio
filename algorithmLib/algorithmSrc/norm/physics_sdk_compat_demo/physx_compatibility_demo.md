# PhysX compatibility demo

This package is a small but real PhysX rigid-body compatibility example.

The plugin owns a zero-gravity PhysX scene containing a static ground actor and two dynamic box actors. At the beginning of every round, the algorithm generates a random collision point, two random starting positions, and two different velocities aimed at that same point with the same arrival time. This preserves randomized motion while guaranteeing a collision. Each Compatibility execution advances PhysX by one fixed time step, fetches the results, reads both actors' poses and velocities, and writes two body states into the `a1` algorithm array. Contact relative speed drives a small visual squash-and-stretch deformation in the state written for the result-render stage. The round is regenerated every five seconds.

The mainline does not know about PhysX types or physics objects. The `.algo` carries the plugin, shader resources, and the matching PhysX runtime DLLs. This demonstrates that a third-party algorithm library can own its implementation and still expose one executable AlgoForge package.

The demonstration uses one `.algo` package containing the plugin and matching PhysX runtime DLLs. DebugTool's `Debug` and `releaseWithDebugInfo` mount methods select the runtime inspection behavior; they do not represent two algorithm package builds. Set `ALGOFORGE_PHYSX_INCLUDE_DIR` to the PhysX include directory and `ALGOFORGE_PHYSX_RUNTIME_DIR` to the directory containing `PhysXCommon_64.dll`, `PhysXFoundation_64.dll`, and `PhysX_64.dll`, then build it with `python buildProject/Microsoft/build_algorithm.py physics_sdk_compat_demo`. Run it with the Compatibility runner preference.
