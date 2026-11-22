# CoopKit Sessions

Online sessions for a co-op Unreal Engine game, from the main menu to the match: host, find and join, a lobby with ready checks, join in progress and reconnection. Free and open source (MIT).

- **Blueprint nodes and a C++ subsystem** on top of the engine's Online Subsystem: Null for LAN play and tests, Steam or EOS by configuration only.
- **Lobby with ready checks:** one component on the game state counts down and travels everyone to the match map, seamlessly when the game mode uses seamless travel.
- **Reconnection:** a client that loses the connection finds the same session and joins it again; the host recognizes the returning player.
- **Kick and ban:** the host removes a player and tells them why, and that client does not try to reconnect. A banned player is refused on join, reconnect and direct connection until the session ends. Bans go by online id and are not saved.
- **Groups:** *Find Sessions For Group* and *Quick Play For Group* list or join only sessions with room for the whole group.
- **Invites and friends** on Steam: invite a friend, join a friend, and accept invites from the overlay or the friends list. On EOS the engine has no invite overlay, so those calls fail with a clear reason. Not tested with a real Steam or EOS login yet.
- **Setup check for Steam and EOS:** a window in *Tools > CoopKit Sessions Setup* and a check in the running game list the usual reasons why online play works in the editor but not in a build, each with a fix. The check cannot see the Steam client.
- **No base classes to inherit from:** no game mode, game state or player controller of the plugin's own.
- **Tested with a bad network:** a server and two clients in one editor process, 150 ms packet lag and 2% loss on every connection, for listen and dedicated servers.

**Supported:** Unreal Engine 5.6, 5.7 and 5.8, Windows (Win64). The automation tests use the Null subsystem; Steam and EOS are not covered by them.

## Install

Clone or copy this repository into your project as `Plugins/CoopKitSessions`, or install the plugin from [Fab](https://www.fab.com/listings/9e96749d-d852-4495-a5ca-1c78574ea9d4). Enable **CoopKit Sessions** in *Edit > Plugins* and restart the editor.

Documentation (quick start, online services, data assets, lobby and travel, reconnection, events and replication): [`Docs/README.md`](Docs/README.md). Changes: [`CHANGELOG.md`](CHANGELOG.md).

## See it in a game

[CoopKit Sample](https://github.com/Reasp/CoopKitSample) is a co-op sample project built with the CoopKit plugins. Its main menu, sessions and lobby come from this plugin.

## Need it in your project?

I set up Steam and EOS sessions and lobbies, fix replication bugs and review multiplayer code. Hire me on [Upwork](https://www.upwork.com/freelancers/~01cb01e68c8e69f34e), write to me on [LinkedIn](https://www.linkedin.com/in/reasp/) or use the form on [my website](http://reasp.pro/#contact).

## License

MIT, see `LICENSE`.
