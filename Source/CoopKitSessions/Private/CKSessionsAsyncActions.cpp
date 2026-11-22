// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsAsyncActions.h"
#include "CKSessionsSubsystem.h"
#include "Engine/World.h"

UCKSessionsHostAction* UCKSessionsHostAction::HostSession(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile, const TSoftObjectPtr<UWorld> InMap)
{
	UCKSessionsHostAction* Action = NewObject<UCKSessionsHostAction>();
	Action->WorldContext = WorldContextObject;
	Action->Profile = InProfile;
	Action->Map = InMap;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UCKSessionsHostAction* UCKSessionsHostAction::HostSessionWithAttributes(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile, const TSoftObjectPtr<UWorld> InMap,
	const TMap<FName, FString>& InAttributes)
{
	UCKSessionsHostAction* Action = HostSession(WorldContextObject, InProfile, InMap);
	Action->Attributes = InAttributes;
	Action->bWithAttributes = true;
	return Action;
}

void UCKSessionsHostAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		HandleComplete(false, ECKSessionsFailure::NoOnlineSubsystem);
		return;
	}
	// The subsystem may answer synchronously (busy, no online subsystem), so the handler is bound first.
	Subsystem->OnHostComplete.AddDynamic(this, &UCKSessionsHostAction::HandleComplete);
	if (bWithAttributes)
	{
		Subsystem->HostSessionWithAttributes(Profile, Map, Attributes);
	}
	else
	{
		Subsystem->HostSession(Profile, Map);
	}
}

void UCKSessionsHostAction::HandleComplete(const bool bSuccess, const ECKSessionsFailure Failure)
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnHostComplete.RemoveDynamic(this, &UCKSessionsHostAction::HandleComplete);
	}
	(bSuccess ? OnSuccess : OnFailure).Broadcast(Failure);
	SetReadyToDestroy();
}

UCKSessionsFindAction* UCKSessionsFindAction::FindSessions(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile)
{
	UCKSessionsFindAction* Action = NewObject<UCKSessionsFindAction>();
	Action->WorldContext = WorldContextObject;
	Action->Profile = InProfile;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UCKSessionsFindAction* UCKSessionsFindAction::FindSessionsWithAttributes(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile, const TMap<FName, FString>& InFilter)
{
	UCKSessionsFindAction* Action = FindSessions(WorldContextObject, InProfile);
	Action->Filter = InFilter;
	return Action;
}

UCKSessionsFindAction* UCKSessionsFindAction::FindSessionsInBackground(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile, const TMap<FName, FString>& InFilter)
{
	UCKSessionsFindAction* Action = FindSessionsWithAttributes(WorldContextObject, InProfile, InFilter);
	Action->bBackground = true;
	return Action;
}

void UCKSessionsFindAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		Complete(false, TArray<FCKSessionsSearchResult>(), ECKSessionsFailure::NoOnlineSubsystem);
		return;
	}
	if (!bBackground)
	{
		Subsystem->OnFindComplete.AddDynamic(this, &UCKSessionsFindAction::HandleComplete);
		Subsystem->FindSessionsWithAttributes(Profile, Filter);
		return;
	}
	// A refusal does not fire the subsystem's event (a background search in flight keeps its listeners): read it here.
	const ECKSessionsFailure Refusal = Subsystem->IsBackgroundSearchInFlight() ? ECKSessionsFailure::Busy : ECKSessionsFailure::InvalidAttribute;
	Subsystem->OnBackgroundFindComplete.AddDynamic(this, &UCKSessionsFindAction::HandleBackgroundComplete);
	if (!Subsystem->FindSessionsInBackground(Profile, Filter) && !bCompleted)
	{
		Complete(false, TArray<FCKSessionsSearchResult>(), Refusal);
	}
}

void UCKSessionsFindAction::HandleComplete(const bool bSuccess, const TArray<FCKSessionsSearchResult>& Results)
{
	ECKSessionsFailure Failure = ECKSessionsFailure::NoOnlineSubsystem;
	if (Subsystem != nullptr)
	{
		Failure = bSuccess ? ECKSessionsFailure::None : Subsystem->GetLastFailure();
		if (!bSuccess && Failure == ECKSessionsFailure::None)
		{
			// Refused before searching: another request is in flight.
			Failure = ECKSessionsFailure::Busy;
		}
	}
	Complete(bSuccess, Results, Failure);
}

void UCKSessionsFindAction::HandleBackgroundComplete(const bool bSuccess, const ECKSessionsFailure Failure, const TArray<FCKSessionsSearchResult>& Results)
{
	Complete(bSuccess, Results, Failure);
}

void UCKSessionsFindAction::Complete(const bool bSuccess, const TArray<FCKSessionsSearchResult>& Results, const ECKSessionsFailure Failure)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;
	if (Subsystem != nullptr)
	{
		Subsystem->OnFindComplete.RemoveDynamic(this, &UCKSessionsFindAction::HandleComplete);
		Subsystem->OnBackgroundFindComplete.RemoveDynamic(this, &UCKSessionsFindAction::HandleBackgroundComplete);
	}
	(bSuccess ? OnSuccess : OnFailure).Broadcast(Results, Failure);
	SetReadyToDestroy();
}

UCKSessionsJoinAction* UCKSessionsJoinAction::JoinSession(UObject* WorldContextObject, const FCKSessionsSearchResult& InResult)
{
	UCKSessionsJoinAction* Action = NewObject<UCKSessionsJoinAction>();
	Action->WorldContext = WorldContextObject;
	Action->Result = InResult;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UCKSessionsJoinAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		HandleComplete(false, ECKSessionsFailure::NoOnlineSubsystem);
		return;
	}
	Subsystem->OnJoinComplete.AddDynamic(this, &UCKSessionsJoinAction::HandleComplete);
	Subsystem->JoinSession(Result);
}

void UCKSessionsJoinAction::HandleComplete(const bool bSuccess, const ECKSessionsFailure Failure)
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnJoinComplete.RemoveDynamic(this, &UCKSessionsJoinAction::HandleComplete);
	}
	(bSuccess ? OnSuccess : OnFailure).Broadcast(Failure);
	SetReadyToDestroy();
}

UCKSessionsQuickPlayAction* UCKSessionsQuickPlayAction::QuickPlay(UObject* WorldContextObject, const UCKSessionsProfileData* InProfile, const TMap<FName, FString>& InAttributes,
	const TSoftObjectPtr<UWorld> InMap)
{
	UCKSessionsQuickPlayAction* Action = NewObject<UCKSessionsQuickPlayAction>();
	Action->WorldContext = WorldContextObject;
	Action->Profile = InProfile;
	Action->Attributes = InAttributes;
	Action->Map = InMap;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UCKSessionsQuickPlayAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		HandleComplete(false, false, ECKSessionsFailure::NoOnlineSubsystem);
		return;
	}
	Subsystem->OnQuickPlayComplete.AddDynamic(this, &UCKSessionsQuickPlayAction::HandleComplete);
	Subsystem->QuickPlay(Profile, Attributes, Map);
}

void UCKSessionsQuickPlayAction::HandleComplete(const bool bSuccess, const bool bHosted, const ECKSessionsFailure Failure)
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnQuickPlayComplete.RemoveDynamic(this, &UCKSessionsQuickPlayAction::HandleComplete);
	}
	if (!bSuccess)
	{
		OnFailure.Broadcast(Failure);
	}
	else
	{
		(bHosted ? OnHosted : OnJoined).Broadcast(Failure);
	}
	SetReadyToDestroy();
}

UCKSessionsReadFriendsAction* UCKSessionsReadFriendsAction::ReadFriends(UObject* WorldContextObject)
{
	UCKSessionsReadFriendsAction* Action = NewObject<UCKSessionsReadFriendsAction>();
	Action->WorldContext = WorldContextObject;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UCKSessionsReadFriendsAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		HandleComplete(false, ECKSessionsFailure::NoOnlineSubsystem, TArray<FCKSessionsFriend>());
		return;
	}
	// The subsystem may answer synchronously (Null, busy), so the handler is bound first.
	Subsystem->OnReadFriendsComplete.AddDynamic(this, &UCKSessionsReadFriendsAction::HandleComplete);
	Subsystem->ReadFriends();
}

void UCKSessionsReadFriendsAction::HandleComplete(const bool bSuccess, const ECKSessionsFailure Failure, const TArray<FCKSessionsFriend>& Friends)
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnReadFriendsComplete.RemoveDynamic(this, &UCKSessionsReadFriendsAction::HandleComplete);
	}
	(bSuccess ? OnSuccess : OnFailure).Broadcast(Friends, Failure);
	SetReadyToDestroy();
}

UCKSessionsJoinFriendAction* UCKSessionsJoinFriendAction::JoinFriend(UObject* WorldContextObject, const FCKSessionsFriend& InFriend)
{
	UCKSessionsJoinFriendAction* Action = NewObject<UCKSessionsJoinFriendAction>();
	Action->WorldContext = WorldContextObject;
	Action->Friend = InFriend;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UCKSessionsJoinFriendAction::Activate()
{
	Subsystem = UCKSessionsSubsystem::Get(WorldContext);
	if (Subsystem == nullptr)
	{
		HandleComplete(false, ECKSessionsFailure::NoOnlineSubsystem);
		return;
	}
	Subsystem->OnJoinComplete.AddDynamic(this, &UCKSessionsJoinFriendAction::HandleComplete);
	Subsystem->JoinFriend(Friend);
}

void UCKSessionsJoinFriendAction::HandleComplete(const bool bSuccess, const ECKSessionsFailure Failure)
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnJoinComplete.RemoveDynamic(this, &UCKSessionsJoinFriendAction::HandleComplete);
	}
	(bSuccess ? OnSuccess : OnFailure).Broadcast(Failure);
	SetReadyToDestroy();
}
