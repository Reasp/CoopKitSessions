# Changelog: CoopKitSessions

All notable changes to CoopKitSessions are listed here, newest first. The plugin follows [Semantic Versioning](https://semver.org/):
the version below matches `VersionName` in `CoopKitSessions.uplugin`, and the notes of a released version are pasted into
the Changelog field of the Fab listing.

Sections used in a version (omit the empty ones):

- **Breaking** - changes to the integration contract: Gameplay Tags, interfaces, Blueprint API, replication. Bumps the major version.
- **Added** - new features. Bumps the minor version.
- **Changed** - behavior changes that keep the contract.
- **Fixed** - bug fixes. Bumps the patch version.
- **Engine** - Unreal Engine versions added or dropped.

## [Unreleased]

## [1.1.0] - 2026-11-22

### Added

- Kick and ban for the host. **Kick Player** removes a player and tells them why: the client gets **On Session Failure** with the new reason *Kicked* and the host's text, and does not try to reconnect (the engine's own kick looks like a lost connection, so the client reconnected). **Ban Player** also keeps the player out for the rest of the session: a join, a reconnect and a direct connection to the host's address are refused, and the player gets the reason *Banned* with the text. A search still lists the session. **Unban Player**, **Get Banned Players**, **Is Player Banned** and **Get Last Removal Reason** go with them. The bans end with the session.
- **Find Sessions For Group** and **Quick Play For Group** for parties: only sessions with enough open slots for the whole group are listed or joined, and a group that fits nowhere hosts. **Find Sessions With Attributes** and **Quick Play** work as before.
- Setup check for Steam and EOS. It finds the usual reasons why a game works in the editor but not in a build: a service that quietly fell back to Null, a net driver that does not exist on your engine version, a missing engine plugin, a packaged build without `steam_appid.txt`, session flags that Steam in Unreal Engine 5.8 refuses, EOS without product settings, and a menu or match map that the packaged build does not contain. Each finding comes with a fix. The Steam and EOS setups have not been tested with a real login yet.
- Setup window *Tools > CoopKit Sessions Setup*. It lists the findings for your project. The buttons **Apply recommended setup: Null**, **Steam** and **EOS** write the settings for your engine version into `Config/DefaultEngine.ini` and enable the engine plugins, after you confirm. They never write your EOS product settings.
- The running game, also a packaged one, checks its setup a second after it starts and writes the findings to the log (`Setup check ...`). The Blueprint nodes **Check Setup** and **Get Setup Findings** on the sessions subsystem return them, and the demo panel shows a red line when something is wrong.
- Session profiles in the CoopKit Hub (*Tools > CoopKit Hub*, category **Sessions Profile**), next to the types of other installed CoopKit plugins. Lobby rules stay in the Content Browser.
- Sessions profile card: Properties, Preview, Parts, Summary and Validation. A session profile uses no other asset, so Parts is empty and says so, and Preview has no picture.
- New Sessions Profile wizard: pick a profile to start from and a session type. New profiles go to `/Game/CoopKit/SessionsProfiles/<Name>/`.
- Plugin functions to rename, duplicate and delete session profiles. The hub has no buttons for them.
- Hand a lost host to your own code, for example for host migration. With **Set Handle Host Loss Externally** on, a client that loses its host does not reconnect, does not open the menu map and gets no **On Session Failure**. When the engine is back on the game default map, **On Host Lost** fires with the reason and the lost session's attributes, and **Host Session** and **Find Sessions With Attributes** work right in that event. Off by default.
- **Find Sessions In Background** (Blueprint node and C++) searches while you host or play, for example to notice a second host of your group. It works in any state, does not change the state or the results of **Find Sessions**, and ends with the new event **On Background Find Complete**. It returns full sessions and running matches too. Only one search runs at a time on the online subsystem, so a background search and **Find Sessions** wait for each other. On Null, a host cannot be found by others during its 5-second search.
- Invites and friends for Steam and EOS. The subsystem listens for accepted invites from the moment the game starts: the Steam overlay, *Join Game* in the Steam friends list, a game started from an invite, or EOS. **On Invite Accepted** fires with the invited session. By default nothing else happens (*Invite Accept Policy* = *Event Only*), as in 1.0.0 plus the event: your code decides and calls **Join Invited Session**. With *Join At Once* (project setting or **Set Invite Accept Policy**) the player leaves the current session without being asked, also in a running match, and joins the invited one, from the menu, the lobby, a match or while reconnecting. While **Set Handle Host Loss Externally** is on, an invite only fires the event. Invites to sessions of another type, such as a party, and invites on a dedicated server are ignored. Not tested with a real Steam or EOS login yet.
- **Show Invite UI** (Steam overlay), **Read Friends** with presence, **Invite Friend** and **Join Friend**, in Blueprint and C++, with the async nodes **Read Friends** and **Join Friend**. Where the platform cannot do it, they fail with the new reason *Not Supported*: Null has no friends, and the engine has no invite overlay for EOS. New failure reasons *Friends Failed* and *Invite Failed*.
- Demo panel: **Friends** in the menu, with a *Join* button per friend who is in a game, and **Invite friends** in a game. On Null the panel explains that friends need Steam or EOS. The demo sets *Join At Once* for its own game.
- AI agent skill `coopkit-sessions` for Claude Code, Antigravity, Codex and other agents. From your own C++ module it builds a server list, a session value that players can filter by, a lobby feature or a reaction to returning players. It then builds the Editor target and runs the tests. If the build cannot run, the agent tells you what to install. Install the skill with *Tools > CoopKit > Install AI Agent Skills* or the button in the CoopKit Hub.
- Editor surface for AI agents: `UCKSessionsAgentLibrary` (`unreal.CKSessionsAgentLibrary` in Python, also callable from Editor Utility Blueprints) lists, creates, describes, validates, exports and imports the plugin's data assets and runs the project check, answering in JSON. The AI agent skill has a new section, "Drive the editor", with Python examples.

### Fixed

- A client with a **Menu Map** set in the project settings now returns to it after the host leaves or the connection is lost. Before, it could end up on the project's default map instead. This only happened when the Menu Map and the default map were different.
- README: *Online services* names the EOS net driver class and says that it is the same on Unreal Engine 5.6, 5.7 and 5.8. It also explains `steam_appid.txt` and has a table of symptoms, causes and fixes for Steam and EOS.
- README: the Steam setup now uses the Steam Sockets net driver (`/Script/SteamSockets.SteamSocketsNetDriver`). The old `OnlineSubsystemSteam.SteamNetDriver` is not loaded in Unreal Engine 5.6 to 5.8. The new setting has not been tested with a real Steam login yet.
- AI agent skill: it now says the plugin supports Windows (Win64) only, instead of implying Mac support. Its version-check example uses an engine version from the supported range (5.6 to 5.8). It also explains how to test data validation and what to do when a test that starts several Play In Editor instances cannot start.

### Changed

- Creating a session profile in the Content Browser (*Add > CoopKit > Sessions Profile*) now opens the New Sessions Profile wizard first.
- AI agent skill: the build steps are now plain text in `SKILL.md` instead of scripts. The agent still finds the engine, builds the project with the engine's `Build.bat` and reads the build log using `reference/build_and_diagnose.md`. We removed `scripts/build.ps1` and `scripts/build.sh` because Fab does not accept executables in plugin packages. How you use the skill does not change.

## [1.0.0] - 2026-11-13

### Added

- Sessions subsystem on the Online Subsystem: host (listen or dedicated server), find, join, leave, start and end the match; Null, Steam or EOS by project configuration.
- Join in progress, controlled by the session profile; newcomers are refused at login while a closed match runs.
- Reconnection after a lost connection, and the host's player records with joined, left and reconnected events.
- Lobby component (game state) and ready component (player state): ready checks, countdown, travel to the match map, seamless when the game mode allows it.
- Data assets with their own editor: Sessions Profile and Sessions Lobby Rules (Summary and Validation tabs).
- Blueprint async nodes Host Session, Find Sessions and Join Session; search result library.
- Gameplay Tags `CK.Sessions.State.Lobby`, `.Countdown`, `.InMatch` and `.Ready`.
- Session attributes: the Host Session With Attributes node, Set Session Attribute (updates a running session) and Get Session Attributes. Empty or reserved keys fail with the failure reason Invalid Attribute.
- Searches filtered by attributes: the Find Sessions With Attributes node. Results are matched by exact string, checked in the plugin on every subsystem.
- Quick Play node: joins the matching session with the fewest free slots, or hosts one when none matches.
- Leave Session To Map: leaves the session and opens a map of your choice instead of the menu map.
- Ready-to-play demo: lobby and match maps with a panel of buttons that runs the whole demo with the mouse (host, find and join, ready, the match and back to the lobby). The panel numbers the games it finds and names the players *Player 1*, *Player 2* in join order. With one Play In Editor window, a button on the panel restarts Play with two windows. The overview map points to this demo instead of console commands.
- Console commands `ck.sessions.*` for developers, with `Key=Value` attributes for Find, Quick Play and Attribute, and the debug overlay `ck.sessions.Debug`.
- Automation tests for sessions, join in progress, reconnection, the lobby, seamless travel, attribute filters and Quick Play on Null, and the demo panel. Steam and EOS are not covered by tests.

### Engine

- Unreal Engine 5.6, 5.7 and 5.8 on Windows (Win64).
