// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CKSessionsTypes.h"
#include "Components/Button.h"
#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "CKSessionsDemoPanelWidget.generated.h"

class ACKSessionsDemoHUD;
class UCKSessionsSubsystem;
class UPanelWidget;
class UTextBlock;

/** Names of the panel's widgets: the automation tests find the buttons by these names and click them. */
namespace CKSessions::Demo::WidgetNames
{
	inline constexpr const TCHAR* SessionText = TEXT("SessionText");
	inline constexpr const TCHAR* LobbyText = TEXT("LobbyText");
	inline constexpr const TCHAR* PlayersText = TEXT("PlayersText");
	inline constexpr const TCHAR* HintText = TEXT("HintText");
	inline constexpr const TCHAR* SetupText = TEXT("SetupText");
	inline constexpr const TCHAR* ResultsText = TEXT("ResultsText");
	inline constexpr const TCHAR* StatusText = TEXT("StatusText");
	inline constexpr const TCHAR* PlayWindowsButton = TEXT("PlayWindowsButton");
	inline constexpr const TCHAR* HostButton = TEXT("HostButton");
	inline constexpr const TCHAR* FindButton = TEXT("FindButton");
	inline constexpr const TCHAR* ReadyButton = TEXT("ReadyButton");
	inline constexpr const TCHAR* BackToLobbyButton = TEXT("BackToLobbyButton");
	inline constexpr const TCHAR* LeaveButton = TEXT("LeaveButton");
	inline constexpr const TCHAR* FriendsButton = TEXT("FriendsButton");
	inline constexpr const TCHAR* InviteButton = TEXT("InviteButton");
	inline constexpr const TCHAR* FriendsText = TEXT("FriendsText");

	/** Prefix of the friend rows (Join or Invite, one per friend): FriendButton_0, FriendButton_1, ... */
	inline constexpr const TCHAR* FriendButtonPrefix = TEXT("FriendButton_");

	/** Friends the panel lists. */
	inline constexpr int32 MaxFriendButtons = 6;

	/** Prefix of the Join buttons, one per found game: JoinButton_0, JoinButton_1, ... */
	inline constexpr const TCHAR* JoinButtonPrefix = TEXT("JoinButton_");

	/** Found games the panel lists. */
	inline constexpr int32 MaxJoinButtons = 6;
}

DECLARE_DELEGATE_OneParam(FCKSessionsDemoRowClicked, int32 /*RowIndex*/);

/**
 * One Join button of the list of found games. A dynamic On Clicked delegate carries no sender, so each row knows its index.
 */
UCLASS()
class UCKSessionsDemoRowButton final : public UButton
{
	GENERATED_BODY()

public:
	/** Binds the row to its index and to the panel's handler; call once after the row was constructed. */
	void Setup(int32 InRowIndex, const FCKSessionsDemoRowClicked& InOnRowClicked);

private:
	UFUNCTION()
	void HandleClicked();

	int32 RowIndex = INDEX_NONE;
	FCKSessionsDemoRowClicked OnRowClicked;
};

/**
 * Panel of the CoopKit Sessions demo (ACKSessionsDemoHUD puts it on the screen). Its widget tree is built in C++, so it
 * needs no Widget Blueprint. Every button calls the plugin's Blueprint API:
 * - Host game: Host Session on the current map (a standalone game reopens it as a listen server);
 * - Find games: Find Sessions; every result gets a Join button (Join Session);
 * - Ready / Not ready: Set Ready of the local player's CKSessions Player Component;
 * - Back to lobby (match map, host only): End Match, then a server travel of every player to the lobby map;
 * - Leave: Leave Session To Map, back to the lobby map alone;
 * - Friends (no session): Read Friends; every friend gets a row, Join Friend for those in a joinable game;
 * - Invite friends (in a session): Show Invite UI (Steam overlay); where the platform has no overlay (EOS), Read Friends
 *   with an Invite row per friend (Invite Friend). With Null the panel says that friends need Steam or EOS;
 * - an accepted invite joins at once: the panel sets Invite Accept Policy to Join At Once when it appears;
 * - Play with 2 windows (Play In Editor with fewer windows than the demo needs): restarts Play In Editor.
 * The texts show the live state: the session, the lobby phase and countdown, the players and what to do next. They are
 * refreshed on a world timer, not on the widget's tick: a widget ticks only while Slate paints it, and automation tests
 * run without rendering.
 */
UCLASS()
class UCKSessionsDemoPanelWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The HUD that owns the panel: lobby map and number of windows of the demo. */
	void SetDemoHUD(ACKSessionsDemoHUD* InDemoHUD);

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();
	UTextBlock* AddText(UPanelWidget* Parent, const TCHAR* Name, int32 FontSize, const FLinearColor& Color);
	UButton* AddButton(UPanelWidget* Parent, UClass* ButtonClass, const FString& Name, const FText& Label);

	void Refresh();
	void RefreshResults(const UCKSessionsSubsystem& Sessions, bool bShow);
	void SetStatus(const FText& InText);
	UCKSessionsSubsystem* GetSessions() const;

	/** Play In Editor windows of this editor (dedicated servers excluded); INDEX_NONE outside Play In Editor. */
	int32 CountPlayWindows() const;
	int32 GetNeededWindows() const;

	UFUNCTION()
	void HandlePlayWindowsClicked();

	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleFindClicked();

	UFUNCTION()
	void HandleReadyClicked();

	UFUNCTION()
	void HandleBackToLobbyClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	UFUNCTION()
	void HandleFriendsClicked();

	UFUNCTION()
	void HandleInviteClicked();

	void HandleJoinRowClicked(int32 RowIndex);
	void HandleFriendRowClicked(int32 RowIndex);
	void RefreshFriends(const UCKSessionsSubsystem& Sessions, bool bIdleMenu, bool bInSessionNow);

	UFUNCTION()
	void HandleHostComplete(bool bSuccess, ECKSessionsFailure Failure);

	UFUNCTION()
	void HandleFindComplete(bool bSuccess, const TArray<FCKSessionsSearchResult>& Results);

	UFUNCTION()
	void HandleJoinComplete(bool bSuccess, ECKSessionsFailure Failure);

	UFUNCTION()
	void HandleSessionFailure(ECKSessionsFailure Failure, const FString& Message);

	UFUNCTION()
	void HandleReadFriendsComplete(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsFriend>& Friends);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SessionText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LobbyText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayersText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SetupText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ResultsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> PlayWindowsButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> HostButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> FindButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> JoinButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ReadyButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BackToLobbyButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> FriendsButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> InviteButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FriendsText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> FriendButtons;

	TWeakObjectPtr<ACKSessionsDemoHUD> DemoHUD;

	/** What the friends section shows: nothing, friends to join (no session) or friends to invite (in a session). */
	enum class EFriendsMode : uint8
	{
		Hidden,
		Join,
		Invite
	};

	EFriendsMode FriendsMode = EFriendsMode::Hidden;

	/** Answer of the last Friends or Invite friends click: the list heading, or why there is no list. */
	FText FriendsLine;

	/** True once this panel saw a search end: the list of found games (or "none found") is shown. */
	bool bSearchDone = false;

	FTimerHandle RefreshTimer;
};
