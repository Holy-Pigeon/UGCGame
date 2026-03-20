# UGCGame

Unreal Engine 5.7 playground project for an in-game AI hot reload workflow built on `puerts`.

## Current Focus

The current project focus is an in-game JavaScript hotfix loop:

- generate `puerts` hotfix code from an in-game chat panel
- preview the generated code before applying it
- reload a dedicated `puerts` runtime to make the hotfix live
- keep multi-turn chat history in the running session
- keep the latest generated code snapshot as conversation context
- support GitHub Copilot with either:
  - pasted GitHub token
  - GitHub device-code login inside the game flow

This is still a hotfix/code-generation tool, not a full autonomous agent runtime.

## Project Layout

- `Source/UGCGame`
  Main game module and AI hot reload implementation.
- `Plugins/Puerts`
  JavaScript runtime integration.
- `Content/JavaScript`
  Runtime scripts and generated hotfix modules.
- `Docs/AIHotReload.md`
  Additional notes for the AI hot reload system.

## Main Runtime Pieces

- `UGCAIHotReloadSubsystem`
  Owns provider config, Copilot auth flow, chat session state, generation requests, pending hotfix state, and runtime reload.
- `UGCAIHotfixBridge`
  Narrow bridge exposed to generated JavaScript.
- `UGCAIHotReloadChatWidget`
  In-game UI for auth, prompt entry, transcript, code preview, generate, and reload.
- `AGCAIHotReloadPlayerController`
  Spawns the chat widget on the `MainScene` map.

## In-Game Flow

1. Open the project and run the `MainScene` map.
2. Use the in-game panel to either paste a GitHub token or click `Device Login`.
3. Enter a prompt describing the hotfix.
4. Press `Generate`.
5. Review the generated code preview.
6. Press `Reload` to apply the pending hotfix.

Generated files are written under:

- `Content/JavaScript/AIHotfix/Generated/Current.js`
- `Content/JavaScript/AIHotfix/Generated/Current.ts`

## Safe Bridge Surface

Generated modules are expected to work through the bridge instead of broad engine mutation:

- `bridge.LogMessage(text)`
- `bridge.EmitGameplayCommand(name, payloadJson)`
- `bridge.GetWorldSeconds()`
- `bridge.GetActiveModuleName()`

Gameplay-side code can bind to `OnGameplayCommand` and decide what actions are actually allowed.

## Chat Session Behavior

The in-game chat now keeps:

- prior `user` and `assistant` messages for the active session
- the most recent generated JavaScript snapshot
- runtime/apply/error lines in the visible transcript

New generations are sent with recent chat history plus the latest code snapshot so follow-up prompts can refine the previous result instead of starting from scratch each time.

## Build

Editor target build on macOS:

```bash
"/Users/holy/Documents/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealEditor Mac Development \
  -Project="/Users/holy/Documents/UnrealProjects/UGCGame/UGCGame.uproject" \
  -WaitMutex
```

## Notes

- Default game mode is `AGCAIHotReloadGameMode`.
- The chat widget is currently shown only when the loaded map name ends with `MainScene`.
- Default provider settings live in `UGCAIHotReloadSettings`.
- The current Copilot device login uses GitHub OAuth device flow, then exchanges that OAuth token for a Copilot API token.

## Related Docs

- [AI Hot Reload](./Docs/AIHotReload.md)
- [Agent Instructions](./AGENTS.md)
