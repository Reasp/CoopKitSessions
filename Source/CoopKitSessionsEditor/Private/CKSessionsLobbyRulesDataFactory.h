// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CKSessionsLobbyRulesDataFactory.generated.h"

/** Creates lobby rules from Content Browser > Add > CoopKit. */
UCLASS(HideCategories = Object)
class UCKSessionsLobbyRulesDataFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UCKSessionsLobbyRulesDataFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
