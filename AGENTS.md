# AGENTS.md

This file defines how AI coding agents should operate in this Unreal Engine project.

## Project

- Project root: `/Users/holy/Documents/UnrealProjects/UGCGame`
- Unreal version: `5.7`
- Primary language: `C++`
- Local Unreal Engine install: `/Users/holy/Documents/UE_5.7`
- Unreal MCP repo: `/Users/holy/Documents/unreal-engine-mcp`
- Unreal MCP plugin path: `/Users/holy/Documents/UnrealProjects/UGCGame/Plugins/UnrealMCP`

## Core Mission

Agents should help the project move forward with changes that are:

- correct
- small
- understandable
- testable
- easy to revert

The job is not just to "make it work". The job is to leave the codebase in a state where a human can still reason about it quickly.

## Design Philosophy

This project follows a practical systems-style design philosophy inspired by the engineering values commonly associated with Linus Torvalds:

- prefer simple designs over clever designs
- solve the real problem, not an imagined framework problem
- avoid unnecessary abstraction
- keep the hot path obvious
- make invalid states hard to represent
- if something feels too complicated, first ask whether the structure is wrong
- do not pile on configuration when a direct data flow or clear function boundary would do
- do not add a new layer unless it removes more complexity than it creates

In practice, this means:

- prefer straightforward functions over deep indirection
- prefer explicit data movement over hidden magic
- prefer removing special cases over documenting special cases
- prefer stable conventions over one-off patterns
- prefer local reasoning over global guesswork

## AI Engineering Rules

Agents must optimize for work that is reliable under repeated AI execution, not just for one lucky edit.

### General Rules

- inspect before changing
- state assumptions in the final summary when they affect behavior
- keep diffs focused on the user request
- avoid incidental refactors unless they are required to make the change safe
- do not overwrite or revert user work unless explicitly asked
- when unsure, choose the more conservative change

### Change Size

- prefer one focused change per commit
- if a task requires multiple concerns, separate them conceptually and implement in the smallest safe sequence
- if a file is already large or complex, avoid making it more abstract unless the simplification is clear and immediate

### Readability

- code should be readable without needing a long explanation
- names should explain purpose, not implementation trivia
- comments should explain non-obvious intent, not narrate obvious syntax
- avoid introducing patterns that only make sense to AI tools

### Safety

- preserve behavior unless the task explicitly asks for a behavior change
- if changing behavior, keep the blast radius narrow
- do not make destructive content changes in the level or content folders without clear intent
- prefer compile or targeted validation after code, config, or Blueprint-affecting work

## Architecture Preferences

Use these preferences when designing or modifying systems in this repository.

### Prefer

- data-oriented flow over inheritance-heavy indirection
- explicit ownership of state
- narrow APIs with clear inputs and outputs
- composition where it simplifies reasoning
- helper functions with one purpose
- code paths that fail early and clearly

### Avoid

- speculative abstractions
- "manager of managers" structures without a concrete need
- hidden side effects across distant systems
- large god classes
- broad utility files with unrelated responsibilities
- boolean flag explosions that represent many modes in one function

## Unreal-Specific Guidance

### Code Vs Editor

Use normal code edits for:

- C++ gameplay logic
- module setup
- config files
- build fixes
- project structure
- source-controlled plugin code

Use Unreal MCP for:

- level inspection
- actor placement
- transform changes
- Blueprint graph creation or modification
- editor-side asset inspection
- scene or world-building operations

### C++ Style

- prefer small classes with obvious ownership
- keep headers lean
- minimize includes in headers when forward declarations are sufficient
- avoid coupling unrelated systems through shared utility headers
- prefer explicit types and clear control flow over compact but hard-to-scan code
- keep Unreal reflection macros minimal and intentional

### Unreal Project Boundaries

- project plugins belong under `/Plugins`
- source code belongs under `/Source`
- generated folders should stay out of version control unless there is a strong reason:
  `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`
- treat engine-level changes as high risk and avoid them unless required

### Config Files

- `Default*.ini` files are project configuration and usually belong in version control
- `Saved/Config/*` files are local/generated and usually should not be committed
- treat `/Config/DefaultEngine.ini` as sensitive because changes often affect the whole team
- when editing config, preserve formatting and avoid unrelated key churn

## Workflow

### Default Workflow

1. Inspect the relevant code, config, asset path, or editor state.
2. Identify the smallest valid change.
3. Implement only that change.
4. Validate with the lightest meaningful check.
5. Summarize what changed, what was validated, and any remaining risks.

### Validation Expectations

Prefer one of the following after meaningful changes:

- Unreal build for code or plugin changes
- MCP re-inspection for editor changes
- Blueprint compile when modifying Blueprint content
- targeted runtime sanity checks when available

Use this command to build the editor target:

```bash
"/Users/holy/Documents/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealEditor Mac Development \
  -Project="/Users/holy/Documents/UnrealProjects/UGCGame/UGCGame.uproject" \
  -WaitMutex
```

## Unreal MCP

This project is wired to use `flopperam/unreal-engine-mcp`.

### Preconditions

Before using Unreal MCP:

1. Open `/Users/holy/Documents/UnrealProjects/UGCGame/UGCGame.uproject` in Unreal Editor.
2. Ensure the `UnrealMCP` plugin is enabled.
3. Let the editor finish loading the project and current map.

### Start The MCP Server

```bash
/Users/holy/.local/bin/uv --directory /Users/holy/Documents/unreal-engine-mcp/Python run unreal_mcp_server_advanced.py
```

If `uv` is not on PATH in a client app, always use the absolute path above.

### Codex MCP Config

Codex on this machine uses:

```toml
[mcp_servers.unrealMCP]
args = [
    "--directory",
    "/Users/holy/Documents/unreal-engine-mcp/Python",
    "run",
    "unreal_mcp_server_advanced.py",
]
command = "/Users/holy/.local/bin/uv"
enabled = true
```

### Recommended MCP Workflow

When using Unreal MCP:

1. inspect first
2. make one focused change
3. re-inspect or compile
4. summarize exactly what changed

Useful operations include:

- `get_actors_in_level`
- `find_actors_by_name`
- `set_actor_transform`
- `create_blueprint`
- `compile_blueprint`
- `add_component_to_blueprint`

### MCP Prompting Guidance

- ask Unreal MCP to inspect current state before making scene edits
- prefer explicit actor names, asset names, coordinates, and parent classes
- for Blueprint tasks, ask for compile or validation after edits
- for level edits, request a concise list of modified actors
- avoid large destructive scene-wide operations unless explicitly requested

## Version Control Rules

- commit only intentional source or project changes
- do not commit generated binaries or intermediate files
- keep commits narrow and descriptive
- do not mix unrelated config, plugin, and gameplay changes without a reason
- if the worktree contains user changes, avoid sweeping adds like `git add .` unless you have verified every staged file

## Communication Rules For Agents

- be concise and concrete
- report what changed, not a vague claim of success
- mention what was validated
- call out assumptions and known gaps
- if blocked by editor state, missing assets, or runtime-only behavior, say so clearly

## What Good Looks Like

A good agent change in this repository usually has these properties:

- one clear purpose
- obvious file ownership
- no unnecessary abstraction
- no surprising side effects
- validated by build, MCP inspection, or both
- easy for the next human to continue from

## References

- Upstream README: `/Users/holy/Documents/unreal-engine-mcp/README.md`
- Advanced server notes: `/Users/holy/Documents/unreal-engine-mcp/Python/README_advanced.md`
- Troubleshooting: `/Users/holy/Documents/unreal-engine-mcp/DEBUGGING.md`
