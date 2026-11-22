// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDemoHUD.h"
#include "Blueprint/UserWidget.h"
#include "CKSessionsDemoPanelWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CKSessionsDemoHUD)

namespace CKSessions::Demo
{
	namespace
	{
		/** Above the game's own widgets, below the engine's debug overlays. */
		constexpr int32 PanelZOrder = 10;
	}
}

ACKSessionsDemoHUD::ACKSessionsDemoHUD()
{
	// Cook: the HUD only names the demo lobby for travel; a project that packages the demo cooks its maps itself (Maps To Cook).
	LobbyMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/CoopKitSessions/Maps/L_SessionsDemo.L_SessionsDemo")));
}

FCKSessionsDemoRestartPlay& ACKSessionsDemoHUD::OnRestartPlayInEditor()
{
	static FCKSessionsDemoRestartPlay Delegate;
	return Delegate;
}

void ACKSessionsDemoHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* Controller = GetOwningPlayerController();
	if (Controller == nullptr || !Controller->IsLocalController())
	{
		return;
	}

	UCKSessionsDemoPanelWidget* DemoPanel = CreateWidget<UCKSessionsDemoPanelWidget>(Controller, UCKSessionsDemoPanelWidget::StaticClass());
	if (DemoPanel == nullptr)
	{
		return;
	}
	DemoPanel->SetDemoHUD(this);
	DemoPanel->AddToPlayerScreen(CKSessions::Demo::PanelZOrder);
	Panel = DemoPanel;

	// The mouse stays free over the game: the panel takes clicks, the rest of the screen still reaches the game.
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->SetInputMode(InputMode);
	Controller->SetShowMouseCursor(true);
}

void ACKSessionsDemoHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Panel != nullptr)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
