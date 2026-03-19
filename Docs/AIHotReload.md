# AI Hot Reload

This project now includes a first-pass in-game AI hotfix runtime built on `puerts`.

## What it does

- Loads AI-authored JavaScript from `Content/JavaScript/AIHotfix`.
- Restarts a dedicated `puerts` runtime and executes the generated module immediately.
- Exposes a narrow bridge object so generated code can emit gameplay commands instead of directly mutating broad engine state.
- Can call GitHub Copilot first, then other OpenAI-compatible providers later.

## Core classes

- `UGCAIHotReloadSubsystem`
- `UGCAIHotfixBridge`
- `UGCAIHotReloadSettings`

## Runtime flow

1. Configure `FGCAIProviderConfig` in game or in project settings.
2. Use the in-game chat panel to submit a request.
3. The subsystem requests code from the configured model.
4. Generated code is written to both `.js` and `.ts` mirrors under `Content/JavaScript/AIHotfix/Generated`.
5. Press `Reload` to recreate a fresh `FJsEnv` and make the pending hotfix live.

## Safe gameplay boundary

Generated modules should work through the bridge:

- `bridge.LogMessage(text)`
- `bridge.EmitGameplayCommand(name, payloadJson)`
- `bridge.GetWorldSeconds()`
- `bridge.GetActiveModuleName()`

Use `OnGameplayCommand` in Blueprint or C++ to bind the generated behavior into the actual game.

## Provider notes

The first implementation targets GitHub Copilot directly by exchanging a GitHub token for a Copilot API token, then calling the Copilot chat endpoint.

- The in-game chat window currently expects a GitHub token with Copilot access.
- Alibaba and other providers can be added as explicit adapters on the same subsystem interface.

The code is structured so we can add explicit provider adapters next.
