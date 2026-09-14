# C& Showcase — small programs, visible proof

These examples are designed for demos, talks, README screenshots, and coding-agent loops. They are intentionally small enough to understand in seconds, but each models a bug pattern that appears in real C systems.

The important promise is not that C& magically understands every C program today. The promise is that C& produces one of three honest outcomes:

- **pass** — the implemented verifier subset found no violation and no recognized unsupported ownership semantics;
- **fail** — C& has a concrete ownership/lifetime violation with a stable rule ID and object-state trace;
- **incomplete** — C& refuses to make a safety claim because required ownership semantics are not modeled yet.

That last outcome is a feature. A verifier that says “safe” when it does not understand the program is worse than no verifier.

## One-command demo

After building C&:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build
bash examples/showcase/run.sh ./build/cand
```

Expected story:

```text
Asteroid Arena       unsafe  -> FAIL CAND-T002
Asteroid Arena       fixed   -> PASS
Dungeon Loot         unsafe  -> FAIL CAND-T003
Dungeon Loot         fixed   -> PASS
Snake Tail           unsafe  -> FAIL CAND-T002
Snake Tail           fixed   -> PASS
RPG Inventory Alias           -> INCOMPLETE
Renderer Plugin Boundary      -> INCOMPLETE
```

## 1. Asteroid Arena — a dead boss still awards points

`01-asteroid-arena-unsafe.c` allocates a boss object, destroys it, and then reads its bounty. This is the classic use-after-free hidden inside game-state cleanup.

C& reports **CAND-T002** and returns the object history: allocation -> destruction -> illegal access. A coding agent does not need to guess what happened; it can repair the exact lifetime error.

The fixed program copies the integer bounty while the boss is alive and frees the boss only after the last object access.

## 2. Dungeon Loot — cleanup destroys the chest twice

`02-dungeon-loot-unsafe.c` models a very common generated-code mistake: two cleanup paths were collapsed into one straight-line sequence and both retained a destructor call.

C& reports **CAND-T003** with allocation -> first destruction -> repeated destruction.

The repair is intentionally boring: one owner, one destruction. That is exactly what a verifier should force.

## 3. Snake Tail — score code reads freed segment storage

`03-snake-tail-unsafe.c` uses an allocated integer array as a tiny snake body. Round cleanup frees the body, while score calculation still reads `tail[0]`.

This demonstrates that temporal safety is not only about structs and `->`: indexed memory derived from a dead allocation is also a lifetime problem. P0 reports **CAND-T002** for this direct case.

## 4. RPG Inventory — ownership cannot be guessed from aliases

`04-rpg-inventory-alias-incomplete.c` creates two pointer names for one weapon:

```c
Weapon *weapon = malloc(...);
Weapon *equipped = weapon;
```

Is `equipped` a borrower? A second owner? Was ownership moved? Ordinary C does not say.

P0 therefore returns **incomplete** (`CAND-U001: pointer-alias-initialization`) rather than claiming safety. This is the bridge to the next C& implementation phase: explicit owner/borrow/move relationships and alias propagation.

## 5. Renderer Plugin Boundary — external APIs need contracts

`05-renderer-plugin-boundary-incomplete.c` passes an owned sprite into another API.

From the call alone, a verifier cannot know whether the renderer borrows the pointer during the call, retains it for later, consumes it, or destroys it. P0 returns **incomplete** instead of inventing semantics.

The long-term C& answer is a reviewed contract such as “parameter 0 is borrowed for the duration of the call” or “parameter 0 is retained until unregister.” This is especially important for large existing C ecosystems where rewriting every dependency is unrealistic.

## Why these demos matter for LLM-generated C

A coding model can produce plausible C extremely quickly. Plausible code is not the same thing as lifetime-correct code.

The useful loop is:

```text
prompt / requirement
      |
      v
LLM generates ordinary C
      |
      v
cand check --format=json
      |
  +---+-------------------+
  |                       |
 FAIL                  INCOMPLETE
  |                       |
exact object trace     missing semantics
  |                       |
repair implementation  add/model reviewed contract
  |                       |
  +-----------+-----------+
              |
              v
            PASS
              |
              v
      normal compiler/tests
```

C& should become compelling not because it replaces C or promises impossible omniscience, but because it gives humans and coding agents a deterministic referee for ownership semantics while preserving ordinary C, ordinary ABIs, and ordinary toolchains.

## Demo rule: never overclaim

The current implementation is a P0 temporal-lifecycle feasibility slice. These examples must not be presented as proof of complete C&1 soundness. They demonstrate something more useful at this stage: real violations are caught, repairs are machine-verifiable, and semantics the verifier does not understand fail closed instead of becoming false green checks.
