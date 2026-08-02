# Tetra Blast / Plane Transfer Probe

This agent-mounted algorithm validates finite blast transfer, a second directional external pressure, and the `lg0 -> -lg1` precision-group admission path for a `TetrahedralCap`:

1. The blast point produces a radial pressure pulse with propagation delay, Gaussian time envelope, exponential distance decay, and face exposure.
2. A uniform directional external pressure is integrated on every exposed boundary face and added to the blast traction, nodal force, moment, and Cauchy stress.
3. Three-point triangle quadrature integrates the combined traction into boundary force and equivalent linear-tetra nodal forces.
4. A temporary full8 subdivision samples the same pressure field at 48 points and compares it with the `lg0` affine reconstruction.
5. Pressure-field, resultant-force, subface-force, and moment errors are normalized by material tolerances.
6. A score above `refine_threshold` emits a `RefineDecision`, creates one `-lg1 TetrahedralCap` precision group, and queues eight active child tetrahedra.
7. The formal group repeats the refined surface integration and verifies child-volume conservation before reporting `precision_group_executed=1`.
8. A volume-average symmetric Cauchy stress is reconstructed, and two requested plane tractions are evaluated as `t = sigma * n`.
9. For the fracture probe, the admitted cell is directly and uniformly refined to fixed `-lg4`: 969 shared nodes and 4096 active tetrahedral leaves.
10. A three-seed weighted dual-graph wavefront builds a T-shaped three-partition near the two requested planes. The secondary plane splits only the positive side of the primary plane.
11. Every interface crossing a partition is committed in one batch. Removing those dual edges must produce exactly three closed fragment surfaces, with two oppositely wound crack triangles per broken interface.

The candidate full8 geometry is temporary until admission succeeds. On success the fracture probe deliberately jumps to fixed `-lg4`, so the parent remains a summary and the 4096 fine tetrahedra are the only physical leaves. This is a Jobs reference implementation rather than a full elastic time-history solver.

The short post-commit preview displacement combines the integrated blast/external-pressure boundary impulse with equal-and-opposite stress-release impulses on every broken interface. Its linear-momentum residual is measured against the integrated external impulse; no independent explosion velocity is added. The current crack paths are interface-conforming, grid-biased approximations of the requested planes, not exact tetrahedron/plane remeshes.

## Output metrics

`blast_plane_metrics` contains:

| Index | Meaning |
| --- | --- |
| 0..2 | Integrated external force XYZ |
| 3 | Equivalent-nodal force residual |
| 4 | Equivalent-nodal moment residual |
| 5 | Clipped cut area |
| 6 | Cut-force magnitude |
| 7 | Tensile normal traction |
| 8 | Shear-traction magnitude |
| 9 | Mixed-mode fracture index |
| 10 | Opposite-side cut-force balance residual |
| 11..12 | Minimum and maximum blast arrival time |
| 13 | Maximum sampled face pressure |
| 14 | Tetrahedron mass |
| 15 | Center-of-mass acceleration magnitude |
| 16 | Normalized pressure-field reconstruction error |
| 17 | Normalized resultant-force error |
| 18 | Normalized subface-force distribution error |
| 19 | Normalized moment error |
| 20 | Maximum refinement score |
| 21 | Refinement threshold |
| 22 | Refinement reason mask |
| 23 | Formal precision-group count |
| 24 | Accepted LG depth: 0 = lg0, 1 = -lg1, 4 = fixed -lg4 |
| 25 | Active precision-leaf count |
| 26 | Precision-group execution flag |
| 27..29 | Parent volume, child-volume sum, volume residual |
| 30..31 | Coarse and fine pressure sample counts |

The `refine_decision_packet` stores the documented source level, target level, full8 mode, score, reason mask, individual errors, group execution state, and volume audit.

`fracture_metrics` contains:

| Index | Meaning |
| --- | --- |
| 0..5 | Fine nodes, tetrahedra, interfaces, boundaries, broken faces, fragments |
| 6 | Committed crack area |
| 7..8 | Fine volume and volume residual |
| 9..10 | Fragment masses |
| 11 | Linear-momentum residual |
| 12 | Preview opening/sliding distance |
| 13..14 | Fragment speeds |
| 15 | Fracture checksum |
| 16 | Non-manifold/open fragment-surface edge count |
| 17 | Fragment mass residual |
| 18 | Grid crack area / sum of the two full clipped-plane areas |
| 19 | Relative momentum residual |
| 20 | Partition regularization pass count |
| 21 | Integrated external-pressure force magnitude |
| 22 | Secondary clipped-plane area |
| 23 | Secondary tensile normal traction |
| 24 | Secondary shear traction |
| 25 | Secondary mixed-mode fracture index |
| 26..27 | Primary and secondary broken-face counts |
| 28 | Integrated external impulse magnitude |
| 29 | Active fracture-direction count |
| 30 | Maximum fragment speed |
| 31 | Grid crack area / sum of the two full clipped-plane areas |

The preview panels show the `lg0` geometry and both requested planes, the combined loaded-face result, and three separated fragments. Primary crack faces are red/orange; secondary crack faces are magenta/cyan.

This probe demonstrates prescribed multi-direction fracture transfer. It does not yet nucleate an arbitrary crack direction or grow a branching crack from a dynamic stress-intensity solve.
