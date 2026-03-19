# AGENTS.md

This file describes how AI coding agents should work in this Unreal Engine project.

## Project

- Project root: `/Users/holy/Documents/UnrealProjects/UGCGame`
- Unreal version: `5.7`
- Primary language: `C++`
- Local Unreal Engine install: `/Users/holy/Documents/UE_5.7`
- Unreal MCP repo: `/Users/holy/Documents/unreal-engine-mcp`
- Unreal MCP plugin path: `/Users/holy/Documents/UnrealProjects/UGCGame/Plugins/UnrealMCP`

## Agent Goals

- Prefer small, safe, reversible changes.
- Do not overwrite user changes unless explicitly asked.
- When changing gameplay or editor behavior, keep the project buildable in Unreal Editor.
- Prefer verifying changes with a build or targeted validation when practical.

## Project Safety Rules

- Do not delete user assets or generated content unless the task explicitly requires it.
- Treat `/Config/DefaultEngine.ini` as user-owned unless the request clearly needs engine config edits.
- Keep generated Unreal folders out of version control unless there is a strong reason:
  `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`
- Put project plugins under `/Plugins`.

## Build

Use this command to build the editor target for the project:

```bash
"/Users/holy/Documents/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealEditor Mac Development \
  -Project="/Users/holy/Documents/UnrealProjects/UGCGame/UGCGame.uproject" \
  -WaitMutex
```

## Unreal MCP

This project is wired to use `flopperam/unreal-engine-mcp`.

### What It Is For

Use Unreal MCP when the task benefits from operating inside Unreal Editor, especially for:

- inspecting actors in the current level
- creating or modifying Blueprints
- placing actors or changing transforms
- inspecting materials or components
- world-building and level-editing workflows

Prefer normal code edits for pure C++ source changes. Prefer Unreal MCP when the request is editor-centric or Blueprint-centric.

### Preconditions

Before using Unreal MCP:

1. Open `/Users/holy/Documents/UnrealProjects/UGCGame/UGCGame.uproject` in Unreal Editor.
2. Ensure the `UnrealMCP` plugin is enabled.
3. Let the editor finish loading the project and map.

### Start The MCP Server

Run:

```bash
/Users/holy/.local/bin/uv --directory /Users/holy/Documents/unreal-engine-mcp/Python run unreal_mcp_server_advanced.py
```

If `uv` is not on PATH in a client app, always use the absolute path above.

### MCP Client Config

Example MCP config:

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "/Users/holy/.local/bin/uv",
      "args": [
        "--directory",
        "/Users/holy/Documents/unreal-engine-mcp/Python",
        "run",
        "unreal_mcp_server_advanced.py"
      ]
    }
  }
}
```

### Recommended Workflow

When using Unreal MCP, prefer this sequence:

1. Inspect first.
2. Make one focused change.
3. Re-inspect or compile if relevant.
4. Summarize exactly what changed.

Examples of useful MCP operations mentioned in the upstream docs:

- `get_actors_in_level`
- `find_actors_by_name`
- `set_actor_transform`
- `create_blueprint`
- `compile_blueprint`
- `add_component_to_blueprint`

### Prompting Guidance For Agents

- Ask Unreal MCP to inspect the current state before making scene edits.
- Prefer explicit actor names, asset names, coordinates, and parent classes.
- For Blueprint tasks, ask for compile/validation after edits.
- For level edits, request a short summary of created or modified actors.
- Avoid large destructive scene-wide operations unless the user explicitly wants them.

## When To Use Code Vs MCP

Use code edits for:

- C++ gameplay logic
- module setup
- config files
- build fixes
- source-controlled project structure

Use Unreal MCP for:

- level editing
- actor placement
- transform changes
- Blueprint graph creation or modification
- editor-side inspection

## References

- Upstream README: `/Users/holy/Documents/unreal-engine-mcp/README.md`
- Advanced server notes: `/Users/holy/Documents/unreal-engine-mcp/Python/README_advanced.md`
- Troubleshooting: `/Users/holy/Documents/unreal-engine-mcp/DEBUGGING.md`
