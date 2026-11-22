// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDemoPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CKSessionsDemoHUD.h"
#include "CKSessionsLibrary.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsPlayerComponent.h"
#include "CKSessionsSubsystem.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CKSessionsDemoPanelWidget)

#define LOCTEXT_NAMESPACE "CKSessionsDemoPanel"

namespace CKSessions::Demo
{
	namespace
	{
		const FLinearColor PanelColor(0.02f, 0.03f, 0.05f, 0.85f);
		const FLinearColor TitleColor(0.45f, 0.75f, 1.0f);
		const FLinearColor BodyColor(0.92f, 0.92f, 0.92f);
		const FLinearColor HintColor(1.0f, 0.85f, 0.4f);
		const FLinearColor StatusColor(1.0f, 0.55f, 0.45f);

		/** Width of the panel in slate units: room for a found game's line while the middle of the window stays free. */
		constexpr float PanelWidth = 520.0f;
		constexpr float RowSpacing = 3.0f;
		constexpr float RefreshIntervalSeconds = 0.2f;


		void Show(UWidget* Widget, const bool bShow)
		{
			if (Widget != nullptr)
			{
				Widget->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			}
		}

		void ShowText(UTextBlock* Text, const FText& Value)
		{
			if (Text != nullptr)
			{
				Text->SetText(Value);
				Text->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			}
		}

		void SetLabel(const UButton* Button, const FText& Label)
		{
			if (UTextBlock* Text = Button != nullptr ? Cast<UTextBlock>(Button->GetContent()) : nullptr)
			{
				Text->SetText(Label);
			}
		}

		FText FailureText(const ECKSessionsFailure Failure)
		{
			return StaticEnum<ECKSessionsFailure>()->GetDisplayNameTextByValue(static_cast<int64>(Failure));
		}

		/** Why the friends section has no list: what the player can do instead. */
		FText FriendsFailureText(const ECKSessionsFailure Failure)
		{
			switch (Failure)
			{
			case ECKSessionsFailure::NotSupported:
				return LOCTEXT("FriendsNotSupported",
					"Friends and invites need Steam or EOS. This game runs without them, so there is no friends list.");
			case ECKSessionsFailure::FriendsFailed:
				return LOCTEXT("FriendsFailed", "Could not read your friends list. Check that you are logged in to Steam or your Epic account, then try again.");
			case ECKSessionsFailure::NoOnlineSubsystem:
				return LOCTEXT("FriendsNoOnline", "No online service is running, so there are no friends to show.");
			default:
				return FText::Format(LOCTEXT("FriendsOther", "Could not read your friends list: {0}"), FailureText(Failure));
			}
		}

		FText SessionLine(const UCKSessionsSubsystem& Sessions)
		{
			switch (Sessions.GetState())
			{
			case ECKSessionsState::Hosting:
				return LOCTEXT("SessionHosting", "Session: hosting (the map reopens as a listen server)...");
			case ECKSessionsState::Searching:
				return LOCTEXT("SessionSearching", "Session: looking for games (about 5 s)...");
			case ECKSessionsState::Joining:
				return LOCTEXT("SessionJoining", "Session: joining the host...");
			case ECKSessionsState::InSession:
				return Sessions.IsHost() ? LOCTEXT("SessionHost", "Session: you host this game") : LOCTEXT("SessionClient", "Session: you joined the host's game");
			case ECKSessionsState::Reconnecting:
				return LOCTEXT("SessionReconnecting", "Session: connection lost, reconnecting...");
			case ECKSessionsState::Leaving:
				return LOCTEXT("SessionLeaving", "Session: leaving...");
			case ECKSessionsState::Idle:
			default:
				return LOCTEXT("SessionIdle", "Session: none yet");
			}
		}
	}
}

void UCKSessionsDemoRowButton::Setup(const int32 InRowIndex, const FCKSessionsDemoRowClicked& InOnRowClicked)
{
	RowIndex = InRowIndex;
	OnRowClicked = InOnRowClicked;
	OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoRowButton::HandleClicked);
}

void UCKSessionsDemoRowButton::HandleClicked()
{
	OnRowClicked.ExecuteIfBound(RowIndex);
}

void UCKSessionsDemoPanelWidget::SetDemoHUD(ACKSessionsDemoHUD* InDemoHUD)
{
	DemoHUD = InDemoHUD;
}

bool UCKSessionsDemoPanelWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	// The panel takes clicks; the rest of the screen stays with the game.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return bSuperInitialized;
}

void UCKSessionsDemoPanelWidget::BuildTree()
{
	using namespace CKSessions::Demo;

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrushColor(FLinearColor::Transparent);
	Root->SetPadding(FMargin(24.0f));
	Root->SetHorizontalAlignment(HAlign_Left);
	Root->SetVerticalAlignment(VAlign_Top);
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSize"));
	Size->SetWidthOverride(PanelWidth);
	Root->SetContent(Size);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(16.0f, 12.0f));
	Size->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	UTextBlock* Title = AddText(Column, TEXT("TitleText"), 26, TitleColor);
	Title->SetText(LOCTEXT("Title", "CoopKit Sessions demo"));

	SessionText = AddText(Column, WidgetNames::SessionText, 18, BodyColor);
	LobbyText = AddText(Column, WidgetNames::LobbyText, 18, BodyColor);
	PlayersText = AddText(Column, WidgetNames::PlayersText, 17, BodyColor);
	HintText = AddText(Column, WidgetNames::HintText, 17, HintColor);
	SetupText = AddText(Column, WidgetNames::SetupText, 16, StatusColor);

	PlayWindowsButton = AddButton(Column, UButton::StaticClass(), WidgetNames::PlayWindowsButton, FText::GetEmpty());
	PlayWindowsButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandlePlayWindowsClicked);
	HostButton = AddButton(Column, UButton::StaticClass(), WidgetNames::HostButton, LOCTEXT("Host", "Host game"));
	HostButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleHostClicked);
	FindButton = AddButton(Column, UButton::StaticClass(), WidgetNames::FindButton, LOCTEXT("Find", "Find games"));
	FindButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleFindClicked);

	ResultsText = AddText(Column, WidgetNames::ResultsText, 17, BodyColor);
	for (int32 Index = 0; Index < WidgetNames::MaxJoinButtons; ++Index)
	{
		UCKSessionsDemoRowButton* Row = Cast<UCKSessionsDemoRowButton>(AddButton(Column, UCKSessionsDemoRowButton::StaticClass(),
			FString::Printf(TEXT("%s%d"), WidgetNames::JoinButtonPrefix, Index), FText::GetEmpty()));
		Row->Setup(Index, FCKSessionsDemoRowClicked::CreateUObject(this, &UCKSessionsDemoPanelWidget::HandleJoinRowClicked));
		JoinButtons.Add(Row);
	}

	ReadyButton = AddButton(Column, UButton::StaticClass(), WidgetNames::ReadyButton, LOCTEXT("Ready", "Ready"));
	ReadyButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleReadyClicked);
	BackToLobbyButton = AddButton(Column, UButton::StaticClass(), WidgetNames::BackToLobbyButton, LOCTEXT("BackToLobby", "Back to lobby (everyone)"));
	BackToLobbyButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleBackToLobbyClicked);
	LeaveButton = AddButton(Column, UButton::StaticClass(), WidgetNames::LeaveButton, LOCTEXT("Leave", "Leave"));
	LeaveButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleLeaveClicked);

	FriendsButton = AddButton(Column, UButton::StaticClass(), WidgetNames::FriendsButton, LOCTEXT("Friends", "Friends"));
	FriendsButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleFriendsClicked);
	InviteButton = AddButton(Column, UButton::StaticClass(), WidgetNames::InviteButton, LOCTEXT("Invite", "Invite friends"));
	InviteButton->OnClicked.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleInviteClicked);
	FriendsText = AddText(Column, WidgetNames::FriendsText, 16, BodyColor);
	for (int32 Index = 0; Index < WidgetNames::MaxFriendButtons; ++Index)
	{
		UCKSessionsDemoRowButton* Row = Cast<UCKSessionsDemoRowButton>(AddButton(Column, UCKSessionsDemoRowButton::StaticClass(),
			FString::Printf(TEXT("%s%d"), WidgetNames::FriendButtonPrefix, Index), FText::GetEmpty()));
		Row->Setup(Index, FCKSessionsDemoRowClicked::CreateUObject(this, &UCKSessionsDemoPanelWidget::HandleFriendRowClicked));
		FriendButtons.Add(Row);
	}

	StatusText = AddText(Column, WidgetNames::StatusText, 16, StatusColor);
}

UTextBlock* UCKSessionsDemoPanelWidget::AddText(UPanelWidget* Parent, const TCHAR* Name, const int32 FontSize, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetAutoWrapText(true);
	if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Text)))
	{
		BoxSlot->SetPadding(FMargin(0.0f, CKSessions::Demo::RowSpacing));
	}
	return Text;
}

UButton* UCKSessionsDemoPanelWidget::AddButton(UPanelWidget* Parent, UClass* ButtonClass, const FString& Name, const FText& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(ButtonClass, *Name);
	UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Name + TEXT("_Label")));
	LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
	LabelText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	LabelText->SetJustification(ETextJustify::Center);
	LabelText->SetAutoWrapText(true);
	LabelText->SetText(Label);
	// The label takes the button's full width: wrapping then follows the button, not the label's previous text.
	if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Button->SetContent(LabelText)))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Button)))
	{
		BoxSlot->SetPadding(FMargin(0.0f, CKSessions::Demo::RowSpacing));
		BoxSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	return Button;
}

void UCKSessionsDemoPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		Sessions->OnHostComplete.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleHostComplete);
		Sessions->OnFindComplete.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleFindComplete);
		Sessions->OnJoinComplete.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleJoinComplete);
		Sessions->OnSessionFailure.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleSessionFailure);
		Sessions->OnReadFriendsComplete.AddUniqueDynamic(this, &UCKSessionsDemoPanelWidget::HandleReadFriendsComplete);
		// The demo joins an accepted invite by itself, so it works with the mouse alone; the plugin's default is Event Only.
		Sessions->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::JoinAtOnce);
		// A search finished on an earlier map (the found games survive travel in the subsystem) is still worth showing.
		bSearchDone = !Sessions->GetSearchResults().IsEmpty();
	}

	Refresh();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &UCKSessionsDemoPanelWidget::Refresh),
			CKSessions::Demo::RefreshIntervalSeconds, true);
	}
}

void UCKSessionsDemoPanelWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		Sessions->OnHostComplete.RemoveDynamic(this, &UCKSessionsDemoPanelWidget::HandleHostComplete);
		Sessions->OnFindComplete.RemoveDynamic(this, &UCKSessionsDemoPanelWidget::HandleFindComplete);
		Sessions->OnJoinComplete.RemoveDynamic(this, &UCKSessionsDemoPanelWidget::HandleJoinComplete);
		Sessions->OnSessionFailure.RemoveDynamic(this, &UCKSessionsDemoPanelWidget::HandleSessionFailure);
		Sessions->OnReadFriendsComplete.RemoveDynamic(this, &UCKSessionsDemoPanelWidget::HandleReadFriendsComplete);
	}
	Super::NativeDestruct();
}

UCKSessionsSubsystem* UCKSessionsDemoPanelWidget::GetSessions() const
{
	// The widget's world is the game instance's world, with or without a local player controller.
	UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(this);
	return Sessions != nullptr ? Sessions : UCKSessionsSubsystem::Get(GetOwningPlayer());
}

int32 UCKSessionsDemoPanelWidget::CountPlayWindows() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->WorldType != EWorldType::PIE || GEngine == nullptr)
	{
		return INDEX_NONE;
	}
	int32 Windows = 0;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		Windows += Context.WorldType == EWorldType::PIE && !Context.RunAsDedicated ? 1 : 0;
	}
	return Windows;
}

int32 UCKSessionsDemoPanelWidget::GetNeededWindows() const
{
	return DemoHUD.IsValid() ? DemoHUD->GetPlayWindows() : 2;
}

void UCKSessionsDemoPanelWidget::SetStatus(const FText& InText)
{
	CKSessions::Demo::ShowText(StatusText, InText);
}

void UCKSessionsDemoPanelWidget::Refresh()
{
	using namespace CKSessions::Demo;

	const UWorld* World = GetWorld();
	const UCKSessionsSubsystem* Sessions = GetSessions();
	if (World == nullptr || Sessions == nullptr)
	{
		return;
	}

	const ECKSessionsState State = Sessions->GetState();
	const ENetMode NetMode = World->GetNetMode();
	const bool bIdle = State == ECKSessionsState::Idle;
	const bool bInSession = State == ECKSessionsState::InSession || State == ECKSessionsState::Reconnecting;
	// Play In Editor's own net modes connect windows without a session; the lobby works there too.
	const bool bConnected = bInSession || NetMode != NM_Standalone;
	const UCKSessionsLobbyComponent* Lobby = bConnected ? UCKSessionsLobbyComponent::FindLobbyComponent(World) : nullptr;
	const ECKSessionsLobbyPhase Phase = Lobby != nullptr ? Lobby->GetPhase() : ECKSessionsLobbyPhase::Waiting;
	const bool bInMatch = Lobby != nullptr && Phase == ECKSessionsLobbyPhase::InMatch;
	const bool bInLobby = Lobby != nullptr && !bInMatch;
	const bool bServer = NetMode == NM_ListenServer;
	const UCKSessionsLobbyRulesData* Rules = Lobby != nullptr ? Lobby->GetRules() : nullptr;
	const int32 MinPlayers = Rules != nullptr ? Rules->GetMinPlayersToStart() : 1;
	const int32 PlayerCount = Lobby != nullptr ? Lobby->GetPlayerCount() : 0;
	const APlayerController* Controller = GetOwningPlayer();
	UCKSessionsPlayerComponent* LocalPlayer = Controller != nullptr ? UCKSessionsPlayerComponent::FindPlayerComponent(Controller->PlayerState) : nullptr;

	const int32 Windows = CountPlayWindows();
	const int32 NeededWindows = GetNeededWindows();
	const bool bTooFewWindows = Windows != INDEX_NONE && Windows < NeededWindows;
	const bool bCanRestart = bTooFewWindows && ACKSessionsDemoHUD::OnRestartPlayInEditor().IsBound();

	ShowText(SessionText, SessionLine(*Sessions));

	// Setup problems (Steam or EOS configured wrongly) show as one line; notes and a clean setup show nothing.
	FText SetupLine;
	int32 SetupProblems = 0;
	const FCKSessionsFinding* FirstProblem = nullptr;
	for (const FCKSessionsFinding& Finding : Sessions->GetSetupFindings())
	{
		if (Finding.Severity != ECKSessionsFindingSeverity::Info)
		{
			FirstProblem = FirstProblem != nullptr ? FirstProblem : &Finding;
			++SetupProblems;
		}
	}
	if (FirstProblem != nullptr)
	{
		SetupLine = FText::Format(LOCTEXT("SetupProblem", "Setup problem ({0}): {1} See the log, or Check Setup in the editor."), FText::AsNumber(SetupProblems), FText::FromString(FirstProblem->Message));
	}
	ShowText(SetupText, SetupLine);

	// Lobby or match line.
	FText LobbyLine;
	if (bInMatch)
	{
		LobbyLine = FText::Format(LOCTEXT("InMatch", "Match: every player is on {0}"), FText::FromString(UWorld::RemovePIEPrefix(World->GetMapName())));
	}
	else if (bInLobby)
	{
		switch (Phase)
		{
		case ECKSessionsLobbyPhase::Countdown:
			LobbyLine = FText::Format(LOCTEXT("Countdown", "Lobby: everyone is ready, the match starts in {0} s"),
				FText::AsNumber(FMath::CeilToInt(Lobby->GetCountdownSecondsLeft())));
			break;
		case ECKSessionsLobbyPhase::Traveling:
			LobbyLine = LOCTEXT("Traveling", "Lobby: travelling to the match...");
			break;
		default:
			LobbyLine = FText::Format(LOCTEXT("Waiting", "Lobby: {0} of {1} players ready, {2} needed to start"),
				FText::AsNumber(Lobby->GetReadyPlayerCount()), FText::AsNumber(PlayerCount), FText::AsNumber(MinPlayers));
			break;
		}
	}
	ShowText(LobbyText, LobbyLine);

	// Players of the lobby or the match, as the game state lists them.
	FText PlayersLine;
	const AGameStateBase* GameState = World->GetGameState();
	if ((bInLobby || bInMatch) && GameState != nullptr)
	{
		// Numbered in join order (player id), the same on every machine. Not the online name: with the editor's Null
		// online subsystem it is the computer's name.
		TArray<const APlayerState*> Players;
		for (const APlayerState* Player : GameState->PlayerArray)
		{
			if (Player != nullptr && !Player->IsInactive())
			{
				Players.Add(Player);
			}
		}
		Players.StableSort([](const APlayerState& Left, const APlayerState& Right) { return Left.GetPlayerId() < Right.GetPlayerId(); });
		TArray<FString> Lines;
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			const APlayerState* Player = Players[Index];
			FString Line = TEXT("  ") + FText::Format(LOCTEXT("PlayerNumber", "Player {0}"), FText::AsNumber(Index + 1)).ToString();
			if (Controller != nullptr && Player == Controller->PlayerState)
			{
				Line += LOCTEXT("You", " (you)").ToString();
			}
			const UCKSessionsPlayerComponent* PlayerComponent = UCKSessionsPlayerComponent::FindPlayerComponent(Player);
			if (bInLobby && PlayerComponent != nullptr)
			{
				Line += PlayerComponent->IsReady() ? LOCTEXT("PlayerReady", " - ready").ToString() : LOCTEXT("PlayerNotReady", " - not ready").ToString();
			}
			Lines.Add(Line);
		}
		const FText Heading = bInMatch ? LOCTEXT("MatchPlayers", "Players in the match ({0}):") : LOCTEXT("LobbyPlayers", "Players in the lobby ({0}):");
		PlayersLine = FText::FromString(FText::Format(Heading, FText::AsNumber(Lines.Num())).ToString() + TEXT("\n") + FString::Join(Lines, TEXT("\n")));
	}
	ShowText(PlayersText, PlayersLine);

	// What to do next.
	FText Hint;
	if (bTooFewWindows && (bIdle || bInLobby))
	{
		Hint = bCanRestart
			? FText::Format(LOCTEXT("HintWindows", "Sessions need {0} game windows: one hosts, the others join. Click Play with {0} windows."), FText::AsNumber(NeededWindows))
			: FText::Format(LOCTEXT("HintWindowsManual", "Sessions need {0} game windows. In the Play menu, set Number of Players to {0} and Net Mode to Play Standalone, then click Play."),
				FText::AsNumber(NeededWindows));
	}
	else if (bIdle && !bConnected)
	{
		Hint = Windows != INDEX_NONE
			? LOCTEXT("HintIdlePie", "Click Host game in one window. In another window, click Find games, then Join.")
			: LOCTEXT("HintIdleGame", "Click Host game here, or Find games to join a game that another copy of the game hosts on this PC or your network.");
	}
	else if (bInLobby && Phase == ECKSessionsLobbyPhase::Waiting)
	{
		if (PlayerCount < MinPlayers)
		{
			Hint = bServer ? LOCTEXT("HintWaitHost", "Waiting for players: in another window, click Find games, then Join.")
				: LOCTEXT("HintWaitClient", "Waiting for more players to join.");
		}
		else
		{
			Hint = LOCTEXT("HintReady", "Click Ready in every window. The match starts when everyone is ready.");
		}
	}
	else if (bInLobby && Phase == ECKSessionsLobbyPhase::Countdown)
	{
		Hint = LOCTEXT("HintCountdown", "Not ready stops the countdown.");
	}
	else if (bInMatch)
	{
		Hint = bServer ? LOCTEXT("HintMatchHost", "This is the match map. Back to lobby takes every player back to the lobby.")
			: LOCTEXT("HintMatchClient", "This is the match map. The host can take everyone back to the lobby; Leave quits this game.");
	}
	ShowText(HintText, Hint);

	// Buttons.
	Show(PlayWindowsButton, bCanRestart && (bIdle || bInLobby));
	SetLabel(PlayWindowsButton, FText::Format(LOCTEXT("PlayWindows", "Play with {0} windows"), FText::AsNumber(NeededWindows)));
	Show(HostButton, bIdle && NetMode != NM_Client);
	Show(FindButton, (bIdle || State == ECKSessionsState::Searching) && !bConnected);
	if (FindButton != nullptr)
	{
		FindButton->SetIsEnabled(bIdle);
		SetLabel(FindButton, bIdle ? LOCTEXT("Find", "Find games") : LOCTEXT("Searching", "Looking for games..."));
	}
	RefreshResults(*Sessions, bIdle && !bConnected && bSearchDone);

	const bool bShowReady = bInLobby && (Phase == ECKSessionsLobbyPhase::Waiting || Phase == ECKSessionsLobbyPhase::Countdown);
	Show(ReadyButton, bShowReady);
	if (ReadyButton != nullptr)
	{
		// The lobby gives each player a ready component on the server; a client sees it a moment after it arrives.
		ReadyButton->SetIsEnabled(LocalPlayer != nullptr);
		SetLabel(ReadyButton, LocalPlayer != nullptr && LocalPlayer->IsReady() ? LOCTEXT("NotReady", "Not ready") : LOCTEXT("Ready", "Ready"));
	}
	Show(BackToLobbyButton, bInMatch && bServer && DemoHUD.IsValid() && !DemoHUD->GetLobbyMap().IsNull());
	Show(LeaveButton, bInSession);
	RefreshFriends(*Sessions, bIdle && !bConnected, State == ECKSessionsState::InSession);
}

void UCKSessionsDemoPanelWidget::RefreshFriends(const UCKSessionsSubsystem& Sessions, const bool bIdleMenu, const bool bInSessionNow)
{
	using namespace CKSessions::Demo;

	Show(FriendsButton, bIdleMenu);
	Show(InviteButton, bInSessionNow);
	// The section belongs to the screen it was opened on: friends to join in the menu, friends to invite in a session.
	if ((FriendsMode == EFriendsMode::Join && !bIdleMenu) || (FriendsMode == EFriendsMode::Invite && !bInSessionNow))
	{
		FriendsMode = EFriendsMode::Hidden;
		FriendsLine = FText::GetEmpty();
	}
	ShowText(FriendsText, FriendsMode == EFriendsMode::Hidden ? FText::GetEmpty() : FriendsLine);

	const TArray<FCKSessionsFriend>& Friends = Sessions.GetFriends();
	for (int32 Index = 0; Index < FriendButtons.Num(); ++Index)
	{
		const bool bRow = FriendsMode != EFriendsMode::Hidden && !FriendsLine.IsEmpty() && Friends.IsValidIndex(Index);
		Show(FriendButtons[Index], bRow);
		if (!bRow)
		{
			continue;
		}
		const FCKSessionsFriend& Friend = Friends[Index];
		const FText Name = FText::FromString(Friend.DisplayName);
		FText Label;
		bool bEnabled = false;
		if (FriendsMode == EFriendsMode::Join)
		{
			bEnabled = Friend.bJoinable;
			Label = Friend.bJoinable ? FText::Format(LOCTEXT("JoinFriend", "Join {0}"), Name)
				: FText::Format(Friend.bOnline ? LOCTEXT("FriendNotInGame", "{0} - not in a game you can join") : LOCTEXT("FriendOffline", "{0} - offline"), Name);
		}
		else
		{
			bEnabled = Friend.bOnline;
			Label = Friend.bOnline ? FText::Format(LOCTEXT("InviteFriend", "Invite {0}"), Name) : FText::Format(LOCTEXT("FriendOffline", "{0} - offline"), Name);
		}
		FriendButtons[Index]->SetIsEnabled(bEnabled);
		SetLabel(FriendButtons[Index], Label);
	}
}

void UCKSessionsDemoPanelWidget::RefreshResults(const UCKSessionsSubsystem& Sessions, const bool bShow)
{
	using namespace CKSessions::Demo;

	const TArray<FCKSessionsSearchResult>& Results = Sessions.GetSearchResults();
	FText Heading;
	if (bShow)
	{
		Heading = Results.IsEmpty() ? LOCTEXT("NoGames", "No games found. Host a game in another window first, then click Find games again.")
			: LOCTEXT("Games", "Games found - click one to join:");
	}
	ShowText(ResultsText, Heading);

	for (int32 Index = 0; Index < JoinButtons.Num(); ++Index)
	{
		const bool bRow = bShow && Results.IsValidIndex(Index);
		Show(JoinButtons[Index], bRow);
		if (!bRow)
		{
			continue;
		}
		// Games are numbered in the order of the search results, not named after the host: with the editor's Null online
		// subsystem the host name is the computer's name. No player count: Null does not count the listen server's own
		// player, so a fresh game would read "0 players".
		const FText Label = FText::Format(LOCTEXT("JoinRow", "Join game {0} - {1} ms{2}"),
			FText::AsNumber(Index + 1), FText::AsNumber(UCKSessionsLibrary::GetPingMs(Results[Index])),
			UCKSessionsLibrary::IsMatchInProgress(Results[Index]) ? LOCTEXT("InProgress", ", match in progress") : FText::GetEmpty());
		SetLabel(JoinButtons[Index], Label);
	}
}

void UCKSessionsDemoPanelWidget::HandlePlayWindowsClicked()
{
	SetStatus(FText::GetEmpty());
	if (!ACKSessionsDemoHUD::OnRestartPlayInEditor().IsBound() || !ACKSessionsDemoHUD::OnRestartPlayInEditor().Execute(GetNeededWindows()))
	{
		SetStatus(LOCTEXT("RestartFailed", "Play In Editor could not restart. Set Number of Players in the Play menu instead."));
	}
}

void UCKSessionsDemoPanelWidget::HandleHostClicked()
{
	SetStatus(FText::GetEmpty());
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		// An empty map keeps the current one: a standalone game reopens it as a listen server.
		Sessions->HostSession(nullptr, TSoftObjectPtr<UWorld>());
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleFindClicked()
{
	SetStatus(FText::GetEmpty());
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		Sessions->FindSessions(nullptr);
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleJoinRowClicked(const int32 RowIndex)
{
	SetStatus(FText::GetEmpty());
	UCKSessionsSubsystem* Sessions = GetSessions();
	if (Sessions != nullptr && Sessions->GetSearchResults().IsValidIndex(RowIndex))
	{
		Sessions->JoinSession(Sessions->GetSearchResults()[RowIndex]);
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleReadyClicked()
{
	SetStatus(FText::GetEmpty());
	const APlayerController* Controller = GetOwningPlayer();
	if (UCKSessionsPlayerComponent* LocalPlayer = Controller != nullptr ? UCKSessionsPlayerComponent::FindPlayerComponent(Controller->PlayerState) : nullptr)
	{
		LocalPlayer->SetReady(!LocalPlayer->IsReady());
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleBackToLobbyClicked()
{
	SetStatus(FText::GetEmpty());
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client || !DemoHUD.IsValid() || DemoHUD->GetLobbyMap().IsNull())
	{
		return;
	}
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		// The session is open to newcomers again, then the server travels every player back, seamlessly when the game mode allows it.
		Sessions->EndMatch();
	}
	World->ServerTravel(DemoHUD->GetLobbyMap().GetLongPackageName(), false);
}

void UCKSessionsDemoPanelWidget::HandleLeaveClicked()
{
	SetStatus(FText::GetEmpty());
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		Sessions->LeaveSessionToMap(DemoHUD.IsValid() ? DemoHUD->GetLobbyMap() : TSoftObjectPtr<UWorld>());
	}
}

void UCKSessionsDemoPanelWidget::HandleFriendsClicked()
{
	SetStatus(FText::GetEmpty());
	FriendsMode = EFriendsMode::Join;
	FriendsLine = LOCTEXT("ReadingFriends", "Reading your friends list...");
	if (UCKSessionsSubsystem* Sessions = GetSessions())
	{
		// Null answers at once (Not Supported): the handler fills the line.
		Sessions->ReadFriends();
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleInviteClicked()
{
	SetStatus(FText::GetEmpty());
	UCKSessionsSubsystem* Sessions = GetSessions();
	if (Sessions == nullptr)
	{
		return;
	}
	FriendsMode = EFriendsMode::Invite;
	const ECKSessionsFailure Failure = Sessions->ShowInviteUI();
	if (Failure == ECKSessionsFailure::None)
	{
		FriendsLine = LOCTEXT("OverlayOpen", "Pick the friends to invite in the Steam overlay.");
	}
	else if (Failure == ECKSessionsFailure::NotSupported)
	{
		// No invite overlay (EOS, Null): invite from the friends list instead. Null has none either, and says so.
		FriendsLine = LOCTEXT("ReadingFriends", "Reading your friends list...");
		Sessions->ReadFriends();
	}
	else
	{
		FriendsLine = FText::Format(LOCTEXT("InviteUnavailable", "Could not open the invite window: {0}"), CKSessions::Demo::FailureText(Failure));
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleReadFriendsComplete(const bool bSuccess, const ECKSessionsFailure Failure, const TArray<FCKSessionsFriend>& Friends)
{
	if (FriendsMode == EFriendsMode::Hidden)
	{
		return;
	}
	if (!bSuccess)
	{
		FriendsLine = CKSessions::Demo::FriendsFailureText(Failure);
	}
	else if (Friends.IsEmpty())
	{
		FriendsLine = LOCTEXT("NoFriends", "Your friends list is empty.");
	}
	else
	{
		FriendsLine = FriendsMode == EFriendsMode::Join ? LOCTEXT("FriendsJoin", "Friends - click a friend who is in a game to join:")
			: LOCTEXT("FriendsInvite", "Friends - click a friend to invite to this game:");
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleFriendRowClicked(const int32 RowIndex)
{
	SetStatus(FText::GetEmpty());
	UCKSessionsSubsystem* Sessions = GetSessions();
	if (Sessions == nullptr || !Sessions->GetFriends().IsValidIndex(RowIndex))
	{
		return;
	}
	const FCKSessionsFriend Friend = Sessions->GetFriends()[RowIndex];
	if (FriendsMode == EFriendsMode::Join)
	{
		Sessions->JoinFriend(Friend);
	}
	else if (FriendsMode == EFriendsMode::Invite)
	{
		const ECKSessionsFailure Failure = Sessions->InviteFriend(Friend);
		SetStatus(Failure == ECKSessionsFailure::None ? FText::Format(LOCTEXT("InviteSent", "Invite sent to {0}."), FText::FromString(Friend.DisplayName))
			: FText::Format(LOCTEXT("InviteFailed", "Could not invite {0}: {1}"), FText::FromString(Friend.DisplayName), CKSessions::Demo::FailureText(Failure)));
	}
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleHostComplete(const bool bSuccess, const ECKSessionsFailure Failure)
{
	if (!bSuccess)
	{
		SetStatus(FText::Format(LOCTEXT("HostFailed", "Could not host: {0}"), CKSessions::Demo::FailureText(Failure)));
	}
}

void UCKSessionsDemoPanelWidget::HandleFindComplete(const bool bSuccess, const TArray<FCKSessionsSearchResult>& Results)
{
	bSearchDone = true;
	SetStatus(bSuccess ? FText::GetEmpty() : LOCTEXT("FindFailed", "The search failed. Click Find games to try again."));
	Refresh();
}

void UCKSessionsDemoPanelWidget::HandleJoinComplete(const bool bSuccess, const ECKSessionsFailure Failure)
{
	if (!bSuccess)
	{
		SetStatus(FText::Format(LOCTEXT("JoinFailed", "Could not join: {0}"), CKSessions::Demo::FailureText(Failure)));
	}
}

void UCKSessionsDemoPanelWidget::HandleSessionFailure(const ECKSessionsFailure Failure, const FString& Message)
{
	SetStatus(FText::Format(LOCTEXT("SessionLost", "The session is lost: {0}"), CKSessions::Demo::FailureText(Failure)));
}

#undef LOCTEXT_NAMESPACE
