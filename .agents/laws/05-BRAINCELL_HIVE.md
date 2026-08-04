# THE LAW — BrainCube core + nanobot braincells

**Status:** Coding excellence architecture for Grokium.  
**Does not replace** Commander crypto law.

## Statement

1. **BrainCube is the decision core.**  
   Route and fuse decisions (solo vs hive, plan vs explore bias) go through  
   the LHLAM lattice (`lhlam_cube_decide*`). Models do not override the core.

2. **Nanobots are braincells.**  
   Specialized subagent roles (`explore`, `plan`, `implement`, …) are cells.  
   They communicate via `$NANOBOT_HOME/braincells/*.json` and optional peer bus.  
   Cells do not declare Commander authority.

3. **Hive for hard coding work.**  
   Non-trivial coding prompts may fan out: explore + plan cells → core fuse →  
   implement cell. Results loop back to the parent agent for presentation.

4. **Desktop enables hive; embed stays lean.**  
   `NANOBOT_BRAINCELLS=1` (Grokium default). Robots may leave it off.

5. **Submodules are the wire.**  
   `deps/nanobot` and `deps/braincube` are git submodules (like CubalC).

## Creed

**Core decides. Cells investigate and act. Results return. Commander still seals law.**
