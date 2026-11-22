# CoopKitSessions: extension map

Plugin module names: `CoopKitSessions` (runtime), `CoopKitSessionsEditor` (editor only). Add `"CoopKitSessions"` to the dependency lists of your project module's `Build.cs`, and `"GameplayTags"` only if your code uses tags. Headers are included by plain name: `#include "CKSessionsSubsystem.h"`. The plugin needs the Online Subsystem (Null, Steam or EOS) enabled in the project; code that touches `FOnlineSessionSearchResult` also adds `"OnlineSubsystem"` to its `Build.cs`.

## Entity types (what the CoopKit Hub and the cards know)

An entity is a data asset the CoopKit Hub lists (*Tools > CoopKit Hub*): it has a "New ..." wizard and a card with Properties, Summary and Validation tabs. A part is a reference to another asset that the card shows with Open and Replace; a sessions profile refers to no other asset, so its Parts tab is empty. The lobby rules asset is ordinary plugin data (Add > CoopKit > Sessions Lobby Rules, its own data editor), not an entity.

<!-- coopkit:entities:begin -->
| Entity | Class | Header | Asset folder | Id property | Parts (`CoopKitPart`) | Subclass in project | Purpose |
|---|---|---|---|---|---|---|---|
| Sessions Profile | `UCKSessionsProfileData` | `CKSessionsProfileData.h` | `SessionsProfiles` | none | none | no (`final`) | Session profile: how a session of one kind is created, found, joined and reconnected to. |
<!-- coopkit:entities:end -->

A class marked `final` cannot be subclassed: extend it with a component, a subsystem or a data asset of your own that uses it. The **New Sessions Profile** wizard (*Add > CoopKit > Sessions Profile*, or the hub) starts from the plugin's defaults or from another profile and takes a *Session Type*; the game passes the profile to `HostSession`, `FindSessions` and `QuickPlay`, or leaves it out and uses *Default Profile* of *Project Settings > Plugins > CoopKit Sessions*.

## Classes you will meet

| Class | Role |
|---|---|
| `UCKSessionsSubsystem` (`CKSessionsSubsystem.h`) | game instance subsystem, the one door: `HostSession`, `HostSessionWithAttributes`, `SetSessionAttribute`, `FindSessions`, `FindSessionsWithAttributes`, `QuickPlay`, `JoinSession`, `LeaveSession`, `LeaveSessionToMap`, `StartMatch`, `EndMatch`, `GetState`, `IsHost`, `GetSearchResults`, `GetPlayerRecords`, events; `UCKSessionsSubsystem::Get(WorldContext)` |
| `UCKSessionsProfileData` (`CKSessionsProfileData.h`) | the session profile (`final`): session type, players, scope, lobbies, private, join in progress, reconnect, search, attributes; `IsReservedAttributeKey` |
| `UCKSessionsLobbyRulesData` (`CKSessionsLobbyRulesData.h`) | the lobby rules (`final`): players to start, everyone ready, countdown, match map, travel options, host may start early |
| `UCKSessionsLobbyComponent` (*CoopKit Sessions Lobby*, `CKSessionsLobbyComponent.h`) | on the game state: the lobby phase, countdown, player counts, `StartMatchNow`; `FindLobbyComponent(WorldContext)` |
| `UCKSessionsPlayerComponent` (*CoopKit Sessions Player*, `CKSessionsPlayerComponent.h`) | on the player state: the ready flag, `SetReady`, `OnReadyChanged`; `FindPlayerComponent`, `FindLocalPlayerComponent` |
| `UCKSessionsLibrary` (`CKSessionsLibrary.h`) | readers of a search result: host name, player counts, ping, map name, match in progress, dedicated, `GetSessionAttribute` |
| `FCKSessionsSearchResult`, `ECKSessionsState`, `ECKSessionsFailure`, `FCKSessionsPlayerRecord`, `FCKSessionsInvite`, `FCKSessionsFriend` (`CKSessionsTypes.h`) | a found session, the subsystem's state, the reason of a failure, a player of the session, an accepted invite, a friend with presence |
| `UCKSessionsSettings` (`CKSessionsSettings.h`, *Project Settings > Plugins > CoopKit Sessions*) | default profile, default lobby rules, menu map, dedicated server hosting, Invite Accept Policy (*Join At Once* or *Event Only*) |
| `UCKSessionsHostAction` and the other async actions (`CKSessionsAsyncActions.h`) | Blueprint nodes *Host Session*, *Find Sessions*, *Join Session*, *Quick Play*, *Read Friends*, *Join Friend* with one output pin per result |
| `CKSessions::Tags` (`CKSessionsGameplayTags.h`) | `CK.Sessions.State.Lobby`, `.Countdown`, `.InMatch`, `.Ready` |

## Hooks: where your code runs

| Hook | Runs | Use for |
|---|---|---|
| `OnHostComplete(bSuccess, Failure)` | the machine that hosted | open the lobby UI, show the failure |
| `OnFindComplete(bSuccess, Results)` | the machine that searched | fill a server list from `Results` |
| `OnJoinComplete(bSuccess, Failure)` | the machine that joined | close the menu, show the failure |
| `OnQuickPlayComplete(bSuccess, bHosted, Failure)` | the machine that pressed Quick Play | tell the player whether they joined a game or opened a new one |
| `OnSessionFailure(Failure, Message)` | a client, when the session is lost for good | go back to the menu with a message (the reconnect attempts already failed) |
| `OnHostLost(HostLoss)` | a client with `SetHandleHostLossExternally(true)`, when its host is gone (no reconnect, no menu map, no `OnSessionFailure`) | host migration or your own regroup: `HostLoss.Reason`, `HostLoss.Attributes`; `HostSession` with a map and `FindSessionsWithAttributes` work right in the handler |
| `OnInviteAccepted(Invite)` | the machine whose player accepted an invite to a session of this game (Steam overlay, Join Game, a game started from an invite, EOS) | default *Event Only*: ask the player, then `JoinInvitedSession(Invite.Session)`; with `SetInviteAcceptPolicy(JoinAtOnce)` the subsystem joins by itself without asking (`Invite.bJoining`) |
| `OnReadFriendsComplete(bSuccess, Failure, Friends)` | the machine that called `ReadFriends` | a friends list with Join (`JoinFriend`) or Invite (`InviteFriend`) buttons; `NotSupported` on Null |
| `OnStateChanged(NewState)` | this machine, on each state change | a status line ("Searching...", "Joining...") |
| `OnPlayerJoined`, `OnPlayerLeft`, `OnPlayerReconnected` (`APlayerState*`) | the host | give or free gear, announce, restore a returning player's data |
| `UCKSessionsLobbyComponent::OnPhaseChanged(Phase)` | every machine | lobby UI, countdown sound |
| `UCKSessionsPlayerComponent::OnReadyChanged(Component, bIsReady)` | every machine | the ready list of the lobby UI |

## Failures

`ECKSessionsFailure` (`CKSessionsTypes.h`) says why a request ended: `NoOnlineSubsystem`, `Busy`, `CreateFailed`, `HostTravelFailed`, `SearchFailed`, `InvalidResult`, `SessionFull`, `SessionGone`, `JoinFailed`, `ConnectFailed`, `ConnectionLost`, `ReconnectFailed`, `InvalidAttribute`, `NotSupported` (friends and invites with Null, the invite overlay with EOS), `FriendsFailed`, `InviteFailed`. A game shows its own text per value; a failure is an answer, not a crash. An empty search result list is a success, not `SearchFailed`.

## Where a new parameter goes (checklist)

For a parameter named `X`, decide each line; the first three are always needed.

1. **Property.** `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "...", meta = (ClampMin = "0"))` with a comment line (it becomes the tooltip). In a subclass, a component or a data asset in your project module.
2. **Validation.** `IsDataValid` (editor-only, `#if WITH_EDITOR`) of a data subclass: call `Super::IsDataValid(Context)` first, then report data the code cannot use (negative where it must not be, empty required reference) with `Context.AddError`. Warnings for suspicious values: `Context.AddWarning`. The card's Validation tab and the hub's Check Project show them.
3. **Where the user sees it.** A property of a data asset shows in the card's Properties tab; a value of the game's own UI shows in the widget. Tell the user which asset to open and which field to set.
4. **Card part.** The sessions profile card has no parts. A data asset of your own that refers to other assets gets the plain Data Asset editor; it gets a card only if it becomes a CoopKit entity itself (`CoopKitEntity` metadata, `CoopKitPart` on its `TSoftObjectPtr` properties).
5. **Runtime.** Hosting, the lobby's start and the player records are server work. If clients need a value (a lobby UI), put it on a replicated property of a component you own on the game state or player state; set it on the server and use `DOREPLIFETIME`. Menus and cosmetics run on every machine; a session request (host, find, join) runs on the machine that asks.
6. **Blueprint access.** `BlueprintReadOnly` on the property, plus `UFUNCTION(BlueprintPure)` getters for logic.
7. **Test.** An automation test for the logic and for the validation (see `examples.md`).

## What not to touch

- Anything under `Plugins/CoopKitSessions/` (see "Editing the plugin" in `rules.md`).
- The gameplay tags `CK.Sessions.*` and the hub contract names (`CoopKitEntity` metadata, tab `CoopKitHub`): other CoopKit plugins read them.
- The session keys the plugin advertises (the reserved attribute keys): a custom attribute never reuses them.

## Limits to tell the user

- Windows (Win64) only: the plugin's modules do not build for Mac or Linux.

## Where the editor pieces live (for reading, not editing)

`Source/CoopKitSessionsEditor/Private/`: `CKSessionsWorkspace.cpp` (parts of an entity, rename, duplicate, delete), `CKSessionsDataValidation.cpp`, `SCKSessionsCard.cpp` (card tabs), `CKSessionsProfileDataFactory.cpp` (the New Sessions Profile wizard), the other `*Factory.cpp` files (the "New ..." wizards), the `*Summary.cpp` files (Summary text). The runtime session code is `Source/CoopKitSessions/Private/CKSessionsSubsystem.cpp` and `CKSessionsLobbyComponent.cpp`. The plugin's own tests in `Source/CoopKitSessionsTests/Private/` (`CKSessionsSessionTest.cpp`, `CKSessionsLobbyTest.cpp`) show how to host and join on the Null subsystem and how to make its data in code.
