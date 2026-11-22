// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsShowcaseBoard.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsPlayerComponent.h"
#include "CKSessionsShowcaseGameMode.h"
#include "CKSessionsShowcaseStand.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace CKSessions::Showcase::Board
{
	namespace
	{
		/** The pawn's center is this high over the stand's top. */
		constexpr double CenterHeight = 37.0;

		/** Bots stand this far toward the viewer; the free boards lean at FreeBoardX. */
		constexpr double BotX = 100.0;
		constexpr double FreeBoardX = 40.0;

		/**
		 * The board leans back like a lectern, so the text reads from the camera's height and the sign behind stays in view:
		 * its center (X, Z over the floor), width along Y, length along its slope, the slope's angle from the floor, the margin
		 * of the text and the text sizes.
		 */
		constexpr double BoardZ = 85.0;
		constexpr double BoardWidth = 560.0;
		constexpr double BoardLength = 180.0;
		constexpr double BoardTilt = 40.0;
		constexpr double BoardMargin = 22.0;
		constexpr float HeadingSize = 24.0f;
		constexpr float BodySize = 18.0f;

		/** Seconds between two refreshes of the board: the state changes every frame, the board only needs to follow. */
		constexpr double BoardRefreshSeconds = 0.4;

		/** Steps of the Lobby stand: which bots are ready after each step (Ann, Bo, Cy). The count starts at step 3 and a bot cancels it at step 5. */
		constexpr int32 LobbyBotCount = 3;
		constexpr int32 LobbyStepCount = 6;
		constexpr bool LobbyPlan[LobbyStepCount][LobbyBotCount] = {
			{ false, false, false }, { true, false, false }, { true, true, false }, { true, true, true }, { true, true, true }, { true, true, false } };

		/** Roster stand steps: step 0 brings the players in, step 2 Dan leaves, step 4 he comes back. */
		constexpr int32 RosterStepCount = 6;
		constexpr int32 RosterLeaveStep = 2;
		constexpr int32 RosterReturnStep = 4;

		const FLinearColor AccentTone(0.1f, 0.8f, 0.75f);
		const FLinearColor BoardTone(0.012f, 0.016f, 0.026f);

		FString DescribeValue(const UEnum* Enum, const int64 Value)
		{
			return Enum != nullptr ? Enum->GetNameStringByValue(Value) : FString();
		}
	}
}

ACKSessionsShowcaseBoard::ACKSessionsShowcaseBoard()
{
	PrimaryActorTick.bCanEverTick = true;
	StepCount = CKSessions::Showcase::Board::LobbyStepCount;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	ShapeMaterial = MaterialFinder.Object;
}

void ACKSessionsShowcaseBoard::BeginPlay()
{
	// The base starts its timer here, so the pace is set first.
	StepSeconds = Mode == ECKSessionsShowcaseMode::Roster ? 1.5f : 2.0f;
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (const ACKSessionsShowcaseStand* ShowStand = GetStand())
	{
		BaseDescription = ShowStand->GetDescription();
	}
	BuildBoard();
}

void ACKSessionsShowcaseBoard::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBoard(GetWorld()->GetTimeSeconds());

	// A bot's pawn may spawn after the bot does: put each pawn at its spot once it exists.
	if (HasAuthority() && (Mode == ECKSessionsShowcaseMode::Lobby || Mode == ECKSessionsShowcaseMode::Roster))
	{
		for (int32 Index = 0; Index < Roster.Num(); ++Index)
		{
			APlayerController* Bot = Roster[Index].Get();
			APawn* Pawn = Bot != nullptr ? Bot->GetPawn() : nullptr;
			if (Pawn != nullptr && !PlacedBots.Contains(Pawn))
			{
				PlacedBots.Add(Pawn);
				PlaceBot(Bot, GetBotSpot(Index), FRotator::ZeroRotator);
			}
		}
	}
}

// ---------- building ----------

UStaticMeshComponent* ACKSessionsShowcaseBoard::AddBlock(const FName Name, const FLinearColor& Color)
{
	UStaticMeshComponent* Block = NewObject<UStaticMeshComponent>(this, Name);
	Block->SetupAttachment(GetRootComponent());
	Block->SetStaticMesh(CubeMesh);
	Block->SetMobility(EComponentMobility::Movable);
	Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Block->RegisterComponent();
	if (ShapeMaterial != nullptr)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Block->SetMaterial(0, Material);
	}
	return Block;
}

UTextRenderComponent* ACKSessionsShowcaseBoard::AddText(const FName Name, const FVector& Location, const float Size, const FColor& Color)
{
	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this, Name);
	Text->SetupAttachment(GetRootComponent());
	// World space, not the scenario's: the scenario actor is turned to face the stands, the text reads from +X.
	Text->SetWorldLocationAndRotation(GetActorLocation() + Location, FRotator::ZeroRotator);
	Text->SetHorizontalAlignment(EHTA_Left);
	Text->SetVerticalAlignment(EVRTA_TextTop);
	Text->SetWorldSize(Size);
	Text->SetTextRenderColor(Color);
	Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Text->RegisterComponent();
	return Text;
}

void ACKSessionsShowcaseBoard::BuildBoard()
{
	using namespace CKSessions::Showcase::Board;

	// The bot stands put their live line on the stand's sign: a board behind the bots would be hidden by them.
	if (Mode == ECKSessionsShowcaseMode::Lobby || Mode == ECKSessionsShowcaseMode::Roster)
	{
		UpdateBoard(NextBoardUpdate);
		return;
	}

	// The board is a slab tilted back by BoardTilt from the vertical: Normal looks at the viewer and up, Up runs up the slope.
	const FRotator Tilt(BoardTilt, 0.0, 0.0);
	const FVector Center(FreeBoardX, 0.0, BoardZ);
	const FVector Normal = Tilt.RotateVector(FVector::ForwardVector);
	const FVector Up = Tilt.RotateVector(FVector::UpVector);

	UStaticMeshComponent* Board = AddBlock(TEXT("Board"), BoardTone);
	Board->SetWorldLocationAndRotation(GetActorLocation() + Center, Tilt);
	Board->SetRelativeScale3D(FVector(0.05, BoardWidth / 100.0, BoardLength / 100.0));

	UStaticMeshComponent* Top = AddBlock(TEXT("BoardTop"), AccentTone);
	Top->SetWorldLocationAndRotation(GetActorLocation() + Center + Up * (BoardLength * 0.5) + Normal * 0.5, Tilt);
	Top->SetRelativeScale3D(FVector(0.06, BoardWidth / 100.0, 0.05));

	// Left-aligned text starts at the viewer's left edge (+Y) and runs to the right (-Y); the lines hang from the top of the slope.
	const double LeftY = BoardWidth * 0.5 - BoardMargin;
	const FVector HeadingSpot = Center + Up * (BoardLength * 0.5 - BoardMargin * 0.6) + Normal * 3.0 + FVector(0.0, LeftY, 0.0);
	UTextRenderComponent* Heading = AddText(TEXT("BoardHeading"), HeadingSpot, HeadingSize, AccentTone.ToFColor(true));
	Heading->SetWorldRotation(Tilt);
	switch (Mode)
	{
	case ECKSessionsShowcaseMode::Session:
		Heading->SetText(FText::FromString(TEXT("Session of this game (live)")));
		break;
	case ECKSessionsShowcaseMode::Lobby:
		Heading->SetText(FText::FromString(TEXT("Lobby of this map (live)")));
		break;
	case ECKSessionsShowcaseMode::Roster:
		Heading->SetText(FText::FromString(TEXT("Records of the host (live)")));
		break;
	case ECKSessionsShowcaseMode::Api:
		Heading->SetText(FText::FromString(TEXT("Where to start")));
		break;
	}
	BoardBody = AddText(TEXT("BoardBody"), HeadingSpot - Up * (HeadingSize * 1.4), BodySize, FColor(225, 229, 236));
	BoardBody->SetWorldRotation(Tilt);
	UpdateBoard(NextBoardUpdate);
}

// ---------- the board ----------

FString ACKSessionsShowcaseBoard::MakeBoardText() const
{
	using namespace CKSessions::Showcase::Board;

	TArray<FString> Lines;
	const UWorld* World = GetWorld();
	switch (Mode)
	{
	case ECKSessionsShowcaseMode::Session:
	{
		const UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(World);
		if (Sessions != nullptr)
		{
			Lines.Add(FString::Printf(TEXT("State: %s%s%s"), *DescribeValue(StaticEnum<ECKSessionsState>(), static_cast<int64>(Sessions->GetState())),
				Sessions->IsHost() ? TEXT(", host") : TEXT(""), Sessions->IsMatchInProgress() ? TEXT(", match in progress") : TEXT("")));
			Lines.Add(FString::Printf(TEXT("Search results: %d"), Sessions->GetSearchResults().Num()));
		}
		else
		{
			Lines.Add(TEXT("State: no game instance"));
		}
		Lines.Add(TEXT("Blueprint:  Host, Find, Join Session"));
		Lines.Add(TEXT("Try it with buttons: open L_SessionsDemo, click Play."));
		Lines.Add(TEXT("Its panel opens a second window to join."));
		break;
	}
	case ECKSessionsShowcaseMode::Lobby:
	{
		const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
		if (Lobby == nullptr)
		{
			Lines.Add(TEXT("No lobby on this map yet."));
			break;
		}
		const UCKSessionsLobbyRulesData* Rules = Lobby->GetRules();
		FString Line = FString::Printf(TEXT("Phase: %s, ready %d of %d needed"), *DescribeValue(StaticEnum<ECKSessionsLobbyPhase>(), static_cast<int64>(Lobby->GetPhase())),
			Lobby->GetReadyPlayerCount(), Rules != nullptr ? Rules->GetMinPlayersToStart() : 0);
		if (Lobby->GetPhase() == ECKSessionsLobbyPhase::Countdown)
		{
			Line += FString::Printf(TEXT(", %.1f s"), Lobby->GetCountdownSecondsLeft());
		}
		Lines.Add(Line);
		break;
	}
	case ECKSessionsShowcaseMode::Roster:
	{
		const UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(World);
		const TArray<FString> Names = GetBotNames();
		if (!HasAuthority())
		{
			Lines.Add(TEXT("The host keeps the records."));
		}
		else if (Sessions != nullptr)
		{
			// Only this stand's players: the viewer's record carries the name they logged in with.
			for (const FString& Name : Names)
			{
				for (const FCKSessionsPlayerRecord& Record : Sessions->GetPlayerRecords())
				{
					if (Record.PlayerName == Name)
					{
						FString Line = FString::Printf(TEXT("%s: %s"), *Name, Record.bConnected ? TEXT("here") : TEXT("away"));
						if (Record.ReconnectCount > 0)
						{
							Line += FString::Printf(TEXT(", came back %dx"), Record.ReconnectCount);
						}
						Lines.Add(Line);
					}
				}
			}
		}
		if (Lines.IsEmpty())
		{
			Lines.Add(TEXT("No players yet."));
		}
		return FString::Join(Lines, TEXT("; "));
	}
	case ECKSessionsShowcaseMode::Api:
	{
		Lines.Add(TEXT("Blueprint, category CoopKit|Sessions:"));
		Lines.Add(TEXT("  Host Session, Find Sessions, Join Session"));
		Lines.Add(TEXT("  Set Ready, Start Match Now, Get Player Records"));
		Lines.Add(TEXT("C++:"));
		Lines.Add(TEXT("  UCKSessionsSubsystem: host, find, join, leave"));
		Lines.Add(TEXT("  UCKSessionsLobbyComponent: lobby, countdown"));
		Lines.Add(TEXT("  UCKSessionsPlayerComponent: the ready flag"));
		break;
	}
	}
	return FString::Join(Lines, TEXT("<br>"));
}

void ACKSessionsShowcaseBoard::UpdateBoard(const double Now)
{
	using namespace CKSessions::Showcase::Board;

	const bool bSignLine = Mode == ECKSessionsShowcaseMode::Lobby || Mode == ECKSessionsShowcaseMode::Roster;
	if ((BoardBody == nullptr && !bSignLine) || (Now < NextBoardUpdate && !BoardText.IsEmpty()))
	{
		return;
	}
	NextBoardUpdate = Now + BoardRefreshSeconds;
	const FString Text = MakeBoardText();
	if (Text != BoardText)
	{
		BoardText = Text;
		if (BoardBody != nullptr)
		{
			BoardBody->SetText(FText::FromString(Text));
		}
		else if (ACKSessionsShowcaseStand* ShowStand = GetStand())
		{
			// The live line goes under the stand's own description.
			ShowStand->SetTexts(ShowStand->GetTitle(), FText::FromString(BaseDescription.ToString() + TEXT("\n") + Text));
		}
	}
}

// ---------- the scenario ----------

TArray<FString> ACKSessionsShowcaseBoard::GetBotNames() const
{
	if (Mode == ECKSessionsShowcaseMode::Lobby)
	{
		return { TEXT("Ann"), TEXT("Bo"), TEXT("Cy") };
	}
	if (Mode == ECKSessionsShowcaseMode::Roster)
	{
		return { TEXT("Dan"), TEXT("Eve") };
	}
	return {};
}

FVector ACKSessionsShowcaseBoard::GetBotSpot(const int32 Index) const
{
	using namespace CKSessions::Showcase::Board;

	// Left to right for the viewer is from +Y to -Y.
	const int32 Count = GetBotNames().Num();
	const double Y = (Count - 1) * 0.5 * 180.0 - Index * 180.0;
	return GetActorLocation() + FVector(BotX, Y, CenterHeight);
}

APlayerController* ACKSessionsShowcaseBoard::FindBot(const int32 Index) const
{
	return Roster.IsValidIndex(Index) ? Roster[Index].Get() : nullptr;
}

APlayerController* ACKSessionsShowcaseBoard::AddBotNamed(const int32 Index)
{
	const FString Name = GetBotNames()[Index];
	// The host's record of a player is keyed by the name the player logged in with: give the game mode the name first.
	ACKSessionsShowcaseGameMode* GameMode = GetWorld()->GetAuthGameMode<ACKSessionsShowcaseGameMode>();
	if (GameMode != nullptr)
	{
		GameMode->SetNextPlayerName(Name);
	}
	APlayerController* Bot = CreateBot(Name, GetBotSpot(Index), FRotator::ZeroRotator);
	if (GameMode != nullptr)
	{
		GameMode->SetNextPlayerName(FString());
	}
	if (Roster.Num() <= Index)
	{
		Roster.SetNum(Index + 1);
	}
	Roster[Index] = Bot;
	return Bot;
}

void ACKSessionsShowcaseBoard::RunStep_Implementation(const int32 Step)
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}
	if (Mode == ECKSessionsShowcaseMode::Lobby)
	{
		StepLobby(Step);
	}
	else if (Mode == ECKSessionsShowcaseMode::Roster)
	{
		StepRoster(Step);
	}
}

void ACKSessionsShowcaseBoard::SetBotReady(const int32 Index, const bool bReady) const
{
	const APlayerController* Bot = FindBot(Index);
	UCKSessionsPlayerComponent* Player = Bot != nullptr ? UCKSessionsPlayerComponent::FindPlayerComponent(Bot->PlayerState) : nullptr;
	if (Player != nullptr)
	{
		Player->SetReady(bReady);
	}
}

void ACKSessionsShowcaseBoard::ResetGuestsReady() const
{
	// A visitor who pressed ready could keep the count alive after the bots cancel it: the loop clears them.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState == nullptr)
	{
		return;
	}
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		UCKSessionsPlayerComponent* Player = UCKSessionsPlayerComponent::FindPlayerComponent(PlayerState);
		bool bOwnBot = false;
		for (const TWeakObjectPtr<APlayerController>& Bot : Roster)
		{
			bOwnBot |= Bot.IsValid() && Bot->PlayerState == PlayerState;
		}
		if (Player != nullptr && !bOwnBot)
		{
			Player->SetReady(false);
		}
	}
}

void ACKSessionsShowcaseBoard::StepLobby(const int32 Step)
{
	using namespace CKSessions::Showcase::Board;

	if (Roster.IsEmpty())
	{
		for (int32 Index = 0; Index < LobbyBotCount; ++Index)
		{
			AddBotNamed(Index);
		}
	}
	const int32 PlanStep = Step % LobbyStepCount;
	if (PlanStep == 0 || PlanStep == LobbyStepCount - 1)
	{
		ResetGuestsReady();
	}
	for (int32 Index = 0; Index < LobbyBotCount; ++Index)
	{
		SetBotReady(Index, LobbyPlan[PlanStep][Index]);
	}
}

void ACKSessionsShowcaseBoard::StepRoster(const int32 Step)
{
	using namespace CKSessions::Showcase::Board;

	const int32 PlanStep = Step % RosterStepCount;
	if (PlanStep == 0 && Roster.IsEmpty())
	{
		AddBotNamed(0);
		AddBotNamed(1);
	}
	else if (PlanStep == RosterLeaveStep)
	{
		if (APlayerController* Leaving = FindBot(0))
		{
			PlacedBots.Reset();
			UGameplayStatics::RemovePlayer(Leaving, true);
			Roster[0].Reset();
		}
	}
	else if (PlanStep == RosterReturnStep && Roster.Num() > 0 && FindBot(0) == nullptr)
	{
		PlacedBots.Reset();
		AddBotNamed(0);
	}
}
