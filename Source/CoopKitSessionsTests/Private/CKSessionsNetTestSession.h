// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UGameInstance;
class ULevelEditorPlaySettings;
class UNetDriver;
class UWorld;

namespace CKSessions::Tests
{
	/** Which machine hosts the game in a PIE network session. */
	enum class ENetSessionHost : uint8
	{
		/** Play As Client: an in-process dedicated server (NM_DedicatedServer) plus remote clients. */
		DedicatedServer,
		/** Play As Listen Server: the host is also a player (NM_ListenServer) plus remote clients. */
		ListenServer
	};

	/** How the clients reach the server. */
	enum class ENetClientConnect : uint8
	{
		/** PIE connects every client to the server at start (Play As Client / Play As Listen Server). */
		Automatic,
		/**
		 * Every instance starts standalone and the test connects it (sessions, join in progress): PIE Standalone, plus an
		 * in-process dedicated server for DedicatedServer. For ListenServer the host is instance 0, still standalone at
		 * start; the test makes it a listen server.
		 */
		Manual
	};

	/** Settings of a PIE network session started by a replication test. */
	struct FNetSessionSettings
	{
		ENetSessionHost Host = ENetSessionHost::DedicatedServer;

		ENetClientConnect ClientConnect = ENetClientConnect::Automatic;

		/** Remote clients, excluding a listen server host. */
		int32 RemoteClients = 2;

		/**
		 * True: the session sets PacketLagMs and PacketLossPercent on every net driver once the players connect,
		 * and on every net driver that appears later (a client that joins or reconnects, a host that starts listening).
		 * False: the session leaves the net drivers alone, for tests that drive emulation through the Play settings.
		 */
		bool bEmulateOnNetDrivers = true;

		/** One-way outgoing delay added on every net driver, in milliseconds (Net PktLag). */
		int32 PacketLagMs = 150;

		/** Outgoing packet loss on every net driver, in percent (Net PktLoss). */
		int32 PacketLossPercent = 2;

		/** Frame rate cap (t.MaxFPS) while the session runs, so that tick budgets mean the same on any machine. */
		float MaxFps = 30.0f;

		/** Wall-clock limit for PIE start and player connection, in seconds. Not a replication budget. */
		float ConnectTimeoutSeconds = 60.0f;

		/** Optional extra changes to the private copy of the Play settings, applied after the net mode and client count. */
		TFunction<void(ULevelEditorPlaySettings&)> ConfigurePlaySettings;
	};

	/**
	 * In-process PIE network session for automation tests: one server world and several client worlds
	 * in the same editor process, with packet lag and loss emulated on every connection.
	 */
	class FNetTestSession
	{
	public:
		explicit FNetTestSession(const FNetSessionSettings& InSettings);

		/** Loads the map in the editor and queues the PIE session. Returns false (and reports errors) when PIE cannot start. */
		bool Start(FAutomationTestBase& Test, const FString& MapPackageName);

		/** Automatic mode: true when the server sees every player and every client has a replicated player state. */
		bool AreAllPlayersConnected() const;

		/** Manual mode: true when every PIE instance has begun play (the dedicated server and the standalone instances). */
		bool AreAllInstancesReady() const;

		/**
		 * Manual mode: remembers the instances by PIE index (0 is the host), so tests can reach them after they travel,
		 * and checks their net modes. Returns false (and reports errors) when an instance is in the wrong mode.
		 */
		bool CaptureInstances(FAutomationTestBase& Test);

		/**
		 * Applies PacketLagMs and PacketLossPercent to the net drivers of the session when bEmulateOnNetDrivers is set,
		 * and drops every packet when the BlockNetwork console variable is set. Automatic mode needs a driver on every
		 * world; Manual mode takes the drivers that exist (the dedicated server's). Returns false on failure.
		 */
		bool ApplyNetworkEmulation(FAutomationTestBase& Test) const;

		/**
		 * Applies the same emulation to net drivers that appeared since the last call: a client that joined, travelled or
		 * reconnected, a host that started listening, a late joiner. The scenario calls it every frame.
		 */
		void RefreshNetworkEmulation(FAutomationTestBase& Test) const;

		/**
		 * Automatic mode: adds one more client to the running session through PIE Late Join, the way "join in progress"
		 * happens in a game. AreAllPlayersConnected then waits for this client too.
		 */
		void RequestLateJoin();

		/** Requests the end of PIE and restores the frame rate cap. Safe to call more than once. */
		void Stop();

		bool IsStopped() const;

		/** Automatic mode: the server world (dedicated or listen). Manual mode: the world of the host instance. */
		UWorld* GetServerWorld() const;

		/** Automatic mode: every connected client world. */
		TArray<UWorld*> GetClientWorlds() const;

		/** Manual mode: current world of a client instance (0-based, the host excluded); changes when the client travels. */
		UWorld* GetClientWorld(int32 ClientIndex) const;

		/** Manual mode: game instance of the host (instance 0). */
		UGameInstance* GetHostGameInstance() const;

		/** Manual mode: game instance of a client instance (0-based, the host excluded); the same across travel. */
		UGameInstance* GetClientGameInstance(int32 ClientIndex) const;

		/** Players the server must see: remote clients, late joiners and the listen server host. */
		int32 GetExpectedPlayerCount() const;

		/** Short description for test output, e.g. "dedicated server + 2 clients, lag 150 ms, loss 2%". */
		FString Describe() const;

		const FNetSessionSettings& GetSettings() const
		{
			return Settings;
		}

	private:
		UWorld* GetWorldOfHandle(FName ContextHandle) const;

		/** Sets the emulation on every PIE net driver not set yet. Returns how many PIE worlds have no net driver. */
		int32 EmulateNewDrivers(FAutomationTestBase& Test) const;

		FNetSessionSettings Settings;
		int32 LateJoinClients = 0;
		FName HostContextHandle;
		TArray<FName> ClientContextHandles;
		mutable TArray<TWeakObjectPtr<UNetDriver>> EmulatedDrivers;
		mutable bool bBlockNoticeShown = false;
		float SavedMaxFps = 0.0f;
		bool bFpsCapApplied = false;
	};

	/** One step of a replication scenario: an action on the server and a condition checked on the clients. */
	struct FNetStep
	{
		/** Name shown in the test report, e.g. "value reaches every client". */
		FString Name;

		/** Runs once when the step starts, usually a change made on the server. May be empty. */
		TFunction<void()> Enter;

		/** Checked every editor frame; the step passes on the first frame it returns true. */
		TFunction<bool()> IsDone;

		/** Frames allowed for IsDone to become true. At the session frame cap 30 ticks are about one second. */
		int32 MaxTicks = 45;
	};

	/**
	 * Latent command that drives a whole replication scenario: waits for every player to connect (Automatic) or for
	 * every instance to begin play (Manual), enables network emulation, runs the steps in order and always ends PIE,
	 * also after a failure. Reports the ticks and milliseconds each step took, so the report shows the real delay.
	 */
	class FNetScenarioCommand : public IAutomationLatentCommand
	{
	public:
		FNetScenarioCommand(FAutomationTestBase& InTest, TSharedRef<FNetTestSession> InSession, TArray<FNetStep> InSteps);

		virtual bool Update() override;

	private:
		enum class EPhase : uint8
		{
			Connecting,
			Steps,
			Stopping,
			Done
		};

		void Fail(const FString& Message);

		FAutomationTestBase& Test;
		TSharedRef<FNetTestSession> Session;
		TArray<FNetStep> Steps;
		EPhase Phase = EPhase::Connecting;
		int32 StepIndex = 0;
		int32 StepTicks = 0;
		bool bStepEntered = false;
		double PhaseStartSeconds = 0.0;
	};
}
