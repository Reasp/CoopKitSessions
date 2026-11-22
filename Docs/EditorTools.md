# CoopKitSessions: editor tools

Step-by-step pictures of the editor tools of CoopKitSessions. You do everything with the mouse; the only typing is a name for a new asset. Your editor may look slightly different from the pictures.

The pictures are in `Docs/Images`.

## Show the plugin's content

The plugin's own assets (examples, demo maps) live in the plugin folder. To see them in the Content Browser, click **Settings** in the Content Browser and turn on **Show Plugin Content**.

![Content Browser Settings menu with Show Plugin Content](Images/ContentBrowserSettings.png)

## Create lobby rules or a session profile

1. In the Content Browser, open the folder for the new asset.
2. Click **Add**, point to **CoopKit**, then click **Sessions Lobby Rules**.
3. Enter a name for the asset.

![Add menu, CoopKit submenu, Sessions Lobby Rules entry](Images/CreateAsset.png)

To make a session profile, click **Add**, point to **CoopKit**, then click **Sessions Profile**. The New Sessions Profile wizard opens first. It is described in *Make a new session profile with the wizard* below.

## Edit lobby rules

Double-click the asset in the Content Browser. The data editor opens with three sections.

![Data editor of Sessions Lobby Rules](Images/Editor.png)

- **Properties** (left): the data of the asset.

![Properties section](Images/EditorProperties.png)

- **Summary** (top right): what the values mean in practice.

![Summary section](Images/EditorSummary.png)

- **Validation** (bottom right): problems of the data, refreshed on every change. **Validate** checks again.

![Validation without problems](Images/EditorValidation.png)

When a value does not work, Validation shows the problem in red right away, for example **Min Players To Start** set to 0.

![Validation with an error](Images/EditorValidationError.png)

Session profiles open in their own card, described below.

## Open the CoopKit Hub

The hub lists the entity types of every CoopKit plugin in your project in one window: CoopKitSessions adds the category **Sessions Profile**.

1. On the main menu, click **Tools**, then click **CoopKit Hub**.

![Tools menu with the CoopKit Hub entry](Images/ToolsMenu.png)

2. The hub opens on the categories. Each entity type has a button that shows the number of its entities and the name of the plugin that adds it.

![CoopKit Hub with the categories, Sessions Profile highlighted](Images/Hub.png)

## Find a session profile

1. Click the category **Sessions Profile**. The left side now lists only this category. It also has a search box, the name box and **New Sessions Profile**. Click **< Back** to return to the categories.

![Sessions Profile category open, Back highlighted](Images/HubPickType.png)

2. Click a session profile in the list. A session profile has no separate display name, so the list shows the asset's name. The right side shows its name, type and asset path. **Open** opens its card in a separate window (a double click in the list does the same).

![Sessions Profile entities, one selected](Images/HubEntities.png)

3. **Where Used** shows the assets that use the selected profile. A profile only made with the wizard is used by nothing yet.

![Where Used result](Images/HubWhereUsed.png)

4. **Check Project** checks every entity of every type: data errors and duplicate ids. The result appears in the text box below the buttons.

![Check Project result](Images/HubCheckProject.png)

## Make a new session profile with the wizard

1. In the **Sessions Profile** category, click the name box and enter a name (letters, digits, underscores). The button **New Sessions Profile** turns on.

![Name entered, New Sessions Profile button highlighted](Images/HubNewName.png)

2. Click **New Sessions Profile**. The wizard opens. In **Start From**, pick a profile to copy, or leave it empty for the defaults. In **Session Type**, type the type for your game's sessions, or leave it empty to keep the type of the copied profile (or *Coop* when you copied none). Click **Create**.

![New Sessions Profile wizard](Images/NewWizard.png)

3. The new profile appears in the hub's list and is selected, and its card opens. The text box below the buttons says "Created" and shows the asset path. The new asset is not saved yet: click **Save All** to keep it.

![Hub after the wizard](Images/HubCreated.png)

## The session profile card

The card shows everything about one session profile. Open it from the hub (**Open**) or double-click the asset in the Content Browser.

![Sessions Profile card](Images/Card.png)

- **Properties**: the data of the profile.

![Properties section](Images/CardProperties.png)

- **Preview**: the name of the profile. A session profile has no picture to show.

![Preview section](Images/CardPreview.png)

- **Parts**: the other assets the profile uses. A session profile uses none, so the list is empty.

![Parts section](Images/CardParts.png)

- **Summary**: the exact session settings the online subsystem receives, for Null and for an online service.

![Summary section](Images/CardSummary.png)

- **Validation**: problems of the data, refreshed on every change. **Validate** checks again.

![Validation without problems](Images/CardValidation.png)

When a value does not work, Validation shows the problem in red right away, for example **Max Players** set to 1.

![Validation with an error](Images/CardValidationError.png)

## Install the AI agent skills

The skills let an AI coding agent extend the plugin with you (see *AI agent skills* in the README).

1. On the main menu, click **Tools**, point to **CoopKit**, then click **Install AI Agent Skills**. The CoopKit Hub has the same button.

![Tools, CoopKit, Install AI Agent Skills](Images/ToolsMenuCoopKit.png)

![Install AI Agent Skills button in the hub](Images/HubSkillsButton.png)

2. A message lists the skill folders that were copied into your project. Click **OK**. If some folders could not be copied, the message shows a warning icon and lists them.

![Skills installed](Images/SkillsInstalled.png)

3. If the skills are already in the project, the plugin asks first. **Yes** replaces them and removes any change you made to their files; **No** keeps them.

![Replace the installed skills?](Images/SkillsReplace.png)


## Play the demo with the mouse

1. Show the plugin's content (see above), double-click `/CoopKitSessions/Maps/L_SessionsDemo`, then click **Play** in the toolbar.

![Play button in the toolbar](Images/PlayButton.png)

2. A session needs two game windows. With the default Play settings there is one window, and the panel in its top left corner shows **Play with 2 windows**. Click it: Play restarts with two windows. Your Play settings stay as they were.

![One window with Play with 2 windows](Images/DemoOneWindow.png)

![Two windows after the restart: the host window](Images/DemoTwoWindowsHost.png)

![Two windows after the restart: the second window](Images/DemoTwoWindowsClient.png)

3. In window 1, click **Host game**. The map reopens as a listen server and the panel shows the lobby.

![Window 1 hosts](Images/DemoStart.png)

4. In window 2, click **Find games**. After about 5 seconds the host's game appears in the list as a button, for example **Join game 1 - 12 ms**. Click it.

![Window 2 found the game](Images/DemoFound.png)

5. In both windows, click **Ready**. The lobby counts down 3 seconds, then takes both players to the match map.

![Lobby with both players](Images/DemoLobby.png)

![Countdown](Images/DemoCountdown.png)

6. On the match map, the panel lists the players. In window 1, **Back to lobby (everyone)** takes both players back to the lobby; in window 2, **Leave** leaves the session.

![Match](Images/DemoMatch.png)

![Back in the lobby](Images/DemoBackInLobby.png)
