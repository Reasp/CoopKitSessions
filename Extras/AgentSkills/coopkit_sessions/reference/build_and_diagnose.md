# Read the build log

`SKILL.md`, section "Build the project", has the commands: it finds the engine and the target, builds the Editor target with the engine's `Build.bat` and writes the full log to `Saved\CoopKitAgent\build.log`. This file says what the lines of that log mean. Search the log for the quoted words; the case of the letters does not matter.

## Errors in the code: BUILD FAILED

Fix them and build again. A compiler error names the file, the line and the problem (`MyComponent.cpp(42): error C2065: ...`); fix the code there. These kinds have a usual fix:

| The log says | Usual fix |
|---|---|
| `fatal error C1083: Cannot open include file` | A header was not found. Add the module that owns it to the dependency lists of the project module's `Build.cs` (the engine documentation page of the class names its module). |
| `error LNK2019`, `error LNK2001`, `unresolved external symbol` | A function is declared but not found by the linker. Add the owning module to the `Build.cs` dependencies and make sure the plugin class has its `MODULE_API` macro. |
| `Could not find definition for module`, `Unable to find module '...' referenced by` | A module named in a `Build.cs` file does not exist. Check the spelling in `PublicDependencyModuleNames` and `PrivateDependencyModuleNames` and that the plugin that owns the module is enabled in the `.uproject`. |
| `Unable to find plugin '...'` | A plugin the project needs is missing or disabled. Check the "Plugins" list of the `.uproject` and that the plugin is installed (in the engine or in the project's `Plugins` folder). |

## The computer or the project is not ready: CANNOT BUILD

Stop changing code. Tell the user the text of the matching row in their language, without jargon, and stop. Do not install anything yourself.

| The log says | Tell the user |
|---|---|
| `Visual Studio 2022 must be installed`, `Visual Studio 2022 or newer must be installed`, `Unable to find valid Visual Studio`, `No suitable Visual Studio` | Visual Studio 2022 is missing. Install Visual Studio 2022 Community (free) and tick the workload "Game development with C++" in the installer. |
| `Unable to find valid ... C++ toolchain`, `Visual Studio 2022 (...) must be installed`, `Unable to find valid MSVC`, `Failed to find MSVC` | The C++ compiler (MSVC) that this engine version needs is missing. Open the Visual Studio Installer, choose Modify and tick "Game development with C++"; under Individual components also tick the newest "MSVC v143 - VS 2022 C++ x64/x86 build tools". |
| `Windows SDK ... must be installed`, `No available Windows SDKs found`, `Unable to find a valid Windows SDK`, `Cannot open include file: 'windows.h'` | The Windows SDK is missing. Open the Visual Studio Installer, choose Modify and tick "Windows 11 SDK" (or "Windows 10 SDK") under Individual components. |
| `NETFXSDK` | The .NET Framework SDK is missing. In the Visual Studio Installer tick ".NET Framework 4.8 SDK" (or 4.6.2 and newer) under Individual components. |
| `Unable to build while Live Coding is active` | The editor is open with Live Coding on. Close the editor and ask the agent to build again, or press Ctrl+Alt+F11 in the editor to compile there. |
| `LNK1104: cannot open file`, `The process cannot access the file`, `Unable to delete ... .dll` | A file the build writes is locked, usually because Unreal Editor (or another build) still has it open. Close Unreal Editor and build again. |
| `A conflicting instance of ... is already running` | Another Unreal build is running on this computer. Wait until it finishes, then build again. |
| `Couldn't find target rules file`, `does not have any code` | The project has no C++ target: it is a Blueprint-only project. Add a C++ class once (Tools > New C++ Class in the editor); that creates the Source folder and the targets. |
| `not enough space on the disk`, `There is not enough space` | The disk is full. Free some space (builds need several GB) and build again. |

The engine not found and no `*Editor.Target.cs` stop the build before it starts; `SKILL.md` says what to tell the user then.

## What the computer needs

Before the first build of a project the user needs: Visual Studio 2022 with the workload "Game development with C++" (it brings MSVC and the Windows SDK), the .NET Framework 4.8 SDK, at least 10 GB free on the project drive, and Unreal Editor closed or Live Coding off. Do not check these one by one before building: build, and when the build fails the tables above name what is missing.

## Run the tests

After a successful build, run the tests of your code and, to be sure nothing else broke, the plugin's own `CoopKit.Sessions`. Use the engine's console editor without a window; `<Engine>` is the engine folder from step 1 of "Build the project" in `SKILL.md`.

- Windows: `<Engine>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests <Filter>; Quit" -unattended -nullrhi -nosplash -NoSound -stdout -FullStdOutLogOutput -TestExit="Automation Test Queue Empty"`
- Mac: `<Engine>/Engine/Binaries/Mac/UnrealEditor-Cmd <project>.uproject` with the same arguments.

`<Filter>` is the start of the test names; join several with `+`, for example `Sleepwalker.Sessions+CoopKit.Sessions`. The run takes from half a minute to a few minutes; do not start a second one. Each test ends with a line `Test Completed. Result={Success}` or `Result={Fail}`, and the run with `**** TEST COMPLETE. EXIT CODE: 0 ****` when all passed. Show the user the names and messages of failed tests. If the editor cannot start because the project is open in the editor, ask the user to close it.

The plugin's tests that run a server with clients (they use `CKSessionsNetTestSession`, see `Source/CoopKitSessionsTests/Private/`) start several Play In Editor instances inside that one editor process and run without a window, about half a minute each. If they report that Play In Editor could not start (another Play session is running), tell the user instead of changing your code.
