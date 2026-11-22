// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsSubsystem.h"
#include "CKSessionsDiagnostics.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsSettings.h"
#include "CoopKitSessionsModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineReplStructs.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameMapsSettings.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Net/DataChannel.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"
#include "UObject/Package.h"

namespace CKSessions::Private
{
	/** The first local player hosts, searches and joins. Split-screen guests share the session of the first player. */
	constexpr int32 LocalUserNum = 0;

	/** Wall-clock limit for opening the host map as a listen server and for connecting to a host, in seconds. */
	constexpr double TravelTimeoutSeconds = 30.0;

	/** Seconds after the game instance starts until the setup check runs: the first world and the online subsystem exist by then. */
	constexpr float SetupCheckDelaySeconds = 1.0f;

	/**
	 * How long an accepted invite may wait for the current host, search, join or leave to end, or for the engine to finish
	 * a travel, before it is dropped with On Join Complete (Busy): two travel time limits.
	 */
	constexpr double InviteWaitSeconds = 2.0 * TravelTimeoutSeconds;

	const TCHAR* InviteActionName(const CKSessions::Online::EInviteAction Action)
	{
		switch (Action)
		{
		case CKSessions::Online::EInviteAction::Join:
			return TEXT("joining it");
		case CKSessions::Online::EInviteAction::EventOnly:
			return TEXT("On Invite Accepted only");
		default:
			return TEXT("ignored");
		}
	}

	/** Long package name of the world's map without the Play In Editor prefix. */
	FString GetMapPackageName(const UWorld& World)
	{
		return UWorld::RemovePIEPrefix(World.GetPackage()->GetName());
	}

	FString GetPlayerId(const APlayerState& PlayerState)
	{
		const FUniqueNetIdRepl& UniqueId = PlayerState.GetUniqueId();
		return UniqueId.IsValid() ? UniqueId.ToString() : PlayerState.GetPlayerName();
	}

	ECKSessionsFailure ToFailure(const EOnJoinSessionCompleteResult::Type Result)
	{
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			return ECKSessionsFailure::SessionFull;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			return ECKSessionsFailure::SessionGone;
		case EOnJoinSessionCompleteResult::Success:
			return ECKSessionsFailure::None;
		default:
			return ECKSessionsFailure::JoinFailed;
		}
	}
}

UCKSessionsSubsystem* UCKSessionsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
}

void UCKSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	InvitePolicy = UCKSessionsSettings::Get().GetInviteAcceptPolicy();
	// Subscribe at once: Steam hands over the invite a game was started with (+connect_lobby) only once someone listens.
	// Without a world or an online subsystem yet, Tick subscribes as soon as they exist.
	EnsureInviteSubscription();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCKSessionsSubsystem::Tick));
	// A moment later the first world exists and the online subsystem has started (or has fallen back): check the setup once.
	SetupCheckHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		CheckSetup();
		return false;
	}), CKSessions::Private::SetupCheckDelaySeconds);
	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UCKSessionsSubsystem::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UCKSessionsSubsystem::OnTravelFailure);
	}
	PreLoginHandle = FGameModeEvents::OnGameModePreLoginEvent().AddUObject(this, &UCKSessionsSubsystem::OnGameModePreLogin);
	PostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &UCKSessionsSubsystem::OnGameModePostLogin);
	LogoutHandle = FGameModeEvents::OnGameModeLogoutEvent().AddUObject(this, &UCKSessionsSubsystem::OnGameModeLogout);
}

void UCKSessionsSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(SetupCheckHandle);
	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	FGameModeEvents::OnGameModePreLoginEvent().Remove(PreLoginHandle);
	FGameModeEvents::OnGameModePostLoginEvent().Remove(PostLoginHandle);
	FGameModeEvents::OnGameModeLogoutEvent().Remove(LogoutHandle);

	ClearInviteSubscription();
	bInviteJoinPending = false;

	// The world may already be gone: use the interface this game instance worked with, never the default instance.
	if (const IOnlineSessionPtr Sessions = CachedSessions.Pin())
	{
		Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(CKSessions::Private::LocalUserNum, FindFriendCompleteHandle);
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(BackgroundFindCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
		if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
		{
			Sessions->DestroySession(NAME_GameSession);
		}
	}
	AfterDestroy = nullptr;
	Super::Deinitialize();
}

TArray<FCKSessionsFinding> UCKSessionsSubsystem::CheckSetup()
{
	const CKSessions::Diagnostics::FSnapshot Snapshot = CKSessions::Diagnostics::GatherSnapshot(GetWorld());
	SetupFindings = CKSessions::Diagnostics::Evaluate(Snapshot);
	if (SetupFindings.IsEmpty())
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: setup check found no problems (service %s, subsystem %s)."), *GetNameSafe(GetGameInstance()),
			Snapshot.DefaultPlatformService.IsEmpty() ? TEXT("Null") : *Snapshot.DefaultPlatformService, *Snapshot.LoadedSubsystem.ToString());
	}
	for (const FCKSessionsFinding& Finding : SetupFindings)
	{
		const FString Line = CKSessions::Diagnostics::FormatFinding(Finding);
		switch (Finding.Severity)
		{
		case ECKSessionsFindingSeverity::Error:
			UE_LOG(LogCKSessions, Error, TEXT("Setup check: %s"), *Line);
			break;
		case ECKSessionsFindingSeverity::Warning:
			UE_LOG(LogCKSessions, Warning, TEXT("Setup check: %s"), *Line);
			break;
		default:
			UE_LOG(LogCKSessions, Log, TEXT("Setup check: %s"), *Line);
			break;
		}
	}
	return SetupFindings;
}

void UCKSessionsSubsystem::EnsureInviteSubscription()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || (InviteAcceptedHandle.IsValid() && InviteSessions.Pin() == Sessions))
	{
		return;
	}
	ClearInviteSubscription();
	InviteAcceptedHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnSessionUserInviteAccepted));
	InviteSessions = Sessions;
	UE_LOG(LogCKSessions, Verbose, TEXT("%s: listening for accepted invites (%s)."), *GetNameSafe(GetGameInstance()), *GetOnlineSubsystemName().ToString());
}

void UCKSessionsSubsystem::ClearInviteSubscription()
{
	if (const IOnlineSessionPtr Sessions = InviteSessions.Pin())
	{
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
	}
	InviteAcceptedHandle.Reset();
	InviteSessions.Reset();
}

bool UCKSessionsSubsystem::IsOwnSessionType(const FOnlineSessionSearchResult& Result) const
{
	// Any Session Type this game hosts with: the default profile's and the one of the last request.
	TArray<FName> Types;
	if (const UCKSessionsProfileData* DefaultProfile = UCKSessionsSettings::Get().LoadDefaultProfile())
	{
		Types.AddUnique(DefaultProfile->GetSessionType());
	}
	if (const UCKSessionsProfileData* Profile = GetActiveProfile())
	{
		Types.AddUnique(Profile->GetSessionType());
	}
	return CKSessions::Online::IsSessionOfType(Result.Session.SessionSettings, Types);
}

void UCKSessionsSubsystem::OnSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	const UWorld* World = GetWorld();
	const bool bUsable = bWasSuccessful && InviteResult.IsValid();
	const bool bOwnType = InviteResult.IsValid() && IsOwnSessionType(InviteResult);
	const CKSessions::Online::EInviteAction Action = CKSessions::Online::DecideInviteAction(bUsable, bOwnType, InvitePolicy, bHandleHostLossExternally,
		World != nullptr && World->GetNetMode() == NM_DedicatedServer);
	UE_LOG(LogCKSessions, Log, TEXT("%s: invite accepted (success %d, controller %d) to session %s of type '%s': %s."), *GetNameSafe(GetGameInstance()),
		bWasSuccessful ? 1 : 0, ControllerId, InviteResult.IsValid() ? *InviteResult.GetSessionIdStr() : TEXT("<none>"),
		*CKSessions::Online::GetSessionType(InviteResult.Session.SessionSettings), CKSessions::Private::InviteActionName(Action));
	if (Action == CKSessions::Online::EInviteAction::Ignore)
	{
		return;
	}

	FCKSessionsInvite Invite;
	Invite.Session = FCKSessionsSearchResult(InviteResult);
	Invite.SessionId = InviteResult.GetSessionIdStr();
	Invite.Attributes = CKSessions::Online::ReadAttributes(InviteResult.Session.SessionSettings);
	Invite.bJoining = Action == CKSessions::Online::EInviteAction::Join;
	if (Invite.bJoining)
	{
		QueueInviteJoin(Invite.Session);
	}
	OnInviteAccepted.Broadcast(Invite);
	if (bInviteJoinPending)
	{
		TryStartInviteJoin(FPlatformTime::Seconds());
	}
}

void UCKSessionsSubsystem::SetInviteAcceptPolicy(const ECKSessionsInvitePolicy Policy)
{
	if (InvitePolicy != Policy)
	{
		InvitePolicy = Policy;
		UE_LOG(LogCKSessions, Log, TEXT("%s: invite accept policy %s."), *GetNameSafe(GetGameInstance()), *UEnum::GetValueAsString(Policy));
	}
}

bool UCKSessionsSubsystem::JoinInvitedSession(const FCKSessionsSearchResult& Session)
{
	if (!Session.IsValid())
	{
		LastFailure = ECKSessionsFailure::InvalidResult;
		OnJoinComplete.Broadcast(false, LastFailure);
		return false;
	}
	const UWorld* World = GetWorld();
	if (World != nullptr && World->GetNetMode() == NM_DedicatedServer)
	{
		OnJoinComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}
	QueueInviteJoin(Session);
	TryStartInviteJoin(FPlatformTime::Seconds());
	return true;
}

void UCKSessionsSubsystem::QueueInviteJoin(const FCKSessionsSearchResult& Session)
{
	if (bInviteJoinPending)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: invite to session %s replaces the waiting invite to %s."), *GetNameSafe(GetGameInstance()),
			*Session.GetSessionId(), *PendingInvite.GetSessionId());
	}
	PendingInvite = Session;
	bInviteJoinPending = true;
	InviteQueuedSeconds = FPlatformTime::Seconds();
	// A quick play in flight would join or host elsewhere once its search ends.
	FinishQuickPlay(false, false, ECKSessionsFailure::Busy);
}

void UCKSessionsSubsystem::TryStartInviteJoin(const double Now)
{
	if (!bInviteJoinPending)
	{
		return;
	}
	if (Now - InviteQueuedSeconds > CKSessions::Private::InviteWaitSeconds)
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: the invite to session %s waited %.0f s for the current request to end and is dropped."),
			*GetNameSafe(GetGameInstance()), *PendingInvite.GetSessionId(), CKSessions::Private::InviteWaitSeconds);
		bInviteJoinPending = false;
		PendingInvite = FCKSessionsSearchResult();
		LastFailure = ECKSessionsFailure::Busy;
		OnJoinComplete.Broadcast(false, LastFailure);
		return;
	}

	// A host, search, join or leave in flight ends first; a session or a reconnection is left right away.
	const bool bSettled = State == ECKSessionsState::Idle || State == ECKSessionsState::InSession || State == ECKSessionsState::Reconnecting;
	if (!bSettled || bHostLossPending)
	{
		return;
	}
	// The join travels the local player: wait until the engine has a player and no travel of its own is queued (a client
	// that lost its host is travelled to the default map first). A reconnection's own pending travel is replaced.
	UWorld* World = GetWorld();
	const FWorldContext* Context = World != nullptr && GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (Context == nullptr || !World->HasBegunPlay() || !Context->TravelURL.IsEmpty()
		|| (State != ECKSessionsState::Reconnecting && Context->PendingNetGame != nullptr)
		|| GetGameInstance()->GetFirstLocalPlayerController(World) == nullptr)
	{
		return;
	}

	const FCKSessionsSearchResult Target = PendingInvite;
	bInviteJoinPending = false;
	PendingInvite = FCKSessionsSearchResult();
	UE_LOG(LogCKSessions, Log, TEXT("%s: joining invited session %s (was %s)."), *GetNameSafe(GetGameInstance()), *Target.GetSessionId(), *UEnum::GetValueAsString(State));
	if (State == ECKSessionsState::Idle)
	{
		JoinSession(Target);
		return;
	}

	// Leave the current session, or stop reconnecting, without the menu map: the join travels straight to the new host.
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		if (FindCompleteHandle.IsValid())
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
			Sessions->CancelFindSessions();
		}
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}
	FindCompleteHandle.Reset();
	JoinCompleteHandle.Reset();
	ActiveSearch.Reset();
	// A search waiting for the background search is dropped like a running one; the background search goes on.
	bMainSearchWaiting = false;
	bWaitingForConnection = false;
	bWaitingForListenServer = false;
	ReconnectStep = EReconnectStep::Waiting;
	ReconnectAttemptsLeft = 0;
	SetState(ECKSessionsState::Leaving);
	DestroySessionThen([this, Target]()
	{
		bIsHost = false;
		bMatchInProgress = false;
		RuntimeAttributes.Reset();
		bAttributesChangedWhileHosting = false;
		LastSessionId.Reset();
		LastConnectString.Reset();
		PlayerRecords.Reset();
		BannedPlayers.Reset();
		LastFailure = ECKSessionsFailure::None;
		SetState(ECKSessionsState::Idle);
		JoinSession(Target);
	});
}

ECKSessionsFailure UCKSessionsSubsystem::ShowInviteUI()
{
	IOnlineSubsystem* OnlineSubsystem = GetOnlineSubsystem();
	if (OnlineSubsystem == nullptr)
	{
		return ECKSessionsFailure::NoOnlineSubsystem;
	}
	const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem->GetExternalUIInterface();
	if (OnlineSubsystem->GetSubsystemName() == CKSessions::Online::NullSubsystemName || !ExternalUI.IsValid())
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Show Invite UI: %s has no invite overlay; friends and invites need Steam or EOS."),
			*GetNameSafe(GetGameInstance()), *OnlineSubsystem->GetSubsystemName().ToString());
		return ECKSessionsFailure::NotSupported;
	}
	const IOnlineSessionPtr Sessions = GetSessions();
	if (State != ECKSessionsState::InSession || !Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Show Invite UI: host or join a session first."), *GetNameSafe(GetGameInstance()));
		return ECKSessionsFailure::Busy;
	}
	if (!ExternalUI->ShowInviteUI(CKSessions::Private::LocalUserNum, NAME_GameSession))
	{
		// EOS (5.4-5.8) does not implement the invite overlay.
		UE_LOG(LogCKSessions, Log, TEXT("%s: Show Invite UI: %s did not open an invite overlay; use Read Friends and Invite Friend."),
			*GetNameSafe(GetGameInstance()), *OnlineSubsystem->GetSubsystemName().ToString());
		return ECKSessionsFailure::NotSupported;
	}
	return ECKSessionsFailure::None;
}

bool UCKSessionsSubsystem::ReadFriends()
{
	if (bReadingFriends)
	{
		OnReadFriendsComplete.Broadcast(false, ECKSessionsFailure::Busy, Friends);
		return false;
	}
	IOnlineSubsystem* OnlineSubsystem = GetOnlineSubsystem();
	if (OnlineSubsystem == nullptr)
	{
		FinishReadFriends(false, ECKSessionsFailure::NoOnlineSubsystem);
		return false;
	}
	const IOnlineFriendsPtr FriendsInterface = OnlineSubsystem->GetFriendsInterface();
	if (OnlineSubsystem->GetSubsystemName() == CKSessions::Online::NullSubsystemName || !FriendsInterface.IsValid())
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Read Friends: %s has no friends list; friends and invites need Steam or EOS."),
			*GetNameSafe(GetGameInstance()), *OnlineSubsystem->GetSubsystemName().ToString());
		FinishReadFriends(false, ECKSessionsFailure::NotSupported);
		return false;
	}
	bReadingFriends = true;
	const FString ListName = EFriendsLists::ToString(EFriendsLists::Default);
	if (!FriendsInterface->ReadFriendsList(CKSessions::Private::LocalUserNum, ListName,
		FOnReadFriendsListComplete::CreateUObject(this, &UCKSessionsSubsystem::OnReadFriendsListComplete)) && bReadingFriends)
	{
		// Refused without calling the completion delegate (it clears the flag when it runs).
		bReadingFriends = false;
		UE_LOG(LogCKSessions, Warning, TEXT("%s: Read Friends: %s refused the request; is the player logged in?"), *GetNameSafe(GetGameInstance()),
			*OnlineSubsystem->GetSubsystemName().ToString());
		FinishReadFriends(false, ECKSessionsFailure::FriendsFailed);
		return false;
	}
	return true;
}

void UCKSessionsSubsystem::OnReadFriendsListComplete(const int32 InLocalUserNum, const bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
{
	if (!bReadingFriends)
	{
		return;
	}
	bReadingFriends = false;
	const IOnlineSubsystem* OnlineSubsystem = GetOnlineSubsystem();
	const IOnlineFriendsPtr FriendsInterface = OnlineSubsystem != nullptr ? OnlineSubsystem->GetFriendsInterface() : nullptr;
	TArray<TSharedRef<FOnlineFriend>> List;
	if (!bWasSuccessful || !FriendsInterface.IsValid() || !FriendsInterface->GetFriendsList(InLocalUserNum, ListName, List))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: Read Friends failed: %s"), *GetNameSafe(GetGameInstance()), *ErrorStr);
		FinishReadFriends(false, ECKSessionsFailure::FriendsFailed);
		return;
	}

	Friends.Reset(List.Num());
	for (const TSharedRef<FOnlineFriend>& Entry : List)
	{
		const FOnlineUserPresence& Presence = Entry->GetPresence();
		FCKSessionsFriend& Friend = Friends.AddDefaulted_GetRef();
		Friend.UniqueId = FUniqueNetIdRepl(Entry->GetUserId());
		Friend.DisplayName = Entry->GetDisplayName();
		Friend.bOnline = Presence.bIsOnline;
		Friend.bPlayingThisGame = Presence.bIsPlayingThisGame;
		Friend.bJoinable = Presence.bIsJoinable;
	}
	CKSessions::Online::SortFriends(Friends);
	FinishReadFriends(true, ECKSessionsFailure::None);
}

void UCKSessionsSubsystem::FinishReadFriends(const bool bSuccess, const ECKSessionsFailure Failure)
{
	UE_LOG(LogCKSessions, Log, TEXT("%s: Read Friends %s, %d friends."), *GetNameSafe(GetGameInstance()),
		bSuccess ? TEXT("succeeded") : *UEnum::GetValueAsString(Failure), Friends.Num());
	OnReadFriendsComplete.Broadcast(bSuccess, Failure, Friends);
}

ECKSessionsFailure UCKSessionsSubsystem::InviteFriend(const FCKSessionsFriend& Friend)
{
	IOnlineSubsystem* OnlineSubsystem = GetOnlineSubsystem();
	const IOnlineSessionPtr Sessions = GetSessions();
	if (OnlineSubsystem == nullptr || !Sessions.IsValid())
	{
		return ECKSessionsFailure::NoOnlineSubsystem;
	}
	if (OnlineSubsystem->GetSubsystemName() == CKSessions::Online::NullSubsystemName)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Invite Friend: Null has no friends; friends and invites need Steam or EOS."), *GetNameSafe(GetGameInstance()));
		return ECKSessionsFailure::NotSupported;
	}
	if (!Friend.UniqueId.IsValid())
	{
		return ECKSessionsFailure::InvalidResult;
	}
	if (State != ECKSessionsState::InSession || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Invite Friend: host or join a session first."), *GetNameSafe(GetGameInstance()));
		return ECKSessionsFailure::Busy;
	}
	if (!Sessions->SendSessionInviteToFriend(CKSessions::Private::LocalUserNum, NAME_GameSession, *Friend.UniqueId))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: Invite Friend: %s refused to invite %s."), *GetNameSafe(GetGameInstance()),
			*OnlineSubsystem->GetSubsystemName().ToString(), *Friend.DisplayName);
		return ECKSessionsFailure::InviteFailed;
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: invited %s."), *GetNameSafe(GetGameInstance()), *Friend.DisplayName);
	return ECKSessionsFailure::None;
}

bool UCKSessionsSubsystem::JoinFriend(const FCKSessionsFriend& Friend)
{
	if (State != ECKSessionsState::Idle)
	{
		OnJoinComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}
	IOnlineSubsystem* OnlineSubsystem = GetOnlineSubsystem();
	const IOnlineSessionPtr Sessions = GetSessions();
	ECKSessionsFailure Refusal = ECKSessionsFailure::None;
	if (OnlineSubsystem == nullptr || !Sessions.IsValid())
	{
		Refusal = ECKSessionsFailure::NoOnlineSubsystem;
	}
	else if (OnlineSubsystem->GetSubsystemName() == CKSessions::Online::NullSubsystemName)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: Join Friend: Null has no friends; friends and invites need Steam or EOS."), *GetNameSafe(GetGameInstance()));
		Refusal = ECKSessionsFailure::NotSupported;
	}
	else if (!Friend.UniqueId.IsValid())
	{
		Refusal = ECKSessionsFailure::InvalidResult;
	}
	if (Refusal != ECKSessionsFailure::None)
	{
		LastFailure = Refusal;
		OnJoinComplete.Broadcast(false, LastFailure);
		return false;
	}

	UE_LOG(LogCKSessions, Log, TEXT("%s: looking for the game session of %s."), *GetNameSafe(GetGameInstance()), *Friend.DisplayName);
	PendingJoin = FCKSessionsSearchResult();
	bIsHost = false;
	SetState(ECKSessionsState::Joining);
	FindFriendCompleteHandle = Sessions->AddOnFindFriendSessionCompleteDelegate_Handle(CKSessions::Private::LocalUserNum,
		FOnFindFriendSessionCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnFindFriendSessionComplete));
	if (!Sessions->FindFriendSession(CKSessions::Private::LocalUserNum, *Friend.UniqueId) && FindFriendCompleteHandle.IsValid())
	{
		// Refused without calling the completion delegate (it resets the handle when it runs).
		Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(CKSessions::Private::LocalUserNum, FindFriendCompleteHandle);
		FindFriendCompleteHandle.Reset();
		FinishJoin(false, ECKSessionsFailure::SessionGone);
		return false;
	}
	return true;
}

void UCKSessionsSubsystem::OnFindFriendSessionComplete(const int32 InLocalUserNum, const bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& Results)
{
	if (!FindFriendCompleteHandle.IsValid())
	{
		return;
	}
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(CKSessions::Private::LocalUserNum, FindFriendCompleteHandle);
	}
	FindFriendCompleteHandle.Reset();
	if (State != ECKSessionsState::Joining)
	{
		return;
	}
	// The friend may sit in a party lobby: only a game session of this game is joined.
	const FOnlineSessionSearchResult* Found = bWasSuccessful ? Results.FindByPredicate([this](const FOnlineSessionSearchResult& Result)
	{
		return Result.IsValid() && IsOwnSessionType(Result);
	}) : nullptr;
	if (Found == nullptr)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: the friend is in no game session of this game (%d results)."), *GetNameSafe(GetGameInstance()), Results.Num());
		FinishJoin(false, ECKSessionsFailure::SessionGone);
		return;
	}
	PendingJoin = FCKSessionsSearchResult(*Found);
	StartJoin();
}

IOnlineSessionPtr UCKSessionsSubsystem::GetSessions() const
{
	// Online::GetSubsystem picks the instance of this game instance's world (one per Play In Editor instance).
	if (const UWorld* World = GetWorld())
	{
		if (const IOnlineSubsystem* Subsystem = Online::GetSubsystem(World))
		{
			const IOnlineSessionPtr Sessions = Subsystem->GetSessionInterface();
			CachedSessions = Sessions;
			return Sessions;
		}
	}
	return CachedSessions.Pin();
}

IOnlineSubsystem* UCKSessionsSubsystem::GetOnlineSubsystem() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? Online::GetSubsystem(World) : nullptr;
}

FName UCKSessionsSubsystem::GetOnlineSubsystemName() const
{
	const IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	return Subsystem != nullptr ? Subsystem->GetSubsystemName() : NAME_None;
}

const UCKSessionsProfileData* UCKSessionsSubsystem::ResolveProfile(const UCKSessionsProfileData* Profile) const
{
	return Profile != nullptr ? Profile : UCKSessionsSettings::Get().LoadDefaultProfile();
}

const UCKSessionsProfileData* UCKSessionsSubsystem::GetActiveProfile() const
{
	return ResolveProfile(ActiveProfile);
}

void UCKSessionsSubsystem::SetState(const ECKSessionsState NewState)
{
	if (State != NewState)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: %s -> %s"), *GetNameSafe(GetGameInstance()),
			*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState));
		State = NewState;
		OnStateChanged.Broadcast(NewState);
	}
}

bool UCKSessionsSubsystem::IsOwnWorld(const UWorld* World) const
{
	return World != nullptr && World->GetGameInstance() == GetGameInstance();
}

bool UCKSessionsSubsystem::Tick(const float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	UWorld* World = GetWorld();

	if (!InviteAcceptedHandle.IsValid() || !InviteSessions.IsValid())
	{
		EnsureInviteSubscription();
	}

	if (bWaitingForListenServer && State == ECKSessionsState::Hosting)
	{
		// The session is created only once the map runs as a listen server: the online subsystem takes the
		// advertised port from the world's net driver.
		if (World != nullptr && World->GetNetMode() == NM_ListenServer && World->HasBegunPlay())
		{
			bWaitingForListenServer = false;
			CreateHostedSession();
		}
		else if (Now - WaitStartedSeconds > CKSessions::Private::TravelTimeoutSeconds)
		{
			bWaitingForListenServer = false;
			FinishHost(false, ECKSessionsFailure::HostTravelFailed);
		}
	}

	if (bWaitingForConnection)
	{
		if (IsConnectedToHost())
		{
			bWaitingForConnection = false;
			if (State == ECKSessionsState::Reconnecting)
			{
				UE_LOG(LogCKSessions, Log, TEXT("%s: reconnected to session %s."), *GetNameSafe(GetGameInstance()), *LastSessionId);
				LastFailure = ECKSessionsFailure::None;
				SetState(ECKSessionsState::InSession);
			}
			else
			{
				FinishJoin(true, ECKSessionsFailure::None);
			}
		}
		else if (Now - WaitStartedSeconds > CKSessions::Private::TravelTimeoutSeconds)
		{
			bWaitingForConnection = false;
			if (State == ECKSessionsState::Reconnecting)
			{
				FailReconnectAttempt();
			}
			else
			{
				FinishJoin(false, ECKSessionsFailure::ConnectFailed);
			}
		}
	}

	if (State == ECKSessionsState::Reconnecting)
	{
		TickReconnect(Now);
	}

	if (bHostLossPending)
	{
		TickHostLoss(Now);
	}

	// A dedicated server hosts by itself once its map runs, if the project asks for it.
	if (!bDedicatedServerHostChecked && World != nullptr && World->GetNetMode() == NM_DedicatedServer && World->HasBegunPlay())
	{
		bDedicatedServerHostChecked = true;
		if (UCKSessionsSettings::Get().ShouldDedicatedServerHost() && State == ECKSessionsState::Idle)
		{
			HostSession(nullptr, TSoftObjectPtr<UWorld>());
		}
	}

	if (bInviteJoinPending)
	{
		TryStartInviteJoin(Now);
	}

	PurgeExpiredRecords(Now);
	return true;
}

bool UCKSessionsSubsystem::HostSession(const UCKSessionsProfileData* Profile, const TSoftObjectPtr<UWorld> Map)
{
	if (State != ECKSessionsState::Idle)
	{
		OnHostComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}
	UWorld* World = GetWorld();
	if (World == nullptr || !GetSessions().IsValid())
	{
		LastFailure = ECKSessionsFailure::NoOnlineSubsystem;
		OnHostComplete.Broadcast(false, LastFailure);
		return false;
	}
	if (World->GetNetMode() == NM_Client)
	{
		// A client must leave its session before hosting its own.
		OnHostComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}

	ActiveProfile = ResolveProfile(Profile);
	bIsHost = true;
	bMatchInProgress = false;
	PlayerRecords.Reset();
	BannedPlayers.Reset();
	SetState(ECKSessionsState::Hosting);

	const ENetMode NetMode = World->GetNetMode();
	if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer)
	{
		CreateHostedSession();
		return true;
	}

	const FString MapName = Map.IsNull() ? CKSessions::Private::GetMapPackageName(*World) : Map.GetLongPackageName();
	UE_LOG(LogCKSessions, Log, TEXT("%s: opening %s as a listen server to host."), *GetNameSafe(GetGameInstance()), *MapName);
	bWaitingForListenServer = true;
	WaitStartedSeconds = FPlatformTime::Seconds();
	UGameplayStatics::OpenLevel(World, FName(*MapName), true, TEXT("listen"));
	return true;
}

bool UCKSessionsSubsystem::HostSessionWithAttributes(const UCKSessionsProfileData* Profile, const TSoftObjectPtr<UWorld> Map, const TMap<FName, FString>& Attributes)
{
	FName InvalidKey;
	if (!CKSessions::Online::AreValidAttributeKeys(Attributes, InvalidKey))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: cannot host, attribute key '%s' is empty or reserved."), *GetNameSafe(GetGameInstance()), *InvalidKey.ToString());
		LastFailure = ECKSessionsFailure::InvalidAttribute;
		OnHostComplete.Broadcast(false, LastFailure);
		return false;
	}
	if (State != ECKSessionsState::Idle)
	{
		// Keep the attributes of a session in flight.
		OnHostComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}
	RuntimeAttributes = Attributes;
	return HostSession(Profile, Map);
}

bool UCKSessionsSubsystem::SetSessionAttribute(const FName Key, const FString& Value)
{
	if (!CKSessions::Online::IsValidAttributeKey(Key))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: Set Session Attribute: key '%s' is empty or reserved."), *GetNameSafe(GetGameInstance()), *Key.ToString());
		return false;
	}
	const bool bClientOfSession = !bIsHost
		&& (State == ECKSessionsState::Joining || State == ECKSessionsState::InSession || State == ECKSessionsState::Reconnecting);
	if (bClientOfSession)
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: Set Session Attribute: only the host advertises attributes."), *GetNameSafe(GetGameInstance()));
		return false;
	}

	RuntimeAttributes.Add(Key, Value);
	if (bIsHost && State == ECKSessionsState::InSession)
	{
		UpdateAdvertisedAttributes();
	}
	else if (State == ECKSessionsState::Hosting)
	{
		bAttributesChangedWhileHosting = true;
	}
	return true;
}

TMap<FName, FString> UCKSessionsSubsystem::GetSessionAttributes() const
{
	return CKSessions::Online::MergeAttributes(*GetActiveProfile(), RuntimeAttributes);
}

void UCKSessionsSubsystem::UpdateAdvertisedAttributes()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (Session == nullptr)
	{
		return;
	}
	FOnlineSessionSettings Settings = Session->SessionSettings;
	CKSessions::Online::ApplyAttributes(Settings, RuntimeAttributes);
	Sessions->UpdateSession(NAME_GameSession, Settings, true);
	UE_LOG(LogCKSessions, Log, TEXT("%s: session attributes updated (%d set at runtime)."), *GetNameSafe(GetGameInstance()), RuntimeAttributes.Num());
}

bool UCKSessionsSubsystem::QuickPlay(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, const TSoftObjectPtr<UWorld> Map)
{
	return StartQuickPlay(Profile, Attributes, Map, 1);
}

bool UCKSessionsSubsystem::QuickPlayForGroup(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, const TSoftObjectPtr<UWorld> Map, const int32 GroupSize)
{
	return StartQuickPlay(Profile, Attributes, Map, GroupSize);
}

bool UCKSessionsSubsystem::StartQuickPlay(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, const TSoftObjectPtr<UWorld> Map, const int32 GroupSize)
{
	FName InvalidKey;
	if (!CKSessions::Online::AreValidAttributeKeys(Attributes, InvalidKey))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: cannot quick play, attribute key '%s' is empty or reserved."), *GetNameSafe(GetGameInstance()), *InvalidKey.ToString());
		LastFailure = ECKSessionsFailure::InvalidAttribute;
		OnQuickPlayComplete.Broadcast(false, false, LastFailure);
		return false;
	}
	if (State != ECKSessionsState::Idle || bQuickPlay)
	{
		OnQuickPlayComplete.Broadcast(false, false, ECKSessionsFailure::Busy);
		return false;
	}

	bQuickPlay = true;
	bQuickPlayFellBack = false;
	QuickPlayAttributes = Attributes;
	QuickPlayMap = Map;
	QuickPlayGroupSize = FMath::Max(GroupSize, 1);
	UE_LOG(LogCKSessions, Log, TEXT("%s: quick play, searching (%d attributes, group of %d)."), *GetNameSafe(GetGameInstance()), Attributes.Num(), QuickPlayGroupSize);
	// A search that fails at once ends the quick play through FinishFind.
	return StartFind(Profile, Attributes, QuickPlayGroupSize);
}

void UCKSessionsSubsystem::ContinueQuickPlay(const bool bSearchSucceeded)
{
	if (!bSearchSucceeded)
	{
		FinishQuickPlay(false, false, LastFailure != ECKSessionsFailure::None ? LastFailure : ECKSessionsFailure::SearchFailed);
		return;
	}

	TArray<FOnlineSessionSearchResult> Candidates;
	Candidates.Reserve(SearchResults.Num());
	for (const FCKSessionsSearchResult& Result : SearchResults)
	{
		Candidates.Add(Result.GetNative());
	}
	const int32 Pick = CKSessions::Online::PickQuickPlayResultForGroup(Candidates, QuickPlayGroupSize);
	if (Pick == INDEX_NONE)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: quick play found no session, hosting."), *GetNameSafe(GetGameInstance()));
		if (!HostSessionWithAttributes(GetActiveProfile(), QuickPlayMap, QuickPlayAttributes))
		{
			FinishQuickPlay(false, true, LastFailure != ECKSessionsFailure::None ? LastFailure : ECKSessionsFailure::Busy);
		}
		return;
	}

	UE_LOG(LogCKSessions, Log, TEXT("%s: quick play joins session %s."), *GetNameSafe(GetGameInstance()), *SearchResults[Pick].GetSessionId());
	const FCKSessionsSearchResult Chosen = SearchResults[Pick];
	if (!JoinSession(Chosen))
	{
		FinishQuickPlay(false, false, LastFailure != ECKSessionsFailure::None ? LastFailure : ECKSessionsFailure::Busy);
	}
}

void UCKSessionsSubsystem::FinishQuickPlay(const bool bSuccess, const bool bHosted, const ECKSessionsFailure Failure)
{
	if (!bQuickPlay)
	{
		return;
	}
	bQuickPlay = false;
	QuickPlayAttributes.Reset();
	UE_LOG(LogCKSessions, Log, TEXT("%s: quick play %s: %s."), *GetNameSafe(GetGameInstance()), bSuccess ? TEXT("succeeded") : TEXT("failed"),
		bSuccess ? (bHosted ? TEXT("hosting") : TEXT("joined")) : *UEnum::GetValueAsString(Failure));
	OnQuickPlayComplete.Broadcast(bSuccess, bHosted, Failure);
}

void UCKSessionsSubsystem::CreateHostedSession()
{
	// A session left over from an earlier join or host would make CreateSession fail.
	DestroySessionThen([this]()
	{
		const IOnlineSessionPtr Sessions = GetSessions();
		UWorld* World = GetWorld();
		const UCKSessionsProfileData* Profile = GetActiveProfile();
		if (!Sessions.IsValid() || World == nullptr)
		{
			FinishHost(false, ECKSessionsFailure::NoOnlineSubsystem);
			return;
		}

		// The engine refuses logins above the game session's limit ("Server full"), so the profile's limit holds on every subsystem.
		if (AGameModeBase* GameMode = World->GetAuthGameMode(); GameMode != nullptr && GameMode->GameSession != nullptr)
		{
			GameMode->GameSession->MaxPlayers = Profile->GetMaxPlayers();
		}

		const bool bLan = CKSessions::Online::ShouldUseLan(Profile->GetNetworkScope(), GetOnlineSubsystemName());
		const CKSessions::Online::EHostKind HostKind = World->GetNetMode() == NM_DedicatedServer
			? CKSessions::Online::EHostKind::DedicatedServer : CKSessions::Online::EHostKind::ListenServer;
		FOnlineSessionSettings Settings;
		CKSessions::Online::BuildSessionSettings(*Profile, bLan, HostKind, CKSessions::Private::GetMapPackageName(*World), RuntimeAttributes, Settings);
		bAttributesChangedWhileHosting = false;

		CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnCreateSessionComplete));
		if (!Sessions->CreateSession(CKSessions::Private::LocalUserNum, NAME_GameSession, Settings) && CreateCompleteHandle.IsValid())
		{
			// Refused without calling the completion delegate (it resets the handle when it runs).
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
			CreateCompleteHandle.Reset();
			FinishHost(false, ECKSessionsFailure::CreateFailed);
		}
	});
}

void UCKSessionsSubsystem::OnCreateSessionComplete(const FName SessionName, const bool bSuccess)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
	CreateCompleteHandle.Reset();
	if (State == ECKSessionsState::Hosting)
	{
		FinishHost(bSuccess, bSuccess ? ECKSessionsFailure::None : ECKSessionsFailure::CreateFailed);
	}
}

void UCKSessionsSubsystem::FinishHost(const bool bSuccess, const ECKSessionsFailure Failure)
{
	LastFailure = Failure;
	if (bSuccess)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: hosting session (%s)."), *GetNameSafe(GetGameInstance()), *GetOnlineSubsystemName().ToString());
		SetState(ECKSessionsState::InSession);
		if (bAttributesChangedWhileHosting)
		{
			bAttributesChangedWhileHosting = false;
			UpdateAdvertisedAttributes();
		}
	}
	else
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: hosting failed: %s."), *GetNameSafe(GetGameInstance()), *UEnum::GetValueAsString(Failure));
		bIsHost = false;
		SetState(ECKSessionsState::Idle);
	}
	OnHostComplete.Broadcast(bSuccess, Failure);
	FinishQuickPlay(bSuccess, true, Failure);
}

bool UCKSessionsSubsystem::FindSessions(const UCKSessionsProfileData* Profile)
{
	return FindSessionsWithAttributes(Profile, TMap<FName, FString>());
}

bool UCKSessionsSubsystem::FindSessionsWithAttributes(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter)
{
	return StartFind(Profile, Filter, 1);
}

bool UCKSessionsSubsystem::FindSessionsForGroup(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter, const int32 GroupSize)
{
	return StartFind(Profile, Filter, GroupSize);
}

bool UCKSessionsSubsystem::StartFind(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter, const int32 GroupSize)
{
	FName InvalidKey;
	if (!CKSessions::Online::AreValidAttributeKeys(Filter, InvalidKey))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: cannot search, attribute key '%s' is empty or reserved."), *GetNameSafe(GetGameInstance()), *InvalidKey.ToString());
		LastFailure = ECKSessionsFailure::InvalidAttribute;
		OnFindComplete.Broadcast(false, SearchResults);
		return false;
	}
	if (State != ECKSessionsState::Idle)
	{
		OnFindComplete.Broadcast(false, SearchResults);
		return false;
	}
	SearchFilter = Filter;
	SearchGroupSize = FMath::Max(GroupSize, 1);
	ActiveProfile = ResolveProfile(Profile);
	SetState(ECKSessionsState::Searching);
	return StartSearch();
}

bool UCKSessionsSubsystem::StartSearch()
{
	if (BackgroundFindCompleteHandle.IsValid())
	{
		// The online subsystem runs one search at a time; this one starts when the background search ends.
		bMainSearchWaiting = true;
		return true;
	}
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		LastFailure = ECKSessionsFailure::NoOnlineSubsystem;
		FinishFind(false);
		return false;
	}

	const UCKSessionsProfileData* Profile = GetActiveProfile();
	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	CKSessions::Online::BuildSearch(*Profile, CKSessions::Online::ShouldUseLan(Profile->GetNetworkScope(), GetOnlineSubsystemName()), SearchFilter, *ActiveSearch);

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnFindSessionsComplete));
	if (!Sessions->FindSessions(CKSessions::Private::LocalUserNum, ActiveSearch.ToSharedRef()) && FindCompleteHandle.IsValid())
	{
		// Refused without calling the completion delegate (it resets the handle when it runs).
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		FindCompleteHandle.Reset();
		LastFailure = ECKSessionsFailure::SearchFailed;
		FinishFind(false);
		return false;
	}
	return true;
}

void UCKSessionsSubsystem::OnFindSessionsComplete(const bool bSuccess)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	}
	FindCompleteHandle.Reset();

	SearchResults.Reset();
	if (bSuccess && ActiveSearch.IsValid())
	{
		const UCKSessionsProfileData* Profile = GetActiveProfile();
		for (const FOnlineSessionSearchResult& Result : ActiveSearch->SearchResults)
		{
			// A reconnecting client looks for its own session even if it no longer takes newcomers.
			const bool bOwnSession = State == ECKSessionsState::Reconnecting && Result.GetSessionIdStr() == LastSessionId;
			// Null does not filter by query settings, so the attribute filter is checked here for every subsystem.
			// A group needs a slot for every member (Find Sessions For Group); a single player needs one, which IsJoinable checks.
			if (bOwnSession || (CKSessions::Online::IsJoinable(Result, *Profile)
				&& CKSessions::Online::GetOpenSlots(Result) >= SearchGroupSize
				&& CKSessions::Online::MatchesAttributes(Result.Session.SessionSettings, SearchFilter)))
			{
				SearchResults.Emplace(Result);
			}
		}
	}
	LastFailure = bSuccess ? ECKSessionsFailure::None : ECKSessionsFailure::SearchFailed;
	FinishFind(bSuccess);
}

void UCKSessionsSubsystem::FinishFind(const bool bSuccess)
{
	// The online subsystem is free: a background search that waited goes first, a search requested from the events below
	// waits for it.
	if (bBackgroundSearchWaiting)
	{
		bBackgroundSearchWaiting = false;
		StartBackgroundSearch();
	}

	if (State == ECKSessionsState::Reconnecting)
	{
		const FCKSessionsSearchResult* Own = SearchResults.FindByPredicate([this](const FCKSessionsSearchResult& Result)
		{
			return Result.GetSessionId() == LastSessionId;
		});
		if (Own != nullptr)
		{
			ReconnectStep = EReconnectStep::Joining;
			PendingJoin = *Own;
			StartJoin();
		}
		else
		{
			// Not advertised (private, or a service that hides it): go straight to the last host address.
			UE_LOG(LogCKSessions, Log, TEXT("%s: session %s not found, travelling to the last host address."), *GetNameSafe(GetGameInstance()), *LastSessionId);
			ReconnectStep = EReconnectStep::Joining;
			if (!TravelToHost(LastConnectString))
			{
				FailReconnectAttempt();
			}
		}
		return;
	}

	if (State == ECKSessionsState::Searching)
	{
		SetState(ECKSessionsState::Idle);
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: search finished, %d joinable sessions."), *GetNameSafe(GetGameInstance()), SearchResults.Num());
	OnFindComplete.Broadcast(bSuccess, SearchResults);
	if (bQuickPlay)
	{
		ContinueQuickPlay(bSuccess);
	}
}

bool UCKSessionsSubsystem::FindSessionsInBackground(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter)
{
	FName InvalidKey;
	if (!CKSessions::Online::AreValidAttributeKeys(Filter, InvalidKey))
	{
		// Refusals do not fire the event: its listeners may be waiting for the background search in flight.
		UE_LOG(LogCKSessions, Warning, TEXT("%s: cannot search in the background, attribute key '%s' is empty or reserved."), *GetNameSafe(GetGameInstance()), *InvalidKey.ToString());
		return false;
	}
	if (IsBackgroundSearchInFlight())
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: a background search is already in flight."), *GetNameSafe(GetGameInstance()));
		return false;
	}
	BackgroundProfile = ResolveProfile(Profile);
	BackgroundFilter = Filter;
	if (FindCompleteHandle.IsValid())
	{
		// The online subsystem runs one search at a time; this one starts when the main search ends (Finish Find).
		bBackgroundSearchWaiting = true;
		return true;
	}
	return StartBackgroundSearch();
}

bool UCKSessionsSubsystem::StartBackgroundSearch()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	const UCKSessionsProfileData* Profile = ResolveProfile(BackgroundProfile);
	if (!Sessions.IsValid() || Profile == nullptr)
	{
		FinishBackgroundFind(false, ECKSessionsFailure::NoOnlineSubsystem, TArray<FCKSessionsSearchResult>());
		ResumeWaitingSearch();
		return false;
	}

	BackgroundSearch = MakeShared<FOnlineSessionSearch>();
	CKSessions::Online::BuildSearch(*Profile, CKSessions::Online::ShouldUseLan(Profile->GetNetworkScope(), GetOnlineSubsystemName()), BackgroundFilter, *BackgroundSearch);
	BackgroundFindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnBackgroundFindSessionsComplete));
	if (!Sessions->FindSessions(CKSessions::Private::LocalUserNum, BackgroundSearch.ToSharedRef()) && BackgroundFindCompleteHandle.IsValid())
	{
		// Refused without calling the completion delegate (it resets the handle when it runs).
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(BackgroundFindCompleteHandle);
		BackgroundFindCompleteHandle.Reset();
		BackgroundSearch.Reset();
		FinishBackgroundFind(false, ECKSessionsFailure::SearchFailed, TArray<FCKSessionsSearchResult>());
		ResumeWaitingSearch();
		return false;
	}
	return true;
}

void UCKSessionsSubsystem::OnBackgroundFindSessionsComplete(const bool bSuccess)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(BackgroundFindCompleteHandle);
	}
	BackgroundFindCompleteHandle.Reset();

	TArray<FCKSessionsSearchResult> Results;
	const UCKSessionsProfileData* Profile = ResolveProfile(BackgroundProfile);
	if (bSuccess && BackgroundSearch.IsValid() && Profile != nullptr)
	{
		const TArray<FName> Types = { Profile->GetSessionType() };
		for (const FOnlineSessionSearchResult& Result : BackgroundSearch->SearchResults)
		{
			// Watching, not joining: full sessions and running matches stay. Null does not filter, so the type and the
			// attributes are checked here for every subsystem.
			if (Result.IsValid() && CKSessions::Online::IsSessionOfType(Result.Session.SessionSettings, Types)
				&& CKSessions::Online::MatchesAttributes(Result.Session.SessionSettings, BackgroundFilter))
			{
				Results.Emplace(Result);
			}
		}
	}
	BackgroundSearch.Reset();

	// The main search that waited takes the online subsystem first, so a background search requested from the event waits for it.
	ResumeWaitingSearch();
	UE_LOG(LogCKSessions, Log, TEXT("%s: background search finished, %d sessions."), *GetNameSafe(GetGameInstance()), Results.Num());
	FinishBackgroundFind(bSuccess, bSuccess ? ECKSessionsFailure::None : ECKSessionsFailure::SearchFailed, Results);
}

void UCKSessionsSubsystem::FinishBackgroundFind(const bool bSuccess, const ECKSessionsFailure Failure, const TArray<FCKSessionsSearchResult>& Results)
{
	OnBackgroundFindComplete.Broadcast(bSuccess, Failure, Results);
}

void UCKSessionsSubsystem::ResumeWaitingSearch()
{
	if (!bMainSearchWaiting)
	{
		return;
	}
	bMainSearchWaiting = false;
	if (State == ECKSessionsState::Searching || (State == ECKSessionsState::Reconnecting && ReconnectStep == EReconnectStep::Searching))
	{
		StartSearch();
		return;
	}
	// The request went away while it waited (Leave Session): it ends like a search that was left.
	OnFindComplete.Broadcast(false, SearchResults);
}

bool UCKSessionsSubsystem::JoinSession(const FCKSessionsSearchResult& Result)
{
	if (State != ECKSessionsState::Idle)
	{
		OnJoinComplete.Broadcast(false, ECKSessionsFailure::Busy);
		return false;
	}
	if (!Result.IsValid())
	{
		LastFailure = ECKSessionsFailure::InvalidResult;
		OnJoinComplete.Broadcast(false, LastFailure);
		return false;
	}
	PendingJoin = Result;
	bIsHost = false;
	SetState(ECKSessionsState::Joining);
	StartJoin();
	return true;
}

void UCKSessionsSubsystem::StartJoin()
{
	// A session left over from a lost connection would make JoinSession fail with AlreadyInSession.
	DestroySessionThen([this]()
	{
		const IOnlineSessionPtr Sessions = GetSessions();
		if (!Sessions.IsValid())
		{
			FinishJoin(false, ECKSessionsFailure::NoOnlineSubsystem);
			return;
		}
		JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnJoinSessionComplete));
		if (!Sessions->JoinSession(CKSessions::Private::LocalUserNum, NAME_GameSession, PendingJoin.GetNative()) && JoinCompleteHandle.IsValid())
		{
			// Some subsystems report a refused request only through the return value.
			Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
			JoinCompleteHandle.Reset();
			FinishJoin(false, ECKSessionsFailure::JoinFailed);
		}
	});
}

void UCKSessionsSubsystem::OnJoinSessionComplete(const FName SessionName, const EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}
	JoinCompleteHandle.Reset();

	FString ConnectString;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid() || !Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString))
	{
		const ECKSessionsFailure Failure = Result == EOnJoinSessionCompleteResult::Success ? ECKSessionsFailure::JoinFailed : CKSessions::Private::ToFailure(Result);
		UE_LOG(LogCKSessions, Warning, TEXT("%s: join failed: %s."), *GetNameSafe(GetGameInstance()), LexToString(Result));
		if (State == ECKSessionsState::Reconnecting)
		{
			FailReconnectAttempt();
		}
		else
		{
			FinishJoin(false, Failure);
		}
		return;
	}

	LastSessionId = PendingJoin.GetSessionId();
	LastConnectString = ConnectString;
	if (!TravelToHost(ConnectString))
	{
		if (State == ECKSessionsState::Reconnecting)
		{
			FailReconnectAttempt();
		}
		else
		{
			FinishJoin(false, ECKSessionsFailure::ConnectFailed);
		}
	}
}

bool UCKSessionsSubsystem::TravelToHost(const FString& ConnectString)
{
	APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController(GetWorld());
	if (Controller == nullptr || ConnectString.IsEmpty())
	{
		return false;
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: travelling to host %s."), *GetNameSafe(GetGameInstance()), *ConnectString);
	bWaitingForConnection = true;
	WaitStartedSeconds = FPlatformTime::Seconds();
	Controller->ClientTravel(ConnectString, TRAVEL_Absolute);
	return true;
}

bool UCKSessionsSubsystem::IsConnectedToHost() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() != NM_Client || World->GetGameState() == nullptr)
	{
		return false;
	}
	// While the travel is pending, the old world reports NM_Client too (UWorld derives the net mode from the pending
	// net game's URL, 5.4-5.8): only a world with a live server connection is the host's.
	const FWorldContext* Context = GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	const UNetDriver* NetDriver = World->GetNetDriver();
	if (Context == nullptr || Context->PendingNetGame != nullptr || !Context->TravelURL.IsEmpty()
		|| NetDriver == nullptr || NetDriver->ServerConnection == nullptr)
	{
		return false;
	}
	const APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController(World);
	return Controller != nullptr && Controller->PlayerState != nullptr;
}

void UCKSessionsSubsystem::FinishJoin(const bool bSuccess, const ECKSessionsFailure Failure)
{
	LastFailure = Failure;
	if (bSuccess)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: joined session %s."), *GetNameSafe(GetGameInstance()), *LastSessionId);
		SetState(ECKSessionsState::InSession);
		OnJoinComplete.Broadcast(true, Failure);
		FinishQuickPlay(true, false, Failure);
		return;
	}

	UE_LOG(LogCKSessions, Warning, TEXT("%s: join failed: %s."), *GetNameSafe(GetGameInstance()), *UEnum::GetValueAsString(Failure));
	bWaitingForConnection = false;
	LastSessionId.Reset();
	LastConnectString.Reset();
	DestroySessionThen([this, Failure]()
	{
		SetState(ECKSessionsState::Idle);
		OnJoinComplete.Broadcast(false, Failure);
		if (!bQuickPlay)
		{
			return;
		}
		// The chosen session filled up or closed between the search and the join: quick play hosts instead, once.
		// After a failed travel (ConnectFailed) the engine is still returning to its default map, so it stops there.
		const bool bSessionTaken = Failure == ECKSessionsFailure::SessionFull || Failure == ECKSessionsFailure::SessionGone
			|| Failure == ECKSessionsFailure::JoinFailed;
		if (bSessionTaken && !bQuickPlayFellBack)
		{
			bQuickPlayFellBack = true;
			UE_LOG(LogCKSessions, Log, TEXT("%s: quick play could not join (%s), hosting instead."), *GetNameSafe(GetGameInstance()), *UEnum::GetValueAsString(Failure));
			if (!HostSessionWithAttributes(GetActiveProfile(), QuickPlayMap, QuickPlayAttributes))
			{
				FinishQuickPlay(false, true, LastFailure != ECKSessionsFailure::None ? LastFailure : ECKSessionsFailure::Busy);
			}
			return;
		}
		FinishQuickPlay(false, false, Failure);
	});
}

void UCKSessionsSubsystem::LeaveSession()
{
	LeaveSessionToMap(TSoftObjectPtr<UWorld>());
}

void UCKSessionsSubsystem::LeaveSessionToMap(TSoftObjectPtr<UWorld> Map)
{
	// Leaving while a lost host waits for On Host Lost cancels the event and leaves as usual.
	if (State == ECKSessionsState::Leaving && !bHostLossPending)
	{
		return;
	}
	bHostLossPending = false;
	PendingHostLoss = FCKSessionsHostLoss();
	// The player chose to leave: an invite still waiting for its turn is dropped.
	bInviteJoinPending = false;
	PendingInvite = FCKSessionsSearchResult();
	SetState(ECKSessionsState::Leaving);
	bWaitingForListenServer = false;
	bWaitingForConnection = false;
	FinishQuickPlay(false, false, ECKSessionsFailure::Busy);
	DestroySessionThen([this, Map]()
	{
		const bool bWasHost = bIsHost;
		bIsHost = false;
		bMatchInProgress = false;
		RuntimeAttributes.Reset();
		bAttributesChangedWhileHosting = false;
		LastSessionId.Reset();
		LastConnectString.Reset();
		PlayerRecords.Reset();
		BannedPlayers.Reset();
		LastFailure = ECKSessionsFailure::None;
		SetState(ECKSessionsState::Idle);
		const UWorld* World = GetWorld();
		if (World != nullptr && World->GetNetMode() != NM_DedicatedServer && (World->GetNetMode() != NM_Standalone || bWasHost || !Map.IsNull()))
		{
			ReturnToMenu(Map);
		}
	});
}

void UCKSessionsSubsystem::ReturnToMenu(const TSoftObjectPtr<UWorld>& Map)
{
	const TSoftObjectPtr<UWorld>& MenuMap = Map.IsNull() ? UCKSessionsSettings::Get().GetMenuMap() : Map;
	const FString MapName = MenuMap.IsNull() ? UGameMapsSettings::GetGameDefaultMap() : MenuMap.GetLongPackageName();
	if (!MapName.IsEmpty())
	{
		UGameplayStatics::OpenLevel(GetWorld(), FName(*MapName), true);
	}
}

void UCKSessionsSubsystem::DestroySessionThen(TFunction<void()> Next)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		Next();
		return;
	}
	AfterDestroy = MoveTemp(Next);
	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UCKSessionsSubsystem::OnDestroySessionComplete));
	if (!Sessions->DestroySession(NAME_GameSession) && AfterDestroy)
	{
		OnDestroySessionComplete(NAME_GameSession, false);
	}
}

void UCKSessionsSubsystem::OnDestroySessionComplete(const FName SessionName, const bool bSuccess)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
	// The continuation may destroy again (host after leave), so it is moved out before it runs.
	TFunction<void()> Next = MoveTemp(AfterDestroy);
	AfterDestroy = nullptr;
	if (Next)
	{
		Next();
	}
}

bool UCKSessionsSubsystem::StartMatch()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!bIsHost || State != ECKSessionsState::InSession || !Sessions.IsValid())
	{
		return false;
	}
	bMatchInProgress = true;
	Sessions->StartSession(NAME_GameSession);
	UpdateAdvertisedMatchState();
	UE_LOG(LogCKSessions, Log, TEXT("%s: match started."), *GetNameSafe(GetGameInstance()));
	return true;
}

bool UCKSessionsSubsystem::EndMatch()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!bIsHost || !bMatchInProgress || !Sessions.IsValid())
	{
		return false;
	}
	bMatchInProgress = false;
	Sessions->EndSession(NAME_GameSession);
	UpdateAdvertisedMatchState();
	UE_LOG(LogCKSessions, Log, TEXT("%s: match ended."), *GetNameSafe(GetGameInstance()));
	return true;
}

void UCKSessionsSubsystem::UpdateAdvertisedMatchState()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (Session == nullptr)
	{
		return;
	}
	FOnlineSessionSettings Settings = Session->SessionSettings;
	CKSessions::Online::SetMatchInProgress(Settings, bMatchInProgress, GetActiveProfile()->AllowsJoinInProgress());
	Sessions->UpdateSession(NAME_GameSession, Settings, true);
}

void UCKSessionsSubsystem::BeginReconnect()
{
	const UCKSessionsProfileData* Profile = GetActiveProfile();
	UE_LOG(LogCKSessions, Log, TEXT("%s: connection lost, reconnecting to session %s (%d attempts)."),
		*GetNameSafe(GetGameInstance()), *LastSessionId, Profile->GetReconnectAttempts());
	ReconnectAttemptsLeft = Profile->GetReconnectAttempts();
	ReconnectStep = EReconnectStep::Waiting;
	// The own session is found by its id, whatever the host advertises now.
	SearchFilter.Reset();
	NextReconnectSeconds = FPlatformTime::Seconds() + Profile->GetReconnectIntervalSeconds();
	SetState(ECKSessionsState::Reconnecting);
}

void UCKSessionsSubsystem::TickReconnect(const double Now)
{
	if (ReconnectStep != EReconnectStep::Waiting || Now < NextReconnectSeconds)
	{
		return;
	}
	// After a lost connection the engine travels the client to the default map; wait until that travel is over.
	const UWorld* World = GetWorld();
	const FWorldContext* Context = World != nullptr && GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (Context == nullptr || World->GetNetMode() != NM_Standalone || !World->HasBegunPlay()
		|| !Context->TravelURL.IsEmpty() || Context->PendingNetGame != nullptr)
	{
		return;
	}

	if (ReconnectAttemptsLeft <= 0)
	{
		LoseSession(ECKSessionsFailure::ReconnectFailed, TEXT("Every reconnect attempt failed."));
		return;
	}
	--ReconnectAttemptsLeft;
	UE_LOG(LogCKSessions, Log, TEXT("%s: reconnect attempt, %d left."), *GetNameSafe(GetGameInstance()), ReconnectAttemptsLeft);

	if (GetActiveProfile()->IsPrivate())
	{
		ReconnectStep = EReconnectStep::Joining;
		if (!TravelToHost(LastConnectString))
		{
			FailReconnectAttempt();
		}
		return;
	}
	ReconnectStep = EReconnectStep::Searching;
	DestroySessionThen([this]()
	{
		StartSearch();
	});
}

void UCKSessionsSubsystem::FailReconnectAttempt()
{
	bWaitingForConnection = false;
	ReconnectStep = EReconnectStep::Waiting;
	NextReconnectSeconds = FPlatformTime::Seconds() + GetActiveProfile()->GetReconnectIntervalSeconds();
}

void UCKSessionsSubsystem::LoseSession(const ECKSessionsFailure Failure, const FString& Message)
{
	UE_LOG(LogCKSessions, Warning, TEXT("%s: session lost: %s %s"), *GetNameSafe(GetGameInstance()), *UEnum::GetValueAsString(Failure), *Message);
	bWaitingForConnection = false;
	LastFailure = Failure;
	LastSessionId.Reset();
	LastConnectString.Reset();
	DestroySessionThen([this, Failure, Message]()
	{
		SetState(ECKSessionsState::Idle);
		OnSessionFailure.Broadcast(Failure, Message);
		if (!UCKSessionsSettings::Get().GetMenuMap().IsNull())
		{
			// Not right away: a lost link is reported from inside the engine's network failure broadcast, and the engine
			// then sets its own travel (the default map, "?closed") after this handler returns, overriding ours. One tick
			// later our travel is the last one set.
			if (const UGameInstance* GameInstance = GetGameInstance())
			{
				GameInstance->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					if (State == ECKSessionsState::Idle)
					{
						ReturnToMenu();
					}
				}));
			}
		}
	});
}

void UCKSessionsSubsystem::SetHandleHostLossExternally(const bool bEnabled)
{
	if (bHandleHostLossExternally != bEnabled)
	{
		bHandleHostLossExternally = bEnabled;
		UE_LOG(LogCKSessions, Log, TEXT("%s: a lost host is handled %s."), *GetNameSafe(GetGameInstance()),
			bEnabled ? TEXT("outside the plugin (On Host Lost)") : TEXT("by the plugin (reconnection, menu map)"));
	}
}

void UCKSessionsSubsystem::BeginHostLoss(const ENetworkFailure::Type FailureType, const FString& Message)
{
	PendingHostLoss = FCKSessionsHostLoss();
	switch (FailureType)
	{
	case ENetworkFailure::FailureReceived:
		// The host shut its net driver down and said so ("Host closed the connection.").
		PendingHostLoss.Reason = ECKSessionsHostLossReason::HostLeft;
		break;
	case ENetworkFailure::ConnectionTimeout:
		PendingHostLoss.Reason = ECKSessionsHostLossReason::TimedOut;
		break;
	default:
		PendingHostLoss.Reason = ECKSessionsHostLossReason::ConnectionLost;
		break;
	}
	PendingHostLoss.Message = Message;
	PendingHostLoss.SessionId = LastSessionId;
	PendingHostLoss.Session = PendingJoin;
	if (PendingJoin.IsValid())
	{
		PendingHostLoss.Attributes = CKSessions::Online::ReadAttributes(PendingJoin.GetNative().Session.SessionSettings);
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: host of session %s lost (%s), handed over to On Host Lost once the engine is back on its default map."),
		*GetNameSafe(GetGameInstance()), *LastSessionId, *UEnum::GetValueAsString(PendingHostLoss.Reason));

	bWaitingForConnection = false;
	bHostLossPending = true;
	HostLossStartedSeconds = FPlatformTime::Seconds();
	SetState(ECKSessionsState::Leaving);
}

void UCKSessionsSubsystem::TickHostLoss(const double Now)
{
	// After a lost connection the engine travels the client to its default map. Hosting needs a standalone world, so
	// the event waits for that travel, and fires anyway after the travel time limit.
	const UWorld* World = GetWorld();
	const FWorldContext* Context = World != nullptr && GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	const bool bBackOnDefaultMap = Context != nullptr && World->GetNetMode() == NM_Standalone && World->HasBegunPlay()
		&& Context->TravelURL.IsEmpty() && Context->PendingNetGame == nullptr;
	if (!bBackOnDefaultMap && Now - HostLossStartedSeconds <= CKSessions::Private::TravelTimeoutSeconds)
	{
		return;
	}
	if (!bBackOnDefaultMap)
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: the engine did not return to its default map in %.0f s after the host was lost."),
			*GetNameSafe(GetGameInstance()), CKSessions::Private::TravelTimeoutSeconds);
	}

	bHostLossPending = false;
	LastFailure = ECKSessionsFailure::ConnectionLost;
	LastSessionId.Reset();
	LastConnectString.Reset();
	DestroySessionThen([this]()
	{
		const FCKSessionsHostLoss HostLoss = MoveTemp(PendingHostLoss);
		PendingHostLoss = FCKSessionsHostLoss();
		SetState(ECKSessionsState::Idle);
		UE_LOG(LogCKSessions, Log, TEXT("%s: host lost (%s), session %s with %d attributes."), *GetNameSafe(GetGameInstance()),
			*UEnum::GetValueAsString(HostLoss.Reason), *HostLoss.SessionId, HostLoss.Attributes.Num());
		OnHostLost.Broadcast(HostLoss);
	});
}

void UCKSessionsSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, const ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// A connection refused while connecting (a ban) fails the pending net game, which has no world yet: find its game instance.
	const FWorldContext* PendingContext = World == nullptr && NetDriver != nullptr && GEngine != nullptr ? GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver) : nullptr;
	const bool bOwnPendingGame = PendingContext != nullptr && PendingContext->OwningGameInstance == GetGameInstance();
	if ((!IsOwnWorld(World) && !bOwnPendingGame) || State == ECKSessionsState::Leaving || bIsHost)
	{
		return;
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: network failure %s: %s"), *GetNameSafe(GetGameInstance()), ENetworkFailure::ToString(FailureType), *ErrorString);

	// The host removed this player (kick or ban): final, whatever the state, and never reconnected or handed to On Host Lost.
	bool bBanned = false;
	FString RemovalReason;
	if ((FailureType == ENetworkFailure::FailureReceived || FailureType == ENetworkFailure::PendingConnectionFailure) && CKSessions::Online::ParseRemovalMessage(ErrorString, bBanned, RemovalReason)
		&& (State == ECKSessionsState::Joining || State == ECKSessionsState::Reconnecting || State == ECKSessionsState::InSession))
	{
		HandleRemoval(bBanned, RemovalReason);
		return;
	}

	switch (State)
	{
	case ECKSessionsState::Joining:
		FinishJoin(false, ECKSessionsFailure::ConnectFailed);
		break;
	case ECKSessionsState::Reconnecting:
		if (ReconnectStep == EReconnectStep::Joining)
		{
			FailReconnectAttempt();
		}
		break;
	case ECKSessionsState::InSession:
		if (bHandleHostLossExternally)
		{
			BeginHostLoss(FailureType, ErrorString);
		}
		// Only a lost link is worth another try; a refusal from the host (FailureReceived: full, closed match) is final.
		else if (FailureType != ENetworkFailure::FailureReceived && GetActiveProfile()->GetReconnectAttempts() > 0 && !LastConnectString.IsEmpty())
		{
			BeginReconnect();
		}
		else
		{
			LoseSession(ECKSessionsFailure::ConnectionLost, ErrorString);
		}
		break;
	default:
		break;
	}
}

void UCKSessionsSubsystem::OnTravelFailure(UWorld* World, const ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsOwnWorld(World))
	{
		return;
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: travel failure %s: %s"), *GetNameSafe(GetGameInstance()), ETravelFailure::ToString(FailureType), *ErrorString);

	// A banned player is refused while connecting: the engine may report it as a failed travel with the host's text.
	bool bBanned = false;
	FString RemovalReason;
	if (CKSessions::Online::ParseRemovalMessage(ErrorString, bBanned, RemovalReason)
		&& ((State == ECKSessionsState::Joining && bWaitingForConnection) || (State == ECKSessionsState::Reconnecting && ReconnectStep == EReconnectStep::Joining)))
	{
		HandleRemoval(bBanned, RemovalReason);
		return;
	}

	switch (State)
	{
	case ECKSessionsState::Hosting:
		if (bWaitingForListenServer)
		{
			bWaitingForListenServer = false;
			FinishHost(false, ECKSessionsFailure::HostTravelFailed);
		}
		break;
	case ECKSessionsState::Joining:
		if (bWaitingForConnection)
		{
			FinishJoin(false, ECKSessionsFailure::ConnectFailed);
		}
		break;
	case ECKSessionsState::Reconnecting:
		if (ReconnectStep == EReconnectStep::Joining)
		{
			FailReconnectAttempt();
		}
		break;
	default:
		break;
	}
}

void UCKSessionsSubsystem::HandleRemoval(const bool bBanned, const FString& Reason)
{
	const ECKSessionsFailure Failure = bBanned ? ECKSessionsFailure::Banned : ECKSessionsFailure::Kicked;
	UE_LOG(LogCKSessions, Log, TEXT("%s: removed from the session by the host (%s): %s"), *GetNameSafe(GetGameInstance()), bBanned ? TEXT("ban") : TEXT("kick"), *Reason);
	LastRemovalReason = Reason;
	if (State == ECKSessionsState::Joining)
	{
		FinishJoin(false, Failure);
		return;
	}
	ReconnectAttemptsLeft = 0;
	LoseSession(Failure, Reason);
}

bool UCKSessionsSubsystem::KickPlayer(APlayerState* PlayerState, const FString& Reason)
{
	return RemovePlayer(PlayerState, Reason, false);
}

bool UCKSessionsSubsystem::BanPlayer(APlayerState* PlayerState, const FString& Reason)
{
	return RemovePlayer(PlayerState, Reason, true);
}

bool UCKSessionsSubsystem::RemovePlayer(APlayerState* PlayerState, const FString& Reason, const bool bBan)
{
	APlayerController* Controller = PlayerState != nullptr ? Cast<APlayerController>(PlayerState->GetOwningController()) : nullptr;
	UNetConnection* Connection = Controller != nullptr ? Cast<UNetConnection>(Controller->Player) : nullptr;
	const UWorld* World = GetWorld();
	AGameModeBase* GameMode = World != nullptr ? World->GetAuthGameMode() : nullptr;
	if (!bIsHost || Connection == nullptr || GameMode == nullptr || GameMode->GameSession == nullptr || Controller->IsLocalController())
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s: cannot %s %s: only the host removes a remote player of its session."), *GetNameSafe(GetGameInstance()),
			bBan ? TEXT("ban") : TEXT("kick"), *GetNameSafe(PlayerState));
		return false;
	}

	const FString PlayerId = CKSessions::Private::GetPlayerId(*PlayerState);
	if (bBan)
	{
		FCKSessionsBanRecord& Ban = BannedPlayers.FindOrAdd(PlayerId);
		Ban.PlayerId = PlayerId;
		Ban.PlayerName = PlayerState->GetPlayerName();
		Ban.Reason = Reason;
	}
	UE_LOG(LogCKSessions, Log, TEXT("%s: %s %s (%s): %s"), *GetNameSafe(GetGameInstance()), bBan ? TEXT("banned") : TEXT("kicked"),
		*PlayerState->GetPlayerName(), *PlayerId, *Reason);

	// The engine's kick only closes the connection, which the client takes for a lost link. A failure message first tells
	// it why; the client's engine closes the connection, and the plugin does not reconnect (On Session Failure, Kicked).
	FString RemovalMessage = CKSessions::Online::MakeRemovalMessage(bBan, Reason);
	FNetControlMessage<NMT_Failure>::Send(Connection, RemovalMessage);
	Connection->FlushNet(true);
	GameMode->GameSession->KickPlayer(Controller, FText::FromString(Reason));
	// A removed player has no place to come back to within the grace time.
	PlayerRecords.Remove(PlayerId);
	return true;
}

bool UCKSessionsSubsystem::UnbanPlayer(const FString& PlayerId)
{
	const bool bRemoved = BannedPlayers.Remove(PlayerId) > 0;
	if (bRemoved)
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: unbanned %s."), *GetNameSafe(GetGameInstance()), *PlayerId);
	}
	return bRemoved;
}

bool UCKSessionsSubsystem::IsPlayerBanned(const FString& PlayerId) const
{
	return BannedPlayers.Contains(PlayerId);
}

TArray<FCKSessionsBanRecord> UCKSessionsSubsystem::GetBannedPlayers() const
{
	TArray<FCKSessionsBanRecord> Bans;
	BannedPlayers.GenerateValueArray(Bans);
	return Bans;
}

void UCKSessionsSubsystem::OnGameModePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage)
{
	if (GameMode == nullptr || GameMode->GetGameInstance() != GetGameInstance() || !ErrorMessage.IsEmpty())
	{
		return;
	}
	if (const FCKSessionsBanRecord* Ban = NewPlayer.IsValid() ? BannedPlayers.Find(NewPlayer.ToString()) : nullptr)
	{
		// The ban comes first: it also refuses a player of this session who comes back.
		ErrorMessage = CKSessions::Online::MakeRemovalMessage(true, Ban->Reason);
		UE_LOG(LogCKSessions, Log, TEXT("%s: refused banned player %s."), *GetNameSafe(GetGameInstance()), *Ban->PlayerId);
		return;
	}
	if (bMatchInProgress && !GetActiveProfile()->AllowsJoinInProgress())
	{
		// Players of this session may come back; newcomers wait for the next match.
		const bool bKnownPlayer = NewPlayer.IsValid() && PlayerRecords.Contains(NewPlayer.ToString());
		if (!bKnownPlayer)
		{
			ErrorMessage = TEXT("The match is in progress and this session does not take new players.");
		}
	}
}

void UCKSessionsSubsystem::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (GameMode == nullptr || NewPlayer == nullptr || NewPlayer->PlayerState == nullptr || GameMode->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	APlayerState* PlayerState = NewPlayer->PlayerState;
	const FString PlayerId = CKSessions::Private::GetPlayerId(*PlayerState);
	FCKSessionsPlayerRecord& Record = PlayerRecords.FindOrAdd(PlayerId);
	const bool bReconnected = !Record.PlayerId.IsEmpty() && !Record.bConnected;
	Record.PlayerId = PlayerId;
	Record.PlayerName = PlayerState->GetPlayerName();
	Record.bConnected = true;
	Record.DisconnectedAtSeconds = 0.0;
	if (bReconnected)
	{
		++Record.ReconnectCount;
		UE_LOG(LogCKSessions, Log, TEXT("%s: %s reconnected (%d)."), *GetNameSafe(GetGameInstance()), *Record.PlayerName, Record.ReconnectCount);
		OnPlayerReconnected.Broadcast(PlayerState);
	}
	else
	{
		UE_LOG(LogCKSessions, Log, TEXT("%s: %s joined."), *GetNameSafe(GetGameInstance()), *Record.PlayerName);
		OnPlayerJoined.Broadcast(PlayerState);
	}
}

void UCKSessionsSubsystem::OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (GameMode == nullptr || Exiting == nullptr || Exiting->PlayerState == nullptr || GameMode->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	APlayerState* PlayerState = Exiting->PlayerState;
	if (FCKSessionsPlayerRecord* Record = PlayerRecords.Find(CKSessions::Private::GetPlayerId(*PlayerState)))
	{
		Record->bConnected = false;
		Record->DisconnectedAtSeconds = FPlatformTime::Seconds();
		UE_LOG(LogCKSessions, Log, TEXT("%s: %s left."), *GetNameSafe(GetGameInstance()), *Record->PlayerName);
	}
	OnPlayerLeft.Broadcast(PlayerState);
}

void UCKSessionsSubsystem::PurgeExpiredRecords(const double Now)
{
	if (PlayerRecords.IsEmpty())
	{
		return;
	}
	const double GraceSeconds = GetActiveProfile()->GetReconnectGraceSeconds();
	for (TMap<FString, FCKSessionsPlayerRecord>::TIterator It = PlayerRecords.CreateIterator(); It; ++It)
	{
		if (!It.Value().bConnected && Now - It.Value().DisconnectedAtSeconds > GraceSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

TArray<FCKSessionsPlayerRecord> UCKSessionsSubsystem::GetPlayerRecords() const
{
	TArray<FCKSessionsPlayerRecord> Records;
	PlayerRecords.GenerateValueArray(Records);
	return Records;
}
