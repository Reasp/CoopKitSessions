// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsCompat.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsProfileData.h"
#include "Misc/AutomationTest.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemTypes.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		/** Minimal session info, so a hand-made search result counts as valid without a real online subsystem. */
		class FTestSessionInfo final : public FOnlineSessionInfo
		{
		public:
			FTestSessionInfo()
				: SessionId(FUniqueNetIdString::Create(TEXT("TestSession"), TEXT("TEST")))
			{
			}

			virtual const uint8* GetBytes() const override
			{
				return nullptr;
			}

			virtual int32 GetSize() const override
			{
				return 0;
			}

			virtual bool IsValid() const override
			{
				return true;
			}

			virtual FString ToString() const override
			{
				return SessionId->ToString();
			}

			virtual FString ToDebugString() const override
			{
				return SessionId->ToString();
			}

			virtual const FUniqueNetId& GetSessionId() const override
			{
				return *SessionId;
			}

		private:
			FUniqueNetIdStringRef SessionId;
		};

		template <typename InPropertyType, typename InValueType>
		void SetProperty(UObject& Object, const TCHAR* PropertyName, const InValueType& Value)
		{
			InPropertyType* Property = FindFProperty<InPropertyType>(Object.GetClass(), PropertyName);
			check(Property != nullptr);
			Property->SetPropertyValue_InContainer(&Object, Value);
		}

		/** What a search would return for a host that created its session with these settings. */
		FOnlineSessionSearchResult MakeResult(const FOnlineSessionSettings& Settings, const int32 OpenPublicConnections)
		{
			FOnlineSessionSearchResult Result;
			Result.Session.OwningUserId = FUniqueNetIdString::Create(TEXT("Host"), TEXT("TEST"));
			Result.Session.SessionInfo = MakeShared<FTestSessionInfo>();
			Result.Session.SessionSettings = Settings;
			Result.Session.NumOpenPublicConnections = OpenPublicConnections;
			Result.Session.NumOpenPrivateConnections = 0;
			return Result;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsOnlineSettingsTest, "CoopKit.Sessions.Online.ProfileToSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsOnlineSettingsTest::RunTest(const FString& Parameters)
{
	namespace Online = CKSessions::Online;
	namespace Compat = CKSessions::Compat;
	using namespace CKSessions::Tests;

	UCKSessionsProfileData* Profile = NewObject<UCKSessionsProfileData>(GetTransientPackage());

	// Auto scope: LAN with Null, online with a service.
	TestTrue(TEXT("Auto + Null is LAN"), CKSessions::Online::ShouldUseLan(ECKSessionsNetworkScope::Auto, CKSessions::Online::NullSubsystemName));
	TestFalse(TEXT("Auto + Steam is online"), CKSessions::Online::ShouldUseLan(ECKSessionsNetworkScope::Auto, TEXT("STEAM")));
	TestTrue(TEXT("Lan + Steam is LAN"), CKSessions::Online::ShouldUseLan(ECKSessionsNetworkScope::Lan, TEXT("STEAM")));
	TestTrue(TEXT("Online + Null falls back to LAN"), CKSessions::Online::ShouldUseLan(ECKSessionsNetworkScope::Online, CKSessions::Online::NullSubsystemName));

	// Default profile on Null, listen server.
	FOnlineSessionSettings NullSettings;
	CKSessions::Online::BuildSessionSettings(*Profile, true, CKSessions::Online::EHostKind::ListenServer, TEXT("/Game/Maps/L_Lobby"), NullSettings);
	TestTrue(TEXT("Null: LAN match"), NullSettings.bIsLANMatch);
	TestEqual(TEXT("Null: public slots = Max Players"), NullSettings.NumPublicConnections, 4);
	TestFalse(TEXT("Null: no presence on LAN"), NullSettings.bUsesPresence);
	TestTrue(TEXT("Null: advertised"), NullSettings.bShouldAdvertise);
	FString SessionType;
	TestTrue(TEXT("Session type advertised"), NullSettings.Get(CKSessions::Online::SessionTypeKey, SessionType));
	TestEqual(TEXT("Session type value"), SessionType, FString(TEXT("Coop")));
	FString MapName;
	NullSettings.Get(SETTING_MAPNAME, MapName);
	TestEqual(TEXT("Map advertised"), MapName, FString(TEXT("/Game/Maps/L_Lobby")));
	TestFalse(TEXT("A new session is not in progress"), CKSessions::Online::IsMatchInProgress(NullSettings));

	// Online listen server uses lobbies; a dedicated server never does.
	FOnlineSessionSettings OnlineSettings;
	CKSessions::Online::BuildSessionSettings(*Profile, false, CKSessions::Online::EHostKind::ListenServer, TEXT("/Game/Maps/L_Lobby"), OnlineSettings);
	TestTrue(TEXT("Online listen: presence"), OnlineSettings.bUsesPresence);
	TestTrue(TEXT("Online listen: lobbies"), OnlineSettings.bUseLobbiesIfAvailable);
	FOnlineSessionSettings DedicatedSettings;
	CKSessions::Online::BuildSessionSettings(*Profile, false, CKSessions::Online::EHostKind::DedicatedServer, TEXT("/Game/Maps/L_Lobby"), DedicatedSettings);
	TestTrue(TEXT("Dedicated: flagged"), DedicatedSettings.bIsDedicated);
	TestFalse(TEXT("Dedicated: no presence"), DedicatedSettings.bUsesPresence);
	TestFalse(TEXT("Dedicated: no lobbies"), DedicatedSettings.bUseLobbiesIfAvailable);

	// Search: LAN flag, result limit, both lobby keys and the session type filter.
	FOnlineSessionSearch Search;
	CKSessions::Online::BuildSearch(*Profile, false, Search);
	TestFalse(TEXT("Online search is not LAN"), Search.bIsLanQuery);
	TestEqual(TEXT("Search limit"), Search.MaxSearchResults, 20);
	bool bLobbies = false;
	TestTrue(TEXT("SEARCH_LOBBIES set"), Search.QuerySettings.Get(SEARCH_LOBBIES, bLobbies) && bLobbies);
	bool bLegacyLobbies = false;
	TestTrue(TEXT("UE 5.4 Steam lobby key set"), Search.QuerySettings.Get(Compat::GetLegacyPresenceSearchKey(), bLegacyLobbies) && bLegacyLobbies);
	FString SearchType;
	TestTrue(TEXT("Search filters by session type"), Search.QuerySettings.Get(CKSessions::Online::SessionTypeKey, SearchType) && SearchType == TEXT("Coop"));

	// Joinable: same type, a free slot; a running match only when the profile allows join in progress.
	TestTrue(TEXT("Open session is joinable"), CKSessions::Online::IsJoinable(MakeResult(NullSettings, 3), *Profile));
	TestFalse(TEXT("Full session is not joinable"), CKSessions::Online::IsJoinable(MakeResult(NullSettings, 0), *Profile));
	FOnlineSessionSettings RunningSettings = NullSettings;
	CKSessions::Online::SetMatchInProgress(RunningSettings, true, true);
	TestTrue(TEXT("Running match flagged"), CKSessions::Online::IsMatchInProgress(RunningSettings));
	TestTrue(TEXT("Running match with join in progress is joinable"), CKSessions::Online::IsJoinable(MakeResult(RunningSettings, 3), *Profile));
	CKSessions::Online::SetMatchInProgress(RunningSettings, true, false);
	TestFalse(TEXT("Running match without join in progress closes the session"), RunningSettings.bAllowJoinInProgress);
	TestFalse(TEXT("Running match without join in progress is not joinable"), CKSessions::Online::IsJoinable(MakeResult(RunningSettings, 3), *Profile));
	CKSessions::Online::SetMatchInProgress(RunningSettings, false, false);
	TestTrue(TEXT("Ended match opens the session again"), RunningSettings.bAllowJoinInProgress);

	UCKSessionsProfileData* OtherType = NewObject<UCKSessionsProfileData>(GetTransientPackage());
	SetProperty<FNameProperty>(*OtherType, TEXT("SessionType"), FName(TEXT("Versus")));
	TestFalse(TEXT("Another session type is not joinable"), CKSessions::Online::IsJoinable(MakeResult(NullSettings, 3), *OtherType));

	// Private profile: invite-only slots, not advertised.
	UCKSessionsProfileData* Private = NewObject<UCKSessionsProfileData>(GetTransientPackage());
	SetProperty<FBoolProperty>(*Private, TEXT("bPrivate"), true);
	FOnlineSessionSettings PrivateSettings;
	CKSessions::Online::BuildSessionSettings(*Private, false, CKSessions::Online::EHostKind::ListenServer, TEXT("/Game/Maps/L_Lobby"), PrivateSettings);
	TestFalse(TEXT("Private: not advertised"), PrivateSettings.bShouldAdvertise);
	TestEqual(TEXT("Private: no public slots"), PrivateSettings.NumPublicConnections, 0);
	TestEqual(TEXT("Private: private slots = Max Players"), PrivateSettings.NumPrivateConnections, 4);

	// Reserved keys.
	TestTrue(TEXT("Plugin keys are reserved"), UCKSessionsProfileData::IsReservedAttributeKey(TEXT("CKS_ANYTHING")));
	TestTrue(TEXT("Engine map key is reserved"), UCKSessionsProfileData::IsReservedAttributeKey(SETTING_MAPNAME));
	TestFalse(TEXT("Custom keys are free"), UCKSessionsProfileData::IsReservedAttributeKey(TEXT("Difficulty")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsOnlineAttributesTest, "CoopKit.Sessions.Online.Attributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsOnlineAttributesTest::RunTest(const FString& Parameters)
{
	namespace Online = CKSessions::Online;
	namespace Compat = CKSessions::Compat;
	using namespace CKSessions::Tests;

	UCKSessionsProfileData* Profile = NewObject<UCKSessionsProfileData>(GetTransientPackage());
	const FMapProperty* AttributesProperty = FindFProperty<FMapProperty>(UCKSessionsProfileData::StaticClass(), TEXT("Attributes"));
	if (!TestNotNull(TEXT("Profile has the Attributes property"), AttributesProperty))
	{
		return false;
	}
	TMap<FName, FString>& ProfileAttributes = *AttributesProperty->ContainerPtrToValuePtr<TMap<FName, FString>>(Profile);
	ProfileAttributes.Add(TEXT("Mode"), TEXT("Coop"));
	ProfileAttributes.Add(TEXT("Difficulty"), TEXT("Normal"));

	// Keys: empty and reserved keys are refused, the first bad key is reported.
	TestTrue(TEXT("Custom key is valid"), CKSessions::Online::IsValidAttributeKey(TEXT("MatchId")));
	TestFalse(TEXT("Empty key is invalid"), CKSessions::Online::IsValidAttributeKey(NAME_None));
	TestFalse(TEXT("Plugin key is invalid"), CKSessions::Online::IsValidAttributeKey(CKSessions::Online::SessionTypeKey));
	TestFalse(TEXT("Engine key is invalid"), CKSessions::Online::IsValidAttributeKey(SETTING_MAPNAME));
	FName InvalidKey;
	TestTrue(TEXT("Empty map is valid"), CKSessions::Online::AreValidAttributeKeys(TMap<FName, FString>(), InvalidKey));
	TMap<FName, FString> WithReserved;
	WithReserved.Add(TEXT("MatchId"), TEXT("M1"));
	WithReserved.Add(CKSessions::Online::MatchInProgressKey, TEXT("true"));
	TestFalse(TEXT("Map with a reserved key is invalid"), CKSessions::Online::AreValidAttributeKeys(WithReserved, InvalidKey));
	TestTrue(TEXT("Reserved key reported"), InvalidKey == CKSessions::Online::MatchInProgressKey);

	// Runtime attributes extend the profile and win over it.
	TMap<FName, FString> Overrides;
	Overrides.Add(TEXT("Difficulty"), TEXT("Hard"));
	Overrides.Add(TEXT("MatchId"), TEXT("M1"));
	const TMap<FName, FString> Merged = CKSessions::Online::MergeAttributes(*Profile, Overrides);
	TestEqual(TEXT("Merged count"), Merged.Num(), 3);
	TestEqual(TEXT("Profile attribute kept"), Merged.FindRef(TEXT("Mode")), FString(TEXT("Coop")));
	TestEqual(TEXT("Runtime attribute wins"), Merged.FindRef(TEXT("Difficulty")), FString(TEXT("Hard")));
	TestEqual(TEXT("Runtime attribute added"), Merged.FindRef(TEXT("MatchId")), FString(TEXT("M1")));

	FOnlineSessionSettings Settings;
	CKSessions::Online::BuildSessionSettings(*Profile, true, CKSessions::Online::EHostKind::ListenServer, TEXT("/Game/Maps/L_Lobby"), Overrides, Settings);
	FString Advertised;
	TestTrue(TEXT("Runtime attribute advertised"), Settings.Get(TEXT("MatchId"), Advertised) && Advertised == TEXT("M1"));
	TestTrue(TEXT("Override advertised"), Settings.Get(TEXT("Difficulty"), Advertised) && Advertised == TEXT("Hard"));
	TestTrue(TEXT("Profile attribute advertised"), Settings.Get(TEXT("Mode"), Advertised) && Advertised == TEXT("Coop"));
	TestTrue(TEXT("Session type still advertised"), Settings.Get(CKSessions::Online::SessionTypeKey, Advertised) && Advertised == TEXT("Coop"));
	const FOnlineSessionSetting* MatchIdSetting = Settings.Settings.Find(TEXT("MatchId"));
	TestTrue(TEXT("Runtime attribute is visible to searches"), MatchIdSetting != nullptr
		&& MatchIdSetting->AdvertisementType == EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	FOnlineSessionSettings ProfileOnly;
	CKSessions::Online::BuildSessionSettings(*Profile, true, CKSessions::Online::EHostKind::ListenServer, TEXT("/Game/Maps/L_Lobby"), ProfileOnly);
	TestTrue(TEXT("Without overrides the profile value is advertised"), ProfileOnly.Get(TEXT("Difficulty"), Advertised) && Advertised == TEXT("Normal"));
	TestFalse(TEXT("Without overrides no MatchId"), ProfileOnly.Get(TEXT("MatchId"), Advertised));

	// Update of a live session: new keys appear, existing ones change.
	TMap<FName, FString> Update;
	Update.Add(TEXT("MatchId"), TEXT("M2"));
	CKSessions::Online::ApplyAttributes(ProfileOnly, Update);
	TestTrue(TEXT("Applied attribute advertised"), ProfileOnly.Get(TEXT("MatchId"), Advertised) && Advertised == TEXT("M2"));

	// Search: every filter attribute is an Equals query setting next to the session type.
	TMap<FName, FString> Filter;
	Filter.Add(TEXT("MatchId"), TEXT("M1"));
	FOnlineSessionSearch Search;
	CKSessions::Online::BuildSearch(*Profile, false, Filter, Search);
	const FOnlineSessionSearchParam* MatchIdParam = Search.QuerySettings.SearchParams.Find(TEXT("MatchId"));
	TestTrue(TEXT("Filter is an Equals query setting"), MatchIdParam != nullptr && MatchIdParam->ComparisonOp == EOnlineComparisonOp::Equals
		&& MatchIdParam->Data.ToString() == TEXT("M1"));
	FString SearchType;
	TestTrue(TEXT("Search still filters by session type"), Search.QuerySettings.Get(CKSessions::Online::SessionTypeKey, SearchType) && SearchType == TEXT("Coop"));
	FOnlineSessionSearch PlainSearch;
	CKSessions::Online::BuildSearch(*Profile, false, PlainSearch);
	TestNull(TEXT("Plain search has no attribute filter"), PlainSearch.QuerySettings.SearchParams.Find(TEXT("MatchId")));
	TestNull(TEXT("Profile attributes do not filter a plain search"), PlainSearch.QuerySettings.SearchParams.Find(TEXT("Mode")));

	// Client-side match: every filter attribute with the same case-sensitive value.
	TestTrue(TEXT("Empty filter matches"), CKSessions::Online::MatchesAttributes(Settings, TMap<FName, FString>()));
	TestTrue(TEXT("Same value matches"), CKSessions::Online::MatchesAttributes(Settings, Filter));
	TMap<FName, FString> OtherMatch;
	OtherMatch.Add(TEXT("MatchId"), TEXT("M2"));
	TestFalse(TEXT("Other value does not match"), CKSessions::Online::MatchesAttributes(Settings, OtherMatch));
	TMap<FName, FString> OtherCase;
	OtherCase.Add(TEXT("MatchId"), TEXT("m1"));
	TestFalse(TEXT("Value comparison is case-sensitive"), CKSessions::Online::MatchesAttributes(Settings, OtherCase));
	TMap<FName, FString> TwoKeys = Filter;
	TwoKeys.Add(TEXT("Mode"), TEXT("Coop"));
	TestTrue(TEXT("Every filter attribute matches"), CKSessions::Online::MatchesAttributes(Settings, TwoKeys));
	TwoKeys.Add(TEXT("Region"), TEXT("EU"));
	TestFalse(TEXT("Missing attribute does not match"), CKSessions::Online::MatchesAttributes(Settings, TwoKeys));

	// Reading back (the lost session of On Host Lost): custom string attributes only, no plugin or engine keys.
	FOnlineSessionSettings WithOtherTypes = Settings;
	WithOtherTypes.Set(TEXT("Score"), 7, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	const TMap<FName, FString> ReadBack = CKSessions::Online::ReadAttributes(WithOtherTypes);
	TestEqual(TEXT("Read back: profile and runtime attributes"), ReadBack.Num(), 3);
	TestEqual(TEXT("Read back: runtime attribute"), ReadBack.FindRef(TEXT("MatchId")), FString(TEXT("M1")));
	TestEqual(TEXT("Read back: override"), ReadBack.FindRef(TEXT("Difficulty")), FString(TEXT("Hard")));
	TestEqual(TEXT("Read back: profile attribute"), ReadBack.FindRef(TEXT("Mode")), FString(TEXT("Coop")));
	TestFalse(TEXT("Read back: no session type"), ReadBack.Contains(CKSessions::Online::SessionTypeKey));
	TestFalse(TEXT("Read back: no match state"), ReadBack.Contains(CKSessions::Online::MatchInProgressKey));
	TestFalse(TEXT("Read back: no map name"), ReadBack.Contains(SETTING_MAPNAME));
	TestFalse(TEXT("Read back: no number setting"), ReadBack.Contains(TEXT("Score")));
	TestTrue(TEXT("Read back: empty settings give no attributes"), CKSessions::Online::ReadAttributes(FOnlineSessionSettings()).IsEmpty());

	// Quick play picks the fullest session, then the lower ping.
	TArray<FOnlineSessionSearchResult> Results;
	TestEqual(TEXT("No results: no pick"), CKSessions::Online::PickQuickPlayResult(Results), static_cast<int32>(INDEX_NONE));
	Results.Add(MakeResult(Settings, 3));
	Results.Last().PingInMs = 20;
	Results.Add(MakeResult(Settings, 1));
	Results.Last().PingInMs = 90;
	Results.Add(MakeResult(Settings, 1));
	Results.Last().PingInMs = 40;
	Results.Add(MakeResult(Settings, 2));
	Results.Last().PingInMs = 5;
	TestEqual(TEXT("Fullest session with the lower ping"), CKSessions::Online::PickQuickPlayResult(Results), 2);

	// A group needs a slot for every member: the fullest session that still has room for all of them.
	TestEqual(TEXT("Open slots of a result"), CKSessions::Online::GetOpenSlots(Results[0]), 3);
	TestEqual(TEXT("Group of 1 picks like a single player"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 1), 2);
	TestEqual(TEXT("Group below 1 counts as 1"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 0), 2);
	TestEqual(TEXT("Group of 2 skips the sessions with one slot"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 2), 3);
	TestEqual(TEXT("Group of 3 takes the only session with room"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 3), 0);
	TestEqual(TEXT("Group of 4 fits nowhere: the group hosts"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 4), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Group, no results: no pick"), CKSessions::Online::PickQuickPlayResultForGroup(TArray<FOnlineSessionSearchResult>(), 2), static_cast<int32>(INDEX_NONE));
	Results.Add(MakeResult(Settings, 2));
	Results.Last().PingInMs = 1;
	TestEqual(TEXT("Group of 2: tie on slots goes to the lower ping"), CKSessions::Online::PickQuickPlayResultForGroup(Results, 2), 4);

	// What the host tells a removed player: the text carries the kind and the reason, other errors are not mistaken for it.
	bool bBan = false;
	FString Reason;
	TestTrue(TEXT("Kick message is read back"), CKSessions::Online::ParseRemovalMessage(CKSessions::Online::MakeRemovalMessage(false, TEXT("AFK")), bBan, Reason));
	TestFalse(TEXT("Kick is not a ban"), bBan);
	TestEqual(TEXT("Kick reason"), Reason, FString(TEXT("AFK")));
	TestTrue(TEXT("Ban message is read back"), CKSessions::Online::ParseRemovalMessage(CKSessions::Online::MakeRemovalMessage(true, TEXT("Griefing: spawn camping")), bBan, Reason));
	TestTrue(TEXT("Ban is a ban"), bBan);
	TestEqual(TEXT("Ban reason keeps its punctuation"), Reason, FString(TEXT("Griefing: spawn camping")));
	TestTrue(TEXT("An empty reason gets a default"), CKSessions::Online::ParseRemovalMessage(CKSessions::Online::MakeRemovalMessage(true, FString()), bBan, Reason) && !Reason.IsEmpty());
	TestFalse(TEXT("Other errors are not removals"), CKSessions::Online::ParseRemovalMessage(TEXT("Server full."), bBan, Reason));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
