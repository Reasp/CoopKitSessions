// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "CKSessionsAsyncActions.generated.h"

class UCKSessionsProfileData;
class UCKSessionsSubsystem;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsActionResult, ECKSessionsFailure, Failure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsFindActionResult, const TArray<FCKSessionsSearchResult>&, Results, ECKSessionsFailure, Failure);

/** Blueprint node "Host Session": hosts a session and fires On Success once it exists, or On Failure. */
UCLASS(MinimalAPI)
class UCKSessionsHostAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Hosts a session (see CoopKit Sessions Subsystem > Host Session).
	 * @param Profile  Session profile; empty uses the project's default profile.
	 * @param Map      Map to host on when the game is not a server yet; empty keeps the current map.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Host Session"))
	static UCKSessionsHostAction* HostSession(UObject* WorldContextObject, const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map);

	/**
	 * Hosts a session that advertises Attributes on top of the profile's (see CoopKit Sessions Subsystem > Host Session With Attributes).
	 * @param Profile     Session profile; empty uses the project's default profile.
	 * @param Map         Map to host on when the game is not a server yet; empty keeps the current map.
	 * @param Attributes  Key and string value of each attribute, e.g. MatchId; empty and reserved keys fail.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Host Session With Attributes"))
	static UCKSessionsHostAction* HostSessionWithAttributes(UObject* WorldContextObject, const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map, const TMap<FName, FString>& Attributes);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, ECKSessionsFailure Failure);

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	UPROPERTY()
	TObjectPtr<const UCKSessionsProfileData> Profile;

	TSoftObjectPtr<UWorld> Map;

	TMap<FName, FString> Attributes;

	bool bWithAttributes = false;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};

/** Blueprint node "Find Sessions": searches and fires On Success with the joinable sessions (possibly none), or On Failure. */
UCLASS(MinimalAPI)
class UCKSessionsFindAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Searches for sessions of the profile (see CoopKit Sessions Subsystem > Find Sessions).
	 * @param Profile  Session profile; empty uses the project's default profile.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Find Sessions"))
	static UCKSessionsFindAction* FindSessions(UObject* WorldContextObject, const UCKSessionsProfileData* Profile);

	/**
	 * Searches for sessions of the profile that advertise every attribute of Filter with the same value
	 * (see CoopKit Sessions Subsystem > Find Sessions With Attributes).
	 * @param Profile  Session profile; empty uses the project's default profile.
	 * @param Filter   Key and string value each session must advertise, e.g. MatchId; empty and reserved keys fail.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Find Sessions With Attributes"))
	static UCKSessionsFindAction* FindSessionsWithAttributes(UObject* WorldContextObject, const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter);

	/**
	 * Searches in the background, in any state and without changing it, e.g. a host watching for a second host of its
	 * group (see CoopKit Sessions Subsystem > Find Sessions In Background). Results include full sessions and running matches.
	 * @param Profile  Session profile; empty uses the project's default profile.
	 * @param Filter   Key and string value each session must advertise; may be empty; empty and reserved keys fail.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Find Sessions In Background"))
	static UCKSessionsFindAction* FindSessionsInBackground(UObject* WorldContextObject, const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsFindActionResult OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsFindActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, const TArray<FCKSessionsSearchResult>& Results);

	UFUNCTION()
	void HandleBackgroundComplete(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsSearchResult>& Results);

	/** Unbinds, fires the result pin once and lets the node go. */
	void Complete(bool bSuccess, const TArray<FCKSessionsSearchResult>& Results, ECKSessionsFailure Failure);

	bool bBackground = false;
	bool bCompleted = false;

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	UPROPERTY()
	TObjectPtr<const UCKSessionsProfileData> Profile;

	TMap<FName, FString> Filter;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};

/** Blueprint node "Join Session": joins a found session and fires On Success once connected to the host, or On Failure. */
UCLASS(MinimalAPI)
class UCKSessionsJoinAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Joins a session from Find Sessions (see CoopKit Sessions Subsystem > Join Session). */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Join Session"))
	static UCKSessionsJoinAction* JoinSession(UObject* WorldContextObject, const FCKSessionsSearchResult& Result);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, ECKSessionsFailure Failure);

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	FCKSessionsSearchResult Result;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};

/**
 * Blueprint node "Quick Play": finds a session with the given attributes and joins it, or hosts one when there is none.
 * Fires On Joined once connected to a host, On Hosted once the own session exists, or On Failure.
 */
UCLASS(MinimalAPI)
class UCKSessionsQuickPlayAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Quick play (see CoopKit Sessions Subsystem > Quick Play).
	 * @param Profile     Session profile; empty uses the project's default profile.
	 * @param Attributes  Attributes to search for and to advertise when hosting, e.g. Mode = Survival; may be empty.
	 * @param Map         Map to host on when no session is found; empty keeps the current map.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Quick Play"))
	static UCKSessionsQuickPlayAction* QuickPlay(UObject* WorldContextObject, const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, TSoftObjectPtr<UWorld> Map);

	virtual void Activate() override;

	/** Joined a session of another host. */
	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnJoined;

	/** No session matched: this machine now hosts one with the attributes. */
	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnHosted;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, bool bHosted, ECKSessionsFailure Failure);

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	UPROPERTY()
	TObjectPtr<const UCKSessionsProfileData> Profile;

	TMap<FName, FString> Attributes;

	TSoftObjectPtr<UWorld> Map;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsFriendsActionResult, const TArray<FCKSessionsFriend>&, Friends, ECKSessionsFailure, Failure);

/**
 * Blueprint node "Read Friends": reads the local player's friends with presence and fires On Success with the list
 * (possibly empty), or On Failure (Not Supported with Null, Friends Failed when the platform refused).
 */
UCLASS(MinimalAPI)
class UCKSessionsReadFriendsAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Reads the friends list (see CoopKit Sessions Subsystem > Read Friends). */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Read Friends"))
	static UCKSessionsReadFriendsAction* ReadFriends(UObject* WorldContextObject);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsFriendsActionResult OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsFriendsActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsFriend>& Friends);

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};

/** Blueprint node "Join Friend": joins the game session a friend plays in and fires On Success once connected, or On Failure. */
UCLASS(MinimalAPI)
class UCKSessionsJoinFriendAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Joins a friend from Read Friends (see CoopKit Sessions Subsystem > Join Friend). */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Join Friend"))
	static UCKSessionsJoinFriendAction* JoinFriend(UObject* WorldContextObject, const FCKSessionsFriend& Friend);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FCKSessionsActionResult OnFailure;

private:
	UFUNCTION()
	void HandleComplete(bool bSuccess, ECKSessionsFailure Failure);

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	FCKSessionsFriend Friend;

	UPROPERTY()
	TObjectPtr<UCKSessionsSubsystem> Subsystem;
};
