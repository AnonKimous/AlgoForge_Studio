# PhysX GPU lance CUDA demo

This package keeps the PhysX scene opaque to the mainline. The CUDA execution
executor owns the PhysX CUDA context, GPU dynamics scene, lance, and solid.
It reports only execution signals and writes its trace to
`testData/physx_lance_cuda_trace.log`.
