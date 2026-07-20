# PhysX compatibility demo

This package is a small but real PhysX rigid-body compatibility example.

The plugin owns a zero-gravity PhysX scene containing a static ground actor and two dynamic box actors. At the beginning of every round, the algorithm generates a random collision point, two random starting positions, and two different velocities aimed at that same point with the same arrival time. This preserves randomized motion while guaranteeing a collision. Each Compatibility execution advances PhysX by one fixed time step, fetches the results, reads both actors' poses and velocities, and writes two body states into the `a1` algorithm array. Contact relative speed drives a small visual squash-and-stretch deformation in the state written for the result-render stage. The round is regenerated every five seconds.

The mainline does not know about PhysX types or physics objects. The algorithm build resolves the official PhysX source through CMake, links the CPU SDK statically into the platform plugin module, and packages that module together with shaders, the manifest, and the PhysX license. The `.algo` format therefore remains identical on every platform; only the CMake-selected plugin binary differs (`.dll`, `.so`, or the platform equivalent).

Build through the boot entry point so dependency detection is applied:

```bash
python boot/booterNinjaClang.py physics_sdk_compat_demo --physx auto
```

A local PhysX source tree can be used by setting `ALGOFORGE_PHYSX_ROOT` to the SDK directory containing `include/PxPhysicsAPI.h` and `CMakeLists.txt`. Without that variable, CMake fetches the pinned official PhysX release. Run the package with the `compatibility` execution preference.
