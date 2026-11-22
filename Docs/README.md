# CoopKit Sessions

Copyright 2026 Ivan Iaitckikh.

CoopKit Sessions gives a co-op game its online sessions, from the main menu to the match:

- **Host, find, join.** Blueprint nodes and a C++ subsystem on top of the engine's Online Subsystem: Null for local play and tests, Steam or EOS by configuration only.
- **Lobby with ready checks.** One component on the game state: players toggle ready, the lobby counts down, then travels everyone to the match map, seamlessly when the game mode uses seamless travel.
- **Join in progress.** A match in progress stays visible and joinable, or closes to newcomers, as the session profile says.
- **Reconnection.** A client that loses the connection finds the same session and joins it again; the host recognizes the returning player and tells your code, so it can restore the player's data.
- **Data assets with their own editor.** Session profiles and lobby rules are data assets with a Summary (what the online subsystem receives) and a Validation tab. Session profiles also live in the CoopKit Hub, with a card and a wizard that makes a new profile.
- **An AI agent skill.** `coopkit-sessions` lets Claude Code, Antigravity, Codex and other agents build a server list, a lobby feature or a reaction to returning players from your own C++ module.

The plugin changes none of your base classes: no game mode, game state or player controller to inherit from.

## Supported engine versions

Unreal Engine 5.6, 5.7 and 5.8. Platform: Windows (Win64).

## Installation

Install the plugin from Fab, then enable **CoopKit Sessions** in *Edit > Plugins* and restart the editor. The plugin enables the engine plugins **Online Subsystem** and **Online Subsystem Utils**; **Online Subsystem Null** is on by default in every project.

## Editor tools, step by step

[EditorTools.md](EditorTools.md) shows the editor tools of this plugin in screenshots. You use only the mouse. The one thing you type is the name of a new asset.

## Quick start

The plugin ships a demo that is ready to play. Open the map and click **Play**. You do everything with the mouse, on a panel of buttons in the top left corner of each game window. The panel shows the session, the lobby and the players, and tells you what to do next.

1. In the Content Browser, open **Settings** and turn on **Show Plugin Content**. Double-click `/CoopKitSessions/Maps/L_SessionsDemo`, then click **Play** in the toolbar.
2. A session needs two game windows: one hosts, the other joins. With the default Play settings there is one window, so the panel shows **Play with 2 windows**. Click it. Play restarts with two windows. Your Play settings stay as they were.
3. In window 1, click **Host game**. The map reopens as a listen server, and the panel shows the lobby.
4. In window 2, click **Find games**. The search takes about 5 seconds. Then the host's game appears in the list as a button, for example **Join game 1 - 12 ms**. Click it.
5. In both windows, click **Ready**. The lobby counts down 3 seconds, then takes both players to the match map `L_SessionsMatch`.
6. On the match map, the panel lists the players. In window 1, **Back to lobby (everyone)** takes both players back to the lobby. In window 2, **Leave** leaves the session and reopens the lobby map.

How the demo is built:

- The game mode `BP_CKSessionsDemoGameMode` uses seamless travel and the HUD class **CKSessions Demo HUD**, which draws the panel.
- Its game state `BP_CKSessionsDemoGameState` has the lobby component with the rules `DA_SessionsDemoLobbyRules` (2 players, everyone ready, 3 s countdown).
- The panel is C++, without Widget Blueprints. It calls only the Blueprint API described below: Host Session, Find Sessions, Join Session, Set Ready, End Match and Leave Session To Map.
- Play In Editor does not use seamless travel (engine default) and logs "ProcessServerTravel: Seamless travel is disabled in PIE". Standalone and packaged games use the game mode's seamless travel.

In your project:

1. **Host, find, join** with the Blueprint nodes **Host Session**, **Find Sessions** and **Join Session** (category *CoopKit|Sessions*), or call `UCKSessionsSubsystem` from C++. Read search results with the *CoopKit|Sessions* library functions (host name, players, ping, map, attributes). For a "find a game or host one" button, use **Quick Play** (see *Session attributes and Quick Play*).
2. **Lobby:** add **CKSessions Lobby Component** to the game state Blueprint of your lobby map and set its **Rules** (or set *Default Lobby Rules* in the project settings). Turn on **Use Seamless Travel** in the lobby's game mode. Players call **Set Ready** on their **CKSessions Player Component** (the lobby adds it to every player state), as the demo's **Ready** button does. To send a player to a map of your choice when they leave (for example the lobby of a game mode), call **Leave Session To Map** instead of **Leave Session**.
3. **Project settings** (*Project Settings > Plugins > CoopKit Sessions*): default profile, default lobby rules, the menu map a player returns to after leaving, and whether a dedicated server creates its session by itself.

## Console commands for developers

The demo needs no console. For development and automation the plugin also has console commands: `ck.sessions.Host`, `ck.sessions.Find`, `ck.sessions.Join <n>`, `ck.sessions.Ready`, `ck.sessions.Leave` and others (see *Settings, console and API* below). `ck.sessions.Debug 1` draws the session state, the lobby and the search results on every game viewport (not in Shipping builds).

## Online services

The plugin talks to the Online Subsystem, the engine layer that connects a game to a service such as Steam or EOS. The service comes from your project's `Config/DefaultEngine.ini`. With nothing configured, Unreal uses Null: sessions on the local network, which is enough for development, LAN parties and the automation tests.

Steam and EOS are set up in `DefaultEngine.ini`. When a setting is wrong, the engine often says little. It can fall back to another subsystem or to the plain IP net driver, and the game seems to work until a second player tries to join. (The net driver is the engine class that carries game traffic between machines.) The plugin can check the settings for you.

The Steam and EOS setups below have not been tested with a real login yet. Check them by hand before you rely on them.

### Check your setup

- **In the editor:** open *Tools > CoopKit Sessions Setup*. The window lists what is wrong with a fix for each finding. The buttons **Apply recommended setup: Null**, **Steam** and **EOS** write the settings for your engine version into `Config/DefaultEngine.ini` and enable the engine plugins the service needs. They ask first, and they change only the lines of the setup (the service, its `bEnabled`, the app id when you have none, and the net driver). Restart the editor afterwards. The window never writes your EOS product settings: copy the client, product, sandbox and deployment ids from the Epic Developer Portal yourself.
- **In a running game, also a packaged one:** a second after the game starts, the log gets one line per finding, starting with `Setup check` (log category `LogCKSessions`). If a build does not work on a tester's machine, this log often names the cause. The Blueprint node **Check Setup** on the sessions subsystem runs the check again and returns the findings. **Get Setup Findings** returns the last result. The demo panel shows a red line when something is wrong.

The check looks at:

- whether `DefaultPlatformService` names Steam or EOS but the engine loaded another subsystem (it says Null, and sessions then work on the local network only);
- the net driver of `GameNetDriver`: whether the class loads and whether it is the driver of your service on this engine version;
- the engine plugins the service needs;
- `steam_appid.txt` next to the executable of a packaged build that is not Shipping;
- Steam only: whether Uses Presence and Use Lobbies If Available agree in the session settings (Steam 5.8 refuses the session when they differ; the settings of a Sessions Profile always agree);
- EOS only: whether the product settings are filled in;
- the menu map and the lobby's match map: do they exist and will the packaged build contain them.

Play In Editor cannot start Steam. There the check gives a note, not an error, and the setup window checks the files of the project instead of asking the running game.

### Steam

The easy way is *Tools > CoopKit Sessions Setup*: click **Apply recommended setup: Steam**. It writes the lines below and enables the plugins. To do it by hand, enable the **Online Subsystem Steam** and **Steam Sockets** plugins and add this to `Config/DefaultEngine.ini` (480 is Valve's test app id):

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480

[/Script/Engine.GameEngine]
-NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemUtils.IpNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
```

The engine uses the first definition named `GameNetDriver`. The line with `-` removes the engine's own entry, and the line with `+` adds the Steam one. The Steam driver class is `/Script/SteamSockets.SteamSocketsNetDriver` in Unreal Engine 5.6, 5.7 and 5.8. Older guides may name a different Steam driver class. Use the one above. This setup has not been tested with a real Steam login yet.

Steam does not start in Play In Editor. Test it in a standalone game window or a packaged build.

A packaged build that is not Shipping needs a file `steam_appid.txt` next to the executable, with your Steam app id on one line. Without it Steam does not start.

### EOS

The easy way is *Tools > CoopKit Sessions Setup*: click **Apply recommended setup: EOS**. It never fills in your product settings, so copy the client, product, sandbox and deployment ids from the Epic Developer Portal into *Project Settings > Plugins > Epic Online Services* yourself. To do the rest by hand, enable the **Online Subsystem EOS** plugin (and **Socket Subsystem EOS**, which holds the net driver) and add this to `Config/DefaultEngine.ini`:

```ini
[OnlineSubsystem]
DefaultPlatformService=EOS

[OnlineSubsystemEOS]
bEnabled=true

[/Script/Engine.GameEngine]
-NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemUtils.IpNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SocketSubsystemEOS.NetDriverEOS",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
```

| Engine | EOS net driver class | Engine plugins |
|---|---|---|
| 5.6, 5.7, 5.8 | `/Script/SocketSubsystemEOS.NetDriverEOS` (the engine redirects the old name `/Script/OnlineSubsystemEOS.NetDriverEOS` to it) | Online Subsystem EOS, Socket Subsystem EOS |

### Symptom, cause, fix

| What you see | Usual cause | Fix |
|---|---|---|
| The host creates the session, but the client cannot join (Steam) | `GameNetDriver` is the plain IP driver or a Steam driver class that does not load | Use `/Script/SteamSockets.SteamSocketsNetDriver`: click **Apply recommended setup: Steam** in *Tools > CoopKit Sessions Setup* |
| `DefaultPlatformService=Steam`, but sessions appear only on the local network | Steam did not start: the Steam client is not running, a packaged build that is not Shipping has no `steam_appid.txt`, or the Steam plugins are off | Start Steam and log in, add `steam_appid.txt`, enable the plugins. The log line `Setup check` shows what the plugin could find |
| Steam in Play In Editor does not work | Steam does not start inside the editor | Not an error: test Steam in a standalone game window or a packaged build |
| Creating or joining fails on Steam in 5.8 only | Uses Presence and Use Lobbies If Available differ in the session settings you built yourself | Set both flags the same way; settings from a Sessions Profile already do |
| `DefaultPlatformService=EOS`, but the game uses Null | The EOS product settings are empty, or the EOS plugin is off | Fill in the client, product, sandbox and deployment ids; enable the plugin |
| The log says a net driver class does not load | The class name is wrong for this engine version, or its plugin is off | Use the class from the tables above and enable its plugin |
| Works in the editor, but the packaged game stays on the wrong map | The menu map or the match map is not in the package | Add the map in *Project Settings > Packaging*; the check names the map |

Steam and EOS are not exercised by the automation tests: they need accounts and running service clients. The setup check itself is: its rules run on made-up settings for every supported engine version. Check the services by hand: host on one machine, Find Sessions and Join Session on another.

## Data assets

Create them in the Content Browser: *Add > CoopKit*, or make a session profile with **New Sessions Profile** in the CoopKit Hub (below). A double click opens the CoopKit Sessions data editor: *Properties*, *Summary* (what the asset means in practice) and *Validation* (problems, refreshed on every change).

| Data type | What it sets | Validation |
|---|---|---|
| `UCKSessionsProfileData` (*Sessions Profile*) | Session Type (searches only find sessions of the same type), Max Players, network scope (Auto: LAN with Null, online with services), lobbies, private sessions, join in progress, reconnect attempts, interval and grace time, search size, custom attributes. Summary shows the exact `FOnlineSessionSettings` for Null and for an online service. | Session Type required; 2 to 64 players; reconnect values not negative; no attribute key reused from the plugin (`CKS_*`) or the engine (`MAPNAME`, ...). |
| `UCKSessionsLobbyRulesData` (*Sessions Lobby Rules*) | Players needed, whether everyone must be ready, countdown, match map, travel options, early start by the host, automatic ready components. | Match Map required; at least 1 player; countdown not negative; travel options start with `?` and have no spaces. |

### Sessions profile card and the New Sessions Profile wizard

The CoopKit Hub (*Tools > CoopKit Hub*) lists session profiles under the category **Sessions Profile**. It shows each one by its asset name, because a session profile has no display name field. Lobby rules are not in the hub: create and edit them in the Content Browser as before.

- **Sessions profile card** (double-click a session profile, or *Open* in the hub): Properties, **Preview**, **Parts**, Summary and Validation. A session profile refers to no other asset, so the Parts tab is empty and says so.
- **New Sessions Profile wizard** (the hub button **New Sessions Profile**, or *Add > CoopKit > Sessions Profile* in the Content Browser): in **Start From**, pick a session profile to copy values from, or leave it empty for the plugin's defaults. In **Session Type**, type the type of your sessions, or leave it empty to keep the type of the *Start From* profile (with no *Start From* profile, the type is *Coop*). Click **Create** or **Cancel**.
- **Folders.** New session profiles go to `/Game/CoopKit/SessionsProfiles/<Name>/`. To change that, set the key `EntityPathTemplate.Sessions Profile` in the `[CoopKit.Workspace]` section of your project's `Config/DefaultEditor.ini`. Profiles you already have stay where they are, and the hub lists them there.

## AI agent skills

The plugin includes a skill for AI coding agents. It is called `coopkit-sessions`. It works with Claude Code, Antigravity, Codex and other agents that read skills from the `.agents/skills` folder of a project.

The skill needs a C++ project to build. If your project is Blueprint-only, the skill tells you how to add a C++ class first.

Ask your agent for a change to how your game hosts or joins sessions, for example "a server list that shows the host name, players and ping", "filter the games by difficulty" or "a player may be ready only after choosing a hero". In Claude Code you can also type `/coopkit-sessions`. The skill changes your project, not the plugin. If it has to edit the plugin as a last resort, it records the change in `CHANGES-LOCAL.md`. It adds an automation test for the change. Then it builds the Editor target with the engine's own build tool and runs the tests. It tests on the Null online subsystem and never logs in to Steam or EOS.

If the build cannot run, the skill tells you what to do:

- Install Visual Studio 2022 with the "Game development with C++" workload, including MSVC and the Windows SDK.
- If the editor is open with the project, close it, or press Ctrl+Alt+F11 in the editor so Live Coding compiles the change.

The skill can also edit CoopKitSessions data in the open editor, without clicks. The editor module has `UCKSessionsAgentLibrary`, a function library for Python and Editor Utility Blueprints (`unreal.CKSessionsAgentLibrary` in Python). With it the agent lists, creates, describes, validates, exports and imports the data assets and gets the answers as JSON. You can ask your agent, for example: "make a new one, set its values and check it". The editor needs the *Python Editor Script Plugin* enabled in *Edit > Plugins*.

Install the skill into your project in one of two ways:

- In the editor, use *Tools > CoopKit > Install AI Agent Skills* or the *Install AI Agent Skills* button in the CoopKit Hub window. This copies the skills of every enabled CoopKit plugin into `.claude/skills/` and `.agents/skills/` of your project. It asks before replacing skills that are already there. Replacing removes any edits you made to those skill files.
- By hand, copy the plugin's folder `Extras/AgentSkills/coopkit_sessions` to `<YourProject>/.claude/skills/coopkit-sessions` and to `<YourProject>/.agents/skills/coopkit-sessions`. The new folder name has a hyphen. The plugin's folder has an underscore because Fab file names allow only letters, digits and underscores.

Then open your agent in the project folder and ask for the change.

## Lobby, travel and join in progress

- The lobby component runs on the server: when the rules are met, it counts down (a player who stops being ready cancels it), then calls **Start Match** on the session and `ServerTravel` to the match map. With the same game state class on the match map, the component reports *In Match*.
- **Start Match** marks the session as in progress. Searches show it (*Is Match In Progress* on a result). If the profile refuses join in progress, the session closes to newcomers and the host refuses their login; players of the session may still come back.
- **Ready** is lobby state: a new player state after travel starts not ready. Data that must survive travel or a reconnect belongs to your code; the host's player records (below) tell you who is who.

## Reconnection and player records

- On a client that loses the connection (not one the host kicked, banned or refused), the subsystem switches to *Reconnecting*: after the engine returns the client to the default map, it searches for the same session and joins it again, up to *Reconnect Attempts* times. A private session, which searches never show, is rejoined through the last host address. When every attempt fails, **On Session Failure** fires; the player stays on the default map, or goes to the menu map when one is set in the project settings.
- The host keeps a record of every player (`Get Player Records`): connected or away, and how many times they came back. **On Player Joined**, **On Player Left** and **On Player Reconnected** fire on the host; a player who comes back within *Reconnect Grace Seconds* counts as reconnected. Restore the player's data in **On Player Reconnected**.

## Kick, ban and groups

- **Kick Player** (host only) removes a remote player and tells them why. The player's **On Session Failure** fires with the failure *Kicked* and the reason you gave, **Get Last Removal Reason** returns it, and the plugin does not try to reconnect. The kicked player can find and join the session again; to keep a player out, use **Ban Player**. The host's record of the player is dropped at once. It returns false on a client, for the host's own player and for a player without a connection.
- **Ban Player** (host only) kicks the player and keeps them out for the rest of the session. **Find Sessions** still lists the session, but a join, a reconnect and a direct connection to the host's address are refused at login. The player gets the failure *Banned* with the reason (**On Join Complete** for a join, **On Session Failure** for a player who was in the session). **Unban Player** lets the id in again, **Get Banned Players** lists the bans (player id, name, reason) and **Is Player Banned** checks an id. The bans end with the session (**Leave Session**, or hosting a new one). They are not saved, and the plugin does not ban a player on the platform. The ban is by the player's online id. A player without a valid online id is listed by name, but the login check cannot match them, so they are not kept out.
- **Find Sessions For Group** and **Quick Play For Group** take the number of players who play together. A session is listed or joined only when it has at least that many open slots, so the whole group fits. Quick Play For Group joins the fullest session that has room for the group, and when none has, it hosts, as **Quick Play** does when it finds nothing. Both leave the existing nodes unchanged: **Find Sessions With Attributes** and **Quick Play** count a group of one. The plugin does not keep the group together after the join: if another player takes a slot in the meantime, the join can still fail with *Session Full*, and Quick Play For Group then hosts.

## Handing a lost host to your own code

Host migration and similar features need to know when the host is lost. A client can hand this to your code instead of using the plugin's reconnection.

- Call **Set Handle Host Loss Externally** (true) on the subsystem of each client. It stays on until you turn it off, also in later sessions. It is off by default, and then the plugin behaves as in 1.0.0. It does nothing on the host.
- While it is on and a client loses its host, the subsystem does not reconnect, does not open the menu map and does not fire **On Session Failure**. The engine still takes the client back to the game default map. When the client is there (or after 30 seconds at most), the subsystem destroys the local online session, goes *Idle* and fires **On Host Lost**.
- **On Host Lost** gives the reason (*Host Left*, *Timed Out* or *Connection Lost*), the engine's message, the lost session's id and search result, and the custom attributes the session had when this client found it. Later changes by the host are not included.
- In that event, **Host Session** with a map and **Find Sessions With Attributes** work at once. For example, one player hosts a new session with a group attribute and the others search for it.
- **Leave Session** during the wait cancels the event.
- A kick also arrives as *Connection Lost*. If a kicked player must not host or search, check that in your handler.
- A crashed host is noticed only when the client's connection times out. With the engine's default `IpNetDriver` settings (`ConnectionTimeout`) that takes 60 seconds. Play In Editor turns connection timeouts off, so in the editor a client never notices a crashed host. The automation tests turn them back on with 5 seconds.
- Measured in our automation tests on Null, in the editor, on Unreal Engine 5.8, with 150 ms lag and 2% loss (one run each): **On Host Lost** fired 0.29 s after the host left, and 5.39 s after a simulated host crash with a 5-second timeout.

## Searching in the background

**Find Sessions** puts the subsystem in the *Searching* state and returns only sessions you can join. To watch other sessions while you host or play, for example to notice a second host of the same group, use **Find Sessions In Background** (Blueprint node and C++).

- It works in any state, also while hosting or in a session. It does not change the state, **Get Search Results** or **Get Last Failure**.
- It returns every session of the profile's *Session Type* that advertises every attribute of the filter, including full sessions and running matches. It is meant for watching, not for picking a game to join.
- It ends with **On Background Find Complete** (success, failure reason, results). The Blueprint node fires *On Success* or *On Failure*.
- **Is Background Search In Flight** tells you whether a background search is running or waiting for its turn.
- The online subsystem runs one search at a time. A background search waits until a running **Find Sessions** ends, and **Find Sessions** waits until a running background search ends. The subsystem shows *Searching* at once in both cases.
- Only one background search runs at a time. A second request, or a filter with an empty or reserved key, returns false at once. **On Background Find Complete** does not fire for it, so the listeners of the running search are not disturbed. The Blueprint node fires *On Failure* with *Busy* or *Invalid Attribute*.
- **Join Session** does not wait for a background search.
- On Null (LAN), a host is not visible to others while it searches. The engine's LAN session either answers searches or searches itself. Each search takes 5 seconds.

## Invites and friends

Players can invite friends and join a friend's game. The features are built for Steam and EOS and have not been tested with a real login on either yet. Null has no friends list, so in the editor and in any Null build they only answer *Not Supported*.

- **Accepting an invite.** The subsystem listens for accepted invites from the moment the game starts. An invite arrives when the player accepts it in the Steam overlay, clicks *Join Game* in the Steam friends list, starts the game from an invite (Steam passes `+connect_lobby`), or accepts it on EOS. **On Invite Accepted** fires with the session, its id and its attributes. A dedicated server ignores invites.
- **What happens next** is set by the *Invite Accept Policy*:
  - *Event Only* (default): only the event fires, nothing else happens. This is how 1.0.0 behaved, plus the event. Your code decides, for example asks the player, then calls **Join Invited Session**.
  - *Join At Once*: the subsystem leaves the current session and joins the invited one. It does not ask the player first, so a player in a running match leaves it too. This works from the menu, the lobby, a running match and while reconnecting. A player who hosts ends their own session. A host, search or join that is already running finishes first. If that takes longer than 60 seconds, the invite is dropped and **On Join Complete** fires with *Busy*. **Leave Session** also drops a waiting invite. **On Join Complete** fires when the player is connected.
  - To switch, set *Invite Accept Policy* in *Project Settings > Plugins > CoopKit Sessions > Invites*. That writes `InviteAcceptPolicy=JoinAtOnce` (or `EventOnly`) under `[/Script/CoopKitSessions.CKSessionsSettings]` in your `Config/DefaultCoopKitSessions.ini`. At runtime, call **Set Invite Accept Policy**. It lasts until the game instance ends, so call it again at every start. The demo panel sets *Join At Once* for its own game.
- While **Set Handle Host Loss Externally** is on, an invite only fires the event, whatever the policy. Your host migration code decides where the player goes.
- **Only invites to your game are taken.** The invited session must have the *Session Type* of the default profile or of the profile you used last. Invites to other sessions, such as a CoopKit Party session, are ignored.
- **Show Invite UI** opens the Steam overlay to invite friends to the current session. It needs a session, otherwise it returns *Busy*. The engine does not implement an invite overlay for EOS, so there it returns *Not Supported*: show your own list with **Read Friends** and **Invite Friend**.
- **Read Friends** (also a Blueprint node) reads the friends list with presence: name, online, playing this game, joinable. On EOS the friends come from the player's Epic account. **Get Friends** returns the last list, joinable friends first.
- **Invite Friend** sends the platform's invite to the current session. It needs a session, otherwise it returns *Busy*. **Join Friend** (also a Blueprint node) finds the game session a friend plays in and joins it. It works only while the subsystem is idle (otherwise *Busy*). If the friend is not in a session of your game, it fails with *Session Gone*.
- The demo panel has the same buttons: **Friends** in the menu (a *Join* button per friend who is in a game) and **Invite friends** in a game (the Steam overlay, or an *Invite* button per friend on EOS). On Null the panel explains that friends need Steam or EOS.

## Session attributes and Quick Play

Attributes are key and string value pairs that a session advertises, for example `Mode = Survival` or the `MatchId` a matchmaker returned. Searches can ask for them, and Quick Play uses them to put players with the same choice together. They work with OSS v1 and need no cloud service. They are tested on Null only; Steam and EOS pass the filters on according to the engine source, but you should check them by hand (see *Online services*).

- **From the profile:** the *Attributes* of the session profile are always advertised.
- **At runtime:** **Host Session With Attributes** hosts with extra attributes; a key that is also in the profile takes the new value. **Set Session Attribute** changes one attribute: while this machine hosts and is in the session, the session is updated at once; otherwise the value waits for the next host. It returns false on a client of someone else's session. **Get Session Attributes** returns the profile attributes plus the runtime ones. **Leave Session** clears the runtime attributes.
- **Reserved keys:** a key must not be empty or start with `CKS_`, and must not be an engine session name such as `MAPNAME`. **Host Session With Attributes** fails with *Invalid Attribute* for such a key, and **Set Session Attribute** returns false.
- **Search:** **Find Sessions With Attributes** returns only sessions that advertise every attribute of the filter with exactly the same value (case-sensitive string compare). Each filter attribute is sent to the online service as an Equals condition, and the plugin checks every result again, because Null does not filter. Plain **Find Sessions** does not filter by attributes, not even by the profile's.
- **Quick Play:** the **Quick Play** node searches with the attributes and joins the matching session with the fewest free slots (then the lowest ping). When nothing matches, it hosts a session with these attributes on the given map. If the join fails before travel because the session is full, gone, or refuses you, it hosts instead, once. The node fires *On Joined*, *On Hosted* or *On Failure*. In C++, call `UCKSessionsSubsystem::QuickPlay` and bind `OnQuickPlayComplete`.
- **Console:** `ck.sessions.Find Mode=Survival`, `ck.sessions.QuickPlay Mode=Survival` and `ck.sessions.Attribute MatchId=42` take `Key=Value` arguments.

With a matchmaker, the chosen host calls **Host Session With Attributes** with `MatchId`, and the other players call **Find Sessions With Attributes** with the same `MatchId`, then **Join Session**. The online service (Steam or EOS relay) handles the connection, so the matchmaker never needs the host's address.

## Overview map

The map `/CoopKitSessions/Maps/L_SessionsOverview` shows the plugin on four stands (enable *Show Plugin Content* in the Content Browser, open the map and click **Play**). Hosting, searching and joining need several game instances, so the map shows what runs in one world and says so where it cannot:

1. **Host, find and join.** A board with the live state of this game's sessions subsystem (state, host, search results). To host, find and join yourself, open the demo map of *Quick start*: its panel does it with buttons, in two windows.
2. **Lobby with Ready and countdown.** The plugin's own lobby component runs on the map: three bots get ready, the countdown starts, then one bot backs out and cancels it. The lobby never travels on this map: the loop resets the ready flag of visitors, so the map stays put.
3. **A player leaves and comes back.** The host's records of two bots: Dan leaves the game and returns, and the board shows that the host recognized him (`came back 1x`). Restore the player's data in **On Player Reconnected**.
4. **Blueprint nodes and C++ API.** The nodes and the classes to start from.

The bots are extra local players of the host, so they appear in the player list like any other player.

## Automation tests

Editor automation tests (Session Frontend, or `Automation RunTests CoopKit.Sessions`):

- `CoopKit.Sessions.Session.DedicatedServer` / `.ListenServer` - a server and two clients in one editor process; the clients join through the plugin's API: host, find, join, start the match, a second client finds the match in progress and joins, replication reaches it, the first client loses the connection and reconnects. 150 ms packet lag and 2% loss on every connection.
- `CoopKit.Sessions.Lobby.DedicatedServer` / `.ListenServer` - ready checks on every machine, countdown and its cancellation, seamless travel to the match map with the same connections.
- `CoopKit.Sessions.Roster.LateJoin` - the host's records follow a client that joins a running game.
- `CoopKit.Sessions.Attributes.ListenServer` - two listen server sessions in one process; the host sets `MatchId` during the game and refuses a reserved key; filtered searches find both sessions, none, or only the host's, and the client joins it.
- `CoopKit.Sessions.QuickPlay.ListenServer` - Quick Play: two players with different modes find nothing and host; a third player with the first mode joins the first host, not the second.
- `CoopKit.Sessions.Kick.ListenServer` - the host kicks a client with a reason: the client gets *Kicked* and the reason, does not reconnect, the host drops its record, and the client can join again.
- `CoopKit.Sessions.Ban.ListenServer` - the host bans a client: the client gets *Banned* and the reason and does not reconnect; a search still finds the session, but a join and the host's address opened directly are refused; after **Unban Player** the client joins again.
- `CoopKit.Sessions.QuickPlay.Group` - two listen servers host a session each. A search and a Quick Play for a group one larger than any session's open slots find nothing, and the Quick Play hosts. A Quick Play for a group that fits joins a session.
- `CoopKit.Sessions.BackgroundFind.ListenServer` - background search: a host finds another host's session and stays in its session; an idle player searches in the background before and after its own **Find Sessions**, and each search gets its own results; a second request and a reserved key are refused without an event; the player joins a session while a background search runs, then searches in the background as a client of that session.
- `CoopKit.Sessions.HostLoss.HostLeaves` / `.HostCrash` - a listen host and three clients. Clients A and B hand a lost host to the test's code, client C keeps the default. The host leaves, or goes silent (all its packets are dropped and its online session is destroyed) and the clients time out after 5 seconds. A and B get **On Host Lost** with the reason and the session's `Group=G1` attribute, without reconnecting or opening the menu map. Inside the event A hosts the demo map with `Group=G2`, and B searches for `Group=G2` until it finds it, then joins A. C gets **On Session Failure** when the host left, or starts reconnecting when the host crashed, as in 1.0.0.
- `CoopKit.Sessions.Invites.Rules` / `.FilterAndFriends` / `.JoinFromAnyState` - invites. The tests fire the same engine event that Steam and EOS fire when a player accepts an invite. `Rules` checks which invites are taken and the order of the friends list. `FilterAndFriends` checks that the project default is *Event Only*, that invites to another game or to a party are ignored, that *Event Only* and *Handle Host Loss Externally* only fire the event, and that friends and invites answer *Not Supported* on Null. In `JoinFromAnyState`, two players host a game each and a third player with *Join At Once* accepts invites from the menu, the lobby, a running match and while reconnecting to a silent host. Each time it ends up in the invited game.
- `CoopKit.Sessions.Demo.Friends` - the panel sets *Join At Once* for its game; on Null, its **Friends** and **Invite friends** buttons explain that friends need Steam or EOS.
- `CoopKit.Sessions.Demo.MapsLoad` / `.ListenServer` / `.RestartPlay` - the demo maps: they open in the editor and start Play with no warning or error in the log; two windows go through the whole demo by clicking the panel's buttons (host, find, join, ready, match, back to lobby, leave); one window restarts Play with two windows from its panel.
- `CoopKit.Sessions.Overview` - the overview map: a listen server and a client, the real lobby counts down and is cancelled by a bot, the host records a bot that leaves and returns.
- `CoopKit.Sessions.Diagnostics.Evaluate` / `.ApplySetup` / `.GameWorld` - the setup check. `Evaluate` runs every rule on sample settings for each supported engine version (wrong or missing net driver, a service that fell back to Null, missing `steam_appid.txt`, differing session flags, maps missing from the package, EOS without product settings). `ApplySetup` writes the recommended setup into a copy of `DefaultEngine.ini`: repeating it changes nothing, and other lines stay. `GameWorld` uses a game world with a net driver that does not exist, and checks that the check finds it and the demo panel shows it. None of these tests log in to Steam or EOS.
- `CoopKit.Sessions.Online.ProfileToSettings`, `CoopKit.Sessions.Online.Attributes`, `CoopKit.Sessions.Data.*Editor`, `CoopKit.Sessions.Tags.GetActorTags` - profile translation, attribute filters, the Quick Play choice (also for a group), the kick and ban messages, data editors, tag reading.
- `CoopKit.Sessions.Hub.*`, `CoopKit.Sessions.Workspace.*` - the CoopKit Hub (which plugin raises the hub window, entity types, New and Open), the New Sessions Profile wizard, the card, the workspace functions that rename, duplicate and delete session profiles, and a session profile in an older folder.
- `CoopKit.Sessions.Skills.*` - the AI agent skill is found and installed into a project, also with the hub button.

## Limitations and known issues

- Steam and EOS are configuration only and are not covered by the automation tests (see *Online services*); this includes searches with attributes, which Steam and EOS filter on their side per the engine source.
- Invites and friends are tested on Null through the engine event only. Nothing was tried with a real Steam or EOS login: not the overlay, *Join Game*, starting the game from an invite, the friends list or EOS invites.
- The setup check cannot see the Steam client. It knows that the engine fell back to another subsystem and lists the usual causes, but not which one applies. The engine log (`LogOnline`, `LogEOS`) may have the exact reason.
- A runtime attribute cannot be removed from a running session, only given another value.
- The engine's own `AGameSession::KickPlayer` and `BanPlayer` only close the connection, and the client takes that for a lost link and tries to reconnect. Use **Kick Player** and **Ban Player** of the sessions subsystem instead: they tell the client why and stop the reconnect. A ban lasts for the session only.
- One local player per game instance hosts, searches and joins; split-screen guests share the first player's session.
- With the Null subsystem a search always lasts 5 seconds (the engine's LAN query time). A host that searches in the background cannot be found by others during those 5 seconds.
- When a client joins, the engine logs `LogOnlineSession: Warning: OSS: Player ... is not part of session (GameSession)` twice. It comes from `APlayerState` unregistering the player during travel, is harmless and is not caused by the plugin.
- Dedicated servers: tested in the editor on a dedicated server world. The plugin also compiles into the sample project's Server target, built on Unreal Engine 5.8 from source, and two clients join that packaged server in a smoke test. On UE 5.6 and 5.7 the Server target is not compiled, and any Server target needs an engine built from source.

## Integration

How CoopKit Sessions talks to your project and to other plugins without compile-time links.

### Gameplay Tags

| Tag | Kind | Set by | Meaning |
|---|---|---|---|
| `CK.Sessions.State.Lobby` | State | Lobby component (game state), server | The lobby waits for players, also during the countdown. |
| `CK.Sessions.State.Countdown` | State | Lobby component, server | Enough players are ready; the lobby travels when the countdown ends. |
| `CK.Sessions.State.InMatch` | State | Lobby component, server | The current map is the match map of the lobby rules. |
| `CK.Sessions.State.Ready` | State | Player component (player state), server | The player is ready in the lobby. |

Both components publish their tags through `IGameplayTagAssetInterface`; the values come from replicated properties, so they are the same on the server and on clients.

### Components

| Component | Put on | Replicated | Notes |
|---|---|---|---|
| `UCKSessionsLobbyComponent` | Game state of the lobby map (and of the match map, to report *In Match*) | Yes | Server logic; rules from the component or the project's default lobby rules. `Find Lobby Component` finds it. |
| `UCKSessionsPlayerComponent` | Player state | Yes | The lobby adds it on the server unless the rules turn that off. `Find Local Player Component` finds the local player's one. |
| `UCKSessionsSubsystem` | Game instance subsystem (created by the engine) | No | Session, search, reconnection, player records. |

### Interfaces (optional, implement on your actors)

| Interface | Method | Default when not implemented |
|---|---|---|
| (none) | | The plugin needs nothing from your actors; react to its events instead. |

### Events

| Delegate / Gameplay Event | Fires on (Server / Owning Client / All) | Payload |
|---|---|---|
| `UCKSessionsSubsystem::OnStateChanged` | The machine whose subsystem changed | New `ECKSessionsState` |
| `UCKSessionsSubsystem::OnHostComplete`, `OnJoinComplete` | The machine that made the request | Success, `ECKSessionsFailure` |
| `UCKSessionsSubsystem::OnFindComplete` | The machine that searched | Success, results |
| `UCKSessionsSubsystem::OnBackgroundFindComplete` | The machine that searched in the background | Success, `ECKSessionsFailure`, results |
| `UCKSessionsSubsystem::OnQuickPlayComplete` | The machine that made the request | Success, hosted (true) or joined (false), `ECKSessionsFailure` |
| `UCKSessionsSubsystem::OnSessionFailure` | Owning Client (the session is lost for good, or the host kicked or banned the player: *Kicked*, *Banned*, message is the reason) | `ECKSessionsFailure`, message |
| `UCKSessionsSubsystem::OnHostLost` | Owning Client, only with *Handle Host Loss Externally* on | `FCKSessionsHostLoss`: reason, message, session id, attributes, search result |
| `UCKSessionsSubsystem::OnInviteAccepted` | The machine whose player accepted an invite to a session of your game | `FCKSessionsInvite`: search result, session id, attributes, joining (true when the subsystem joins by itself) |
| `UCKSessionsSubsystem::OnReadFriendsComplete` | The machine that read the friends list | Success, `ECKSessionsFailure`, friends (`FCKSessionsFriend`) |
| `UCKSessionsSubsystem::OnPlayerJoined`, `OnPlayerLeft`, `OnPlayerReconnected` | Server | Player state |
| `UCKSessionsLobbyComponent::OnPhaseChanged` | All | New `ECKSessionsLobbyPhase` |
| `UCKSessionsPlayerComponent::OnReadyChanged` | All | Component, ready |

### Replication

| Property / RPC | Condition | Reliable |
|---|---|---|
| `UCKSessionsLobbyComponent::Phase` | Every client (`COND_None`), `OnRep` fires `OnPhaseChanged` | Property |
| `UCKSessionsLobbyComponent::CountdownEndSeconds` | Every client (`COND_None`), server world time | Property |
| `UCKSessionsPlayerComponent::bReady` | Every client (`COND_None`), `OnRep` fires `OnReadyChanged` | Property |
| `UCKSessionsPlayerComponent::ServerSetReady` | Owning client to server | Reliable: a rare explicit player action |

Seamless travel keeps every player connected; player states are recreated on the match map, so ready flags start cleared there.

### Settings, console and API

| Name | Kind | Where |
|---|---|---|
| `UCKSessionsSettings` | Project settings, section `[/Script/CoopKitSessions.CKSessionsSettings]` in `DefaultCoopKitSessions.ini` | Runtime module `CoopKitSessions` |
| `UCKSessionsSubsystem` | Blueprint and C++: `HostSession`, `HostSessionWithAttributes`, `SetSessionAttribute`, `GetSessionAttributes`, `FindSessions`, `FindSessionsWithAttributes`, `FindSessionsInBackground`, `IsBackgroundSearchInFlight`, `QuickPlay`, `FindSessionsForGroup`, `QuickPlayForGroup`, `KickPlayer`, `BanPlayer`, `UnbanPlayer`, `IsPlayerBanned`, `GetBannedPlayers`, `GetLastRemovalReason`, `JoinSession`, `LeaveSession`, `LeaveSessionToMap`, `StartMatch`, `EndMatch`, `GetState`, `GetSearchResults`, `GetPlayerRecords`, `SetHandleHostLossExternally`, `IsHostLossHandledExternally` | Runtime module |
| `UCKSessionsSubsystem` invites and friends | Blueprint and C++: `SetInviteAcceptPolicy`, `GetInviteAcceptPolicy`, `JoinInvitedSession`, `ShowInviteUI`, `ReadFriends`, `GetFriends`, `InviteFriend`, `JoinFriend` | Runtime module |
| Host Session, Host Session With Attributes, Find Sessions, Find Sessions With Attributes, Find Sessions In Background, Join Session, Read Friends, Join Friend | Blueprint async nodes with *On Success* / *On Failure* | Runtime module |
| Quick Play | Blueprint async node with *On Joined* / *On Hosted* / *On Failure* | Runtime module |
| `UCKSessionsLibrary` | Blueprint functions reading a search result | Runtime module |
| `UCKSessionsSubsystem::CheckSetup`, `GetSetupFindings` | Blueprint: check the setup of the running game (net driver, plugins, maps, Steam and EOS settings) and read the findings (`FCKSessionsFinding`: code, severity, message, fix) | Runtime module |
| `CKSessions::Diagnostics` | C++: the setup check as pure functions of a settings snapshot, and the recommended `DefaultEngine.ini` text of Null, Steam or EOS | Runtime module, `CKSessionsDiagnostics.h` |
| *Tools > CoopKit Sessions Setup* | Editor window: findings with fixes and the Apply recommended setup buttons | Editor module `CoopKitSessionsEditor` |
| `ACKSessionsDemoHUD` | HUD class of the demo maps: the panel of buttons; set it on your own test map's game mode to get the same panel; the panel also switches the game's *Invite Accept Policy* to *Join At Once* | Runtime module |
| `CKSessions::Online` | C++: profile to `FOnlineSessionSettings` / `FOnlineSessionSearch`, joinability, attribute filters, the Quick Play choice | Runtime module, `CKSessionsOnlineSettings.h` |
| `ck.sessions.Host`, `Find [Key=Value ...]`, `QuickPlay [Key=Value ...]`, `Attribute Key=Value ...`, `Join <n>`, `Ready [0/1]`, `Start`, `Leave` | Console commands | Any build |
| `ck.sessions.Debug` | Console variable, 0/1: overlay | Non-Shipping builds |
| `LogCKSessions` | Log category | |

### CoopKit Hub

| Name | Kind | Where |
|---|---|---|
| Entity type **Sessions Profile** | `UCKSessionsProfileData` with the class metadata `CoopKitEntity="Sessions Profile"`, `CoopKitEntityFolder="SessionsProfiles"` | Runtime module (metadata exists in the editor only) |
| Hub contract version | Marker class `UCKSessionsHubInfo` with `CoopKitHubVersion="2"`. If several CoopKit plugins are installed, the one with the highest hub version shows the hub window. | Editor module `CoopKitSessionsEditor` |
| Folder settings | `[CoopKit.Workspace]` in `Config/DefaultEditor.ini`: `EntityPathTemplate`, `EntityPathTemplate.Sessions Profile`, `SharedPathTemplate` | Your project |
| *Install AI Agent Skills* | Hub window button and *Tools > CoopKit* menu entry | Editor module |

### Engine plugin dependencies

| Plugin | Required / Optional | Stability (Beta / Experimental) |
|---|---|---|
| Online Subsystem | Required | Stable |
| Online Subsystem Utils | Required | Stable |
| Online Subsystem Null | Optional (default service, on by default) | Stable |
| Online Subsystem Steam | Optional, your configuration | Stable |
| Online Subsystem EOS | Optional, your configuration | Stable |

## Changelog

See `CHANGELOG.md` next to `CoopKitSessions.uplugin` and the Changelog section of the Fab listing.
