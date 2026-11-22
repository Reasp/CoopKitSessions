# Rules for code you write for CoopKitSessions

These are the project's code rules in short. They apply to everything you add in the user's project.

## Language and legal

- Code, comments, log text, UI text (`LOCTEXT`) and test names are English (U.S.). Talk to the user in the user's language.
- First line of every `.h`, `.cpp`, `.cs` file: `// Copyright <year> <holder>. All Rights Reserved.` Take the year and the holder from an existing source file of the project (`Source/<Project>/`). Never write the plugin author's name into the user's files. If the project has no source file yet and the user did not say the holder, ask once.

## Style (Epic C++ Coding Standard)

- Tabs for indentation, Allman braces, always braces for `if`/`for`.
- Names: `UMyThing` for UObject, `AMyThing` for Actor, `FMyThing` for struct, `EMyThing` for enum (`enum class`), `IMyThing` for interface, `bFlag` for bool members, `PascalCase` everywhere. Use a prefix that belongs to the project (for a project named `Sleepwalker`: `USleepwalkerRegenComponent`), not `CK`: the `CK` prefix belongs to the plugin.
- `UPROPERTY`/`UFUNCTION` get a `Category` and a comment line above them (it becomes the tooltip); use `meta = (ClampMin = ...)` for numbers with a range.
- Include what you use (IWYU): header of the class you derive from, `CoreMinimal.h`, forward declarations in headers. `.generated.h` is the last include of a header.
- Modules: every module you depend on is in `Build.cs`. For a header of the plugin you need `"CoopKitSessions"`.
- Do not use `GetWorld()` of a data asset or another object without a world without checking for null.

## Engine versions

The project works on one engine version, but the plugin supports Unreal Engine 5.6, 5.7 and 5.8. Write code that does not break on an update: when an API differs between engine versions, isolate the difference in one header of the project, `<Project>Compat.h`, with `UE_VERSION_OLDER_THAN(5, 7, 0)` or the version where the API changed (from `Misc/EngineVersionComparison.h`) and nowhere else. Do not use APIs that the engine marks deprecated.

## Automation tests

- Every new logic and every new validation gets an automation test, in a `.cpp` of the project module under `Private/`, inside `#if WITH_DEV_AUTOMATION_TESTS`.
- Name: `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyClassTest, "<Project>.Sessions.<Feature>", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)`.
- Create objects with `NewObject<T>(GetTransientPackage())`, set private properties through reflection (`FindFProperty`), never write to project assets from a test.
- A test checks the good case and the bad case (what the validation says for bad data).

## Server and clients

- Gameplay changes happen on the server (`HasAuthority()`). Anything a client must see is replicated (a replicated property with `DOREPLIFETIME` in `GetLifetimeReplicatedProps`, or an existing replicated component).
- A data asset is shared by every player who uses it: it keeps no per-player state. Per-player values live on a component of the pawn or the player state.

## Editing the plugin (only when no extension point can do the job)

First tell the user why no extension point can do it and that the change will be local to this project. Then:

1. Locate the plugin. If it is already in the project's `Plugins/CoopKitSessions/`, go to step 3. If it is in the engine (installed from Fab, usually `<Engine>/Engine/Plugins/Marketplace/CoopKitSessions`), never edit it there: that is a shared install and the next update erases the change.
2. Copy the whole folder to `<Project>/Plugins/CoopKitSessions/`. In the copy delete `Binaries/` and `Intermediate/`. A plugin in the project overrides the one in the engine. If the build does not recompile the copy, set `"Installed": false` in its `.uplugin` and record that in `CHANGES-LOCAL.md`.
3. Keep every change small and record it in `<Project>/Plugins/CoopKitSessions/CHANGES-LOCAL.md`, one entry per change:

   ```markdown
   ## <date> <short title>
   - Files: Source/CoopKitSessions/Public/CKSessionsSubsystem.h, Private/CKSessionsSubsystem.cpp
   - Why: <what the user asked, why no extension point could do it>
   - Change: <one or two lines>
   - Plugin version when changed: <VersionName from CoopKitSessions.uplugin>
   ```
4. Tell the user: updating the plugin from Fab overwrites the copy; the entries of `CHANGES-LOCAL.md` say what to apply again.
5. Follow the plugin's own conventions in its files (its `CKSessions` prefix, its copyright line stays as it is), and keep its engine-version differences in `CKSessionsCompat.h`.

## Never

- Edit the engine, the Fab-installed plugin folder or other CoopKit plugins' files.
- Add packages, tools or installers; tell the user what to install instead (the build script's hints).
- Delete or rewrite the user's assets. A new field may need the user to open the asset and set it: say so.
