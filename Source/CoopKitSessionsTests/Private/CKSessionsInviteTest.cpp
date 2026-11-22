// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsNetTestSession.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsSettings.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTestInviteRecorder.h"
#include "CKSessionsTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Misc/AutomationTest.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* InviteMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");
		const FName InviteGroupKey(TEXT("Group"));
		const FName InviteModeKey(TEXT("Mode"));

		/** One Null search (5 s) plus room for a busy machine, at the 30 fps cap of the session. */
		constexpr int32 InviteSearchTicks = 450;

		/** Opening a map as a listen server and creating the session, or leaving one host, resolving, travelling and logging in. */
		constexpr int32 InviteTravelTicks = 450;

		/** A client notices the silent host (5 s timeout) and starts to reconnect. */
		constexpr int32 InviteNoticeTicks = 450;

		/** Frames an ignored or event-only invite is watched: nothing may travel in that time. */
		constexpr int32 InviteQuietTicks = 60;

		/** Client connection timeout for the host that goes silent: Play In Editor turns timeouts off (UNetDriver::InitBase). */
		constexpr float InviteClientTimeoutSeconds = 5.0f;

		/** Minimal session info, so a hand-made search result counts as valid without a real online subsystem. */
		class FInviteTestSessionInfo final : public FOnlineSessionInfo
		{
		public:
			FInviteTestSessionInfo()
				: SessionId(FUniqueNetIdString::Create(TEXT("InviteTestSession"), TEXT("TEST")))
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

		/**
		 * A valid search result that is never joined (its session info belongs to no online subsystem): Type is the advertised
		 * CKS_TYPE, empty for none; PartyType adds the key the CoopKit Party plugin advertises.
		 */
		FOnlineSessionSearchResult MakeHandMadeResult(const FString& Type, const FString& PartyType = FString())
		{
			FOnlineSessionSearchResult Result;
			Result.Session.OwningUserId = FUniqueNetIdString::Create(TEXT("InviteTestHost"), TEXT("TEST"));
			Result.Session.SessionInfo = MakeShared<FInviteTestSessionInfo>();
			if (!Type.IsEmpty())
			{
				Result.Session.SessionSettings.Set(CKSessions::Online::SessionTypeKey, Type, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
			}
			if (!PartyType.IsEmpty())
			{
				Result.Session.SessionSettings.Set(FName(TEXT("CKPARTY_TYPE")), PartyType, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
			}
			Result.Session.SessionSettings.Set(InviteModeKey, FString(TEXT("Survival")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
			return Result;
		}

		UCKSessionsSubsystem* InviteSessionsOf(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		IOnlineSessionPtr OnlineSessionsOf(UGameInstance* GameInstance)
		{
			const UWorld* World = GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
			const IOnlineSubsystem* OnlineSubsystem = World != nullptr ? ::Online::GetSubsystem(World) : nullptr;
			return OnlineSubsystem != nullptr ? OnlineSubsystem->GetSessionInterface() : nullptr;
		}

		/**
		 * The platform's path of an accepted invite: Steam and EOS call the session interface's On Session User Invite
		 * Accepted. Null never does, so the test fires the same delegate of the instance's own (Null) session interface.
		 */
		void AcceptInviteLikePlatform(UGameInstance* GameInstance, const FOnlineSessionSearchResult& Result, const bool bWasSuccessful = true)
		{
			const UWorld* World = GameInstance->GetWorld();
			const IOnlineSubsystem* OnlineSubsystem = ::Online::GetSubsystem(World);
			const IOnlineIdentityPtr Identity = OnlineSubsystem != nullptr ? OnlineSubsystem->GetIdentityInterface() : nullptr;
			const FUniqueNetIdPtr UserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
			OnlineSessionsOf(GameInstance)->TriggerOnSessionUserInviteAcceptedDelegates(bWasSuccessful, 0, UserId, Result);
		}

		int32 CountInvitePlayers(const UWorld* World)
		{
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		bool IsInviteListenHost(UGameInstance* GameInstance, const UWorld* World)
		{
			const UCKSessionsSubsystem* Sessions = InviteSessionsOf(GameInstance);
			return Sessions != nullptr && Sessions->GetState() == ECKSessionsState::InSession && Sessions->IsHost()
				&& World != nullptr && World->GetNetMode() == NM_ListenServer;
		}

		TMap<FName, FString> MakeInviteGroup(const FString& Group)
		{
			TMap<FName, FString> Attributes;
			Attributes.Add(InviteGroupKey, Group);
			return Attributes;
		}

		TSoftObjectPtr<UWorld> GetInviteMap()
		{
			return TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(InviteMap) + TEXT(".L_SessionsDemo")));
		}

		using FInviteRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestInviteRecorder>>;

		FInviteRecorderRef MakeInviteRecorder()
		{
			return MakeShared<TStrongObjectPtr<UCKSessionsTestInviteRecorder>>(NewObject<UCKSessionsTestInviteRecorder>(GetTransientPackage()));
		}

		/** A step that waits a few frames: an ignored or event-only invite must not start anything in that time. */
		FNetStep MakeQuietStep(const FString& Name, TFunction<void()> Enter, TFunction<bool()> StillQuiet)
		{
			const TSharedRef<int32> Ticks = MakeShared<int32>(0);
			return { Name,
				[Ticks, Enter]()
				{
					*Ticks = 0;
					if (Enter)
					{
						Enter();
					}
				},
				[Ticks, StillQuiet]()
				{
					// A quiet step fails by timing out: it only passes once StillQuiet held for every frame.
					if (!StillQuiet())
					{
						*Ticks = -1000000;
					}
					return ++*Ticks >= InviteQuietTicks;
				},
				InviteQuietTicks + 5 };
		}

		/**
		 * One standalone instance, no session: the subsystem listens for invites from the start, ignores invites to
		 * sessions of another Session Type (a party) and failed accepts, only fires the event with policy Event Only and
		 * in the mode of external host loss handling, and refuses friends and invites with Not Supported on Null.
		 */
		bool RunInviteFilterScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 1;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const FInviteRecorderRef Recorder = MakeInviteRecorder();
			FAutomationTestBase* TestPtr = &Test;
			const TFunction<UGameInstance*()> Instance = [Session]() { return Session->GetClientGameInstance(0); };
			const TFunction<bool()> StaysIdle = [Instance]()
			{
				const UWorld* World = Instance()->GetWorld();
				return InviteSessionsOf(Instance())->GetState() == ECKSessionsState::Idle && World != nullptr && World->GetNetMode() == NM_Standalone;
			};

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("The subsystem listens for accepted invites from the start, before any request; the project default is Event Only"),
				[TestPtr, Instance, Recorder]()
				{
					UCKSessionsSubsystem* Sessions = InviteSessionsOf(Instance());
					const IOnlineSessionPtr OnlineSessions = OnlineSessionsOf(Instance());
					TestPtr->TestTrue(TEXT("On Session User Invite Accepted of the instance's session interface has a listener"),
						OnlineSessions.IsValid() && OnlineSessions->OnSessionUserInviteAcceptedDelegates.IsBound());
					TestPtr->TestEqual(TEXT("Idle, no request made"), UEnum::GetValueAsString(Sessions->GetState()), UEnum::GetValueAsString(ECKSessionsState::Idle));
					// The demo panel of this map switches its own subsystem to Join At Once; the plugin's setting stays Event Only.
					TestPtr->TestEqual(TEXT("Invite Accept Policy of the project settings is Event Only (1.0.0 behavior plus the event)"),
						UEnum::GetValueAsString(UCKSessionsSettings::Get().GetInviteAcceptPolicy()), UEnum::GetValueAsString(ECKSessionsInvitePolicy::EventOnly));
					(*Recorder)->Watch(*Sessions);
				},
				[]() { return true; },
				2 });

			Steps.Add(MakeQuietStep(TEXT("Invites to another game's session, to a party session and a failed accept are ignored: no event, no travel"),
				[Instance]()
				{
					AcceptInviteLikePlatform(Instance(), MakeHandMadeResult(TEXT("OtherGame")));
					AcceptInviteLikePlatform(Instance(), MakeHandMadeResult(FString(), TEXT("CKParty")));
					AcceptInviteLikePlatform(Instance(), MakeHandMadeResult(TEXT("Coop")), false);
				},
				[StaysIdle, Recorder]() { return StaysIdle() && (*Recorder)->GetInvites().IsEmpty(); }));

			Steps.Add(MakeQuietStep(TEXT("Policy Event Only: an invite to a Coop session fires On Invite Accepted with Joining false and stays idle"),
				[Instance]()
				{
					InviteSessionsOf(Instance())->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::EventOnly);
					AcceptInviteLikePlatform(Instance(), MakeHandMadeResult(TEXT("Coop")));
				},
				[StaysIdle, Recorder]() { return StaysIdle() && (*Recorder)->GetInvites().Num() == 1; }));

			Steps.Add({ TEXT("The event carries the session, its id and its custom attributes without reserved keys"),
				[TestPtr, Recorder]()
				{
					const FCKSessionsInvite& Invite = (*Recorder)->GetInvites()[0];
					TestPtr->TestFalse(TEXT("Joining is false with Event Only"), Invite.bJoining);
					TestPtr->TestTrue(TEXT("The session is valid"), Invite.Session.IsValid());
					TestPtr->TestEqual(TEXT("Session id"), Invite.SessionId, FString(TEXT("InviteTestSession")));
					TestPtr->TestEqual(TEXT("Attribute Mode = Survival"), Invite.Attributes.FindRef(InviteModeKey), FString(TEXT("Survival")));
					TestPtr->TestFalse(TEXT("No reserved CKS_TYPE among the attributes"), Invite.Attributes.Contains(CKSessions::Online::SessionTypeKey));
					TestPtr->TestEqual(TEXT("No join request"), (*Recorder)->GetJoinFailures().Num() + (*Recorder)->GetJoinSuccessCount(), 0);
				},
				[]() { return true; },
				2 });

			Steps.Add(MakeQuietStep(TEXT("Join At Once with Handle Host Loss Externally on: the invite only fires the event, the project decides"),
				[Instance]()
				{
					UCKSessionsSubsystem* Sessions = InviteSessionsOf(Instance());
					Sessions->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::JoinAtOnce);
					Sessions->SetHandleHostLossExternally(true);
					AcceptInviteLikePlatform(Instance(), MakeHandMadeResult(TEXT("Coop")));
				},
				[StaysIdle, Recorder]()
				{
					return StaysIdle() && (*Recorder)->GetInvites().Num() == 2 && !(*Recorder)->GetInvites()[1].bJoining;
				}));

			Steps.Add({ TEXT("Null: Show Invite UI, Read Friends, Invite Friend and Join Friend refuse with Not Supported, nothing breaks"),
				[TestPtr, Instance, Recorder]()
				{
					UCKSessionsSubsystem* Sessions = InviteSessionsOf(Instance());
					Sessions->SetHandleHostLossExternally(false);
					const FString NotSupported = UEnum::GetValueAsString(ECKSessionsFailure::NotSupported);
					TestPtr->TestEqual(TEXT("Show Invite UI"), UEnum::GetValueAsString(Sessions->ShowInviteUI()), NotSupported);
					TestPtr->TestFalse(TEXT("Read Friends is refused at once"), Sessions->ReadFriends());
					TestPtr->TestEqual(TEXT("On Read Friends Complete fired once"), (*Recorder)->GetReadFriendsCount(), 1);
					TestPtr->TestFalse(TEXT("On Read Friends Complete: failed"), (*Recorder)->GetLastReadFriendsSuccess());
					TestPtr->TestEqual(TEXT("On Read Friends Complete: Not Supported"), UEnum::GetValueAsString((*Recorder)->GetLastReadFriendsFailure()), NotSupported);
					TestPtr->TestTrue(TEXT("No friends"), Sessions->GetFriends().IsEmpty());
					TestPtr->TestEqual(TEXT("Invite Friend"), UEnum::GetValueAsString(Sessions->InviteFriend(FCKSessionsFriend())), NotSupported);
					TestPtr->TestFalse(TEXT("Join Friend is refused at once"), Sessions->JoinFriend(FCKSessionsFriend()));
					TestPtr->TestTrue(TEXT("On Join Complete: Not Supported"), (*Recorder)->GetJoinFailures().Num() == 1
						&& (*Recorder)->GetJoinFailures()[0] == ECKSessionsFailure::NotSupported);
					TestPtr->TestFalse(TEXT("Join Invited Session refuses an empty result"), Sessions->JoinInvitedSession(FCKSessionsSearchResult()));
					TestPtr->TestTrue(TEXT("On Join Complete: Invalid Result"), (*Recorder)->GetJoinFailures().Num() == 2
						&& (*Recorder)->GetJoinFailures()[1] == ECKSessionsFailure::InvalidResult);
				},
				[StaysIdle]() { return StaysIdle(); },
				5 });

			if (!Session->Start(Test, InviteMap))
			{
				return false;
			}
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		/** Search results of the two hosts, found by client B, by their Group attribute. */
		struct FInviteJoinState
		{
			FOnlineSessionSearchResult SessionOfH;
			FOnlineSessionSearchResult SessionOfA;
		};

		/** Client indices: A hosts a second game, B accepts the invites. */
		constexpr int32 InviteClientA = 0;
		constexpr int32 InviteClientB = 1;

		/**
		 * Host H and client A each host a game (Group = H, Group = A); client B accepts invites the way Steam and EOS hand
		 * them over (the session interface's delegate) from every state: the menu, the lobby, a running match and while
		 * reconnecting to a host that went silent. Every time B ends up in the invited game; Event Only and the external
		 * host loss mode only fire the event.
		 */
		bool RunInviteJoinScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TSharedRef<FInviteJoinState> State = MakeShared<FInviteJoinState>();
			const FInviteRecorderRef Recorder = MakeInviteRecorder();
			FAutomationTestBase* TestPtr = &Test;

			const TFunction<UCKSessionsSubsystem*()> SessionsB = [Session]() { return InviteSessionsOf(Session->GetClientGameInstance(InviteClientB)); };
			const TFunction<UWorld*()> WorldH = [Session]() { return Session->GetServerWorld(); };
			const TFunction<UWorld*()> WorldA = [Session]() { return Session->GetClientWorld(InviteClientA); };
			// B plays in a host's game: in session, a client of that host, and the host counts two players while the other counts one.
			const TFunction<bool(const TFunction<UWorld*()>&, const TFunction<UWorld*()>&)> BIsWith = [Session, SessionsB](const TFunction<UWorld*()>& Host,
				const TFunction<UWorld*()>& Other)
			{
				const UWorld* World = Session->GetClientWorld(InviteClientB);
				return SessionsB()->GetState() == ECKSessionsState::InSession && World != nullptr && World->GetNetMode() == NM_Client
					&& CountInvitePlayers(World) == 2 && CountInvitePlayers(Host()) == 2 && CountInvitePlayers(Other()) == 1;
			};
			const TFunction<void(const TCHAR*, int32, bool)> CheckLastInvite = [TestPtr, Recorder](const TCHAR* Where, const int32 Count, const bool bJoining)
			{
				const TArray<FCKSessionsInvite>& Invites = (*Recorder)->GetInvites();
				TestPtr->TestEqual(*FString::Printf(TEXT("%s: On Invite Accepted fired"), Where), Invites.Num(), Count);
				if (Invites.Num() == Count)
				{
					TestPtr->TestEqual(*FString::Printf(TEXT("%s: Joining"), Where), Invites.Last().bJoining, bJoining);
				}
			};

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("H hosts the demo map with Group = H"),
				[Session]() { InviteSessionsOf(Session->GetHostGameInstance())->HostSessionWithAttributes(nullptr, GetInviteMap(), MakeInviteGroup(TEXT("H"))); },
				[Session]() { return IsInviteListenHost(Session->GetHostGameInstance(), Session->GetServerWorld()); },
				InviteTravelTicks });

			Steps.Add({ TEXT("A hosts the demo map with Group = A"),
				[Session]() { InviteSessionsOf(Session->GetClientGameInstance(InviteClientA))->HostSessionWithAttributes(nullptr, GetInviteMap(), MakeInviteGroup(TEXT("A"))); },
				[Session]() { return IsInviteListenHost(Session->GetClientGameInstance(InviteClientA), Session->GetClientWorld(InviteClientA)); },
				InviteTravelTicks });

			Steps.Add({ TEXT("B finds both games (the search results an invite carries)"),
				[SessionsB, Recorder]()
				{
					(*Recorder)->Watch(*SessionsB());
					// Not the default (Event Only): B joins by itself, as in the demo.
					SessionsB()->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::JoinAtOnce);
					SessionsB()->FindSessions(nullptr);
				},
				[SessionsB, State]()
				{
					UCKSessionsSubsystem* Sessions = SessionsB();
					if (Sessions->GetState() != ECKSessionsState::Idle)
					{
						return false;
					}
					State->SessionOfH = FOnlineSessionSearchResult();
					State->SessionOfA = FOnlineSessionSearchResult();
					for (const FCKSessionsSearchResult& Result : Sessions->GetSearchResults())
					{
						const FString Group = CKSessions::Online::ReadAttributes(Result.GetNative().Session.SessionSettings).FindRef(InviteGroupKey);
						(Group == TEXT("H") ? State->SessionOfH : State->SessionOfA) = Result.GetNative();
					}
					if (State->SessionOfH.IsValid() && State->SessionOfA.IsValid())
					{
						return true;
					}
					// The first search may have started before A's session existed: search again.
					Sessions->FindSessions(nullptr);
					return false;
				},
				InviteSearchTicks * 2 });

			Steps.Add({ TEXT("Menu: B accepts an invite to H's game and joins H"),
				[Session, State]() { AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfH); },
				[BIsWith, WorldH, WorldA]() { return BIsWith(WorldH, WorldA); },
				InviteTravelTicks });
			Steps.Add({ TEXT("Menu: one event with Joining true"), [CheckLastInvite]() { CheckLastInvite(TEXT("Menu"), 1, true); }, []() { return true; }, 2 });

			Steps.Add({ TEXT("Lobby: B, in H's lobby, accepts an invite to A's game, leaves H and joins A"),
				[Session, State]() { AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfA); },
				[BIsWith, WorldH, WorldA]() { return BIsWith(WorldA, WorldH); },
				InviteTravelTicks });
			Steps.Add({ TEXT("Lobby: one more event with Joining true"), [CheckLastInvite]() { CheckLastInvite(TEXT("Lobby"), 2, true); }, []() { return true; }, 2 });

			Steps.Add(MakeQuietStep(TEXT("Event Only: B, in A's game, accepts an invite to H's game; only the event fires, B stays with A"),
				[Session, State, SessionsB]()
				{
					SessionsB()->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::EventOnly);
					AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfH);
				},
				[BIsWith, WorldH, WorldA, Recorder]() { return BIsWith(WorldA, WorldH) && (*Recorder)->GetInvites().Num() == 3 && !(*Recorder)->GetInvites()[2].bJoining; }));

			Steps.Add(MakeQuietStep(TEXT("Handle Host Loss Externally: B accepts an invite to H's game; only the event fires, B stays with A"),
				[Session, State, SessionsB]()
				{
					SessionsB()->SetInviteAcceptPolicy(ECKSessionsInvitePolicy::JoinAtOnce);
					SessionsB()->SetHandleHostLossExternally(true);
					AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfH);
				},
				[BIsWith, WorldH, WorldA, Recorder]() { return BIsWith(WorldA, WorldH) && (*Recorder)->GetInvites().Num() == 4 && !(*Recorder)->GetInvites()[3].bJoining; }));

			Steps.Add({ TEXT("Match: A starts the match; B, in the running match, accepts an invite to H's game and joins H"),
				[Session, State, SessionsB]()
				{
					SessionsB()->SetHandleHostLossExternally(false);
					InviteSessionsOf(Session->GetClientGameInstance(InviteClientA))->StartMatch();
					AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfH);
				},
				[BIsWith, WorldH, WorldA]() { return BIsWith(WorldH, WorldA); },
				InviteTravelTicks });
			Steps.Add({ TEXT("Match: one more event with Joining true; A ends its match"),
				[CheckLastInvite, Session]()
				{
					CheckLastInvite(TEXT("Match"), 5, true);
					InviteSessionsOf(Session->GetClientGameInstance(InviteClientA))->EndMatch();
				},
				[]() { return true; },
				2 });

			Steps.Add({ TEXT("H goes silent (every packet dropped): B times out and reconnects"),
				[Session]()
				{
					UWorld* World = Session->GetClientWorld(InviteClientB);
					if (UNetDriver* Driver = World != nullptr ? World->GetNetDriver() : nullptr)
					{
						Driver->bNoTimeouts = false;
						Driver->ConnectionTimeout = InviteClientTimeoutSeconds;
						Driver->InitialConnectTimeout = InviteClientTimeoutSeconds;
					}
#if DO_ENABLE_NET_TEST
					UWorld* HostWorld = Session->GetServerWorld();
					if (UNetDriver* HostDriver = HostWorld != nullptr ? HostWorld->GetNetDriver() : nullptr)
					{
						FPacketSimulationSettings Silence = HostDriver->PacketSimulationSettings;
						Silence.PktLoss = 100;
						HostDriver->SetPacketSimulationSettings(Silence);
					}
#endif
				},
				[SessionsB]() { return SessionsB()->GetState() == ECKSessionsState::Reconnecting; },
				InviteNoticeTicks });

			Steps.Add({ TEXT("Reconnecting: B accepts an invite to A's game, stops reconnecting and joins A"),
				[Session, State]() { AcceptInviteLikePlatform(Session->GetClientGameInstance(InviteClientB), State->SessionOfA); },
				[Session, SessionsB, WorldA]()
				{
					const UWorld* World = Session->GetClientWorld(InviteClientB);
					return SessionsB()->GetState() == ECKSessionsState::InSession && World != nullptr && World->GetNetMode() == NM_Client
						&& CountInvitePlayers(World) == 2 && CountInvitePlayers(WorldA()) == 2;
				},
				InviteTravelTicks });

			Steps.Add({ TEXT("Every join by invite succeeded, none failed"),
				[TestPtr, Recorder, CheckLastInvite]()
				{
					CheckLastInvite(TEXT("Reconnecting"), 6, true);
					TestPtr->TestEqual(TEXT("On Join Complete succeeded four times (menu, lobby, match, reconnecting)"), (*Recorder)->GetJoinSuccessCount(), 4);
					FString Failures;
					for (const ECKSessionsFailure Failure : (*Recorder)->GetJoinFailures())
					{
						Failures += UEnum::GetValueAsString(Failure) + TEXT(" ");
					}
					TestPtr->TestTrue(*FString::Printf(TEXT("No failed join (seen: %s)"), *Failures), (*Recorder)->GetJoinFailures().IsEmpty());
				},
				[]() { return true; },
				2 });

			// The silent host is part of the scenario: the engine reports B's connection timeout as an error.
			Test.AddExpectedErrorPlain(TEXT("UEngine::BroadcastNetworkFailure: FailureType = "), EAutomationExpectedErrorFlags::Contains, 0);

			if (!Session->Start(Test, InviteMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, budgets: search %d, travel %d, notice %d ticks"), *Session->Describe(),
				InviteSearchTicks, InviteTravelTicks, InviteNoticeTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsInviteRulesTest, "CoopKit.Sessions.Invites.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsInviteRulesTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Online;
	using CKSessions::Tests::MakeHandMadeResult;

	// Session Type of a session: CKS_TYPE only; a party advertises its own key.
	TestEqual(TEXT("Session Type of a Coop session"), GetSessionType(MakeHandMadeResult(TEXT("Coop")).Session.SessionSettings), FString(TEXT("Coop")));
	TestTrue(TEXT("A party session has no Session Type"), GetSessionType(MakeHandMadeResult(FString(), TEXT("CKParty")).Session.SessionSettings).IsEmpty());
	const TArray<FName> Types = { TEXT("Coop"), TEXT("Versus") };
	TestTrue(TEXT("Coop is ours"), IsSessionOfType(MakeHandMadeResult(TEXT("Coop")).Session.SessionSettings, Types));
	TestTrue(TEXT("Versus is ours"), IsSessionOfType(MakeHandMadeResult(TEXT("Versus")).Session.SessionSettings, Types));
	TestFalse(TEXT("OtherGame is not ours"), IsSessionOfType(MakeHandMadeResult(TEXT("OtherGame")).Session.SessionSettings, Types));
	TestFalse(TEXT("A party is not ours"), IsSessionOfType(MakeHandMadeResult(FString(), TEXT("CKParty")).Session.SessionSettings, Types));
	// Session Type is an FName and searches compare it the same way (IsJoinable): case does not matter.
	TestTrue(TEXT("Case does not matter: coop is Coop, as in searches"), IsSessionOfType(MakeHandMadeResult(TEXT("coop")).Session.SessionSettings, Types));

	// Decision table of an accepted invite.
	const ECKSessionsInvitePolicy Join = ECKSessionsInvitePolicy::JoinAtOnce;
	const ECKSessionsInvitePolicy Event = ECKSessionsInvitePolicy::EventOnly;
	TestTrue(TEXT("Own, usable, Join At Once: join"), DecideInviteAction(true, true, Join, false, false) == EInviteAction::Join);
	TestTrue(TEXT("Own, usable, Event Only: event"), DecideInviteAction(true, true, Event, false, false) == EInviteAction::EventOnly);
	TestTrue(TEXT("Join At Once with external host loss handling: event"), DecideInviteAction(true, true, Join, true, false) == EInviteAction::EventOnly);
	TestTrue(TEXT("Foreign Session Type: ignore"), DecideInviteAction(true, false, Join, false, false) == EInviteAction::Ignore);
	TestTrue(TEXT("Failed accept: ignore"), DecideInviteAction(false, true, Join, false, false) == EInviteAction::Ignore);
	TestTrue(TEXT("Dedicated server: ignore"), DecideInviteAction(true, true, Join, false, true) == EInviteAction::Ignore);
	TestTrue(TEXT("Foreign type with Event Only: still ignored"), DecideInviteAction(true, false, Event, true, false) == EInviteAction::Ignore);

	// Friends order for a menu.
	const TFunction<FCKSessionsFriend(const TCHAR*, bool, bool, bool)> MakeFriend = [](const TCHAR* Name, const bool bOnline, const bool bPlaying, const bool bJoinable)
	{
		FCKSessionsFriend Friend;
		Friend.DisplayName = Name;
		Friend.bOnline = bOnline;
		Friend.bPlayingThisGame = bPlaying;
		Friend.bJoinable = bJoinable;
		return Friend;
	};
	TArray<FCKSessionsFriend> Friends = {
		MakeFriend(TEXT("zoe"), false, false, false),
		MakeFriend(TEXT("Bob"), true, false, false),
		MakeFriend(TEXT("Carl"), true, true, false),
		MakeFriend(TEXT("anna"), true, false, false),
		MakeFriend(TEXT("Dan"), true, true, true)
	};
	SortFriends(Friends);
	TArray<FString> Order;
	for (const FCKSessionsFriend& Friend : Friends)
	{
		Order.Add(Friend.DisplayName);
	}
	TestEqual(TEXT("Joinable, playing, online (by name, any case), offline"), FString::Join(Order, TEXT(",")), FString(TEXT("Dan,Carl,anna,Bob,zoe")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsInviteFilterTest, "CoopKit.Sessions.Invites.FilterAndFriends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsInviteFilterTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunInviteFilterScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsInviteJoinTest, "CoopKit.Sessions.Invites.JoinFromAnyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsInviteJoinTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunInviteJoinScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
