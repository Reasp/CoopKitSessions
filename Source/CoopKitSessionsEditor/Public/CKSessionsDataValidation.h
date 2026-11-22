// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"

namespace CKSessions::Editor
{
	/** Outcome of validating one data asset: what the Validation tab of the data editor shows. */
	struct FValidationReport
	{
		EDataValidationResult Result = EDataValidationResult::NotValidated;
		TArray<FText> Errors;
		TArray<FText> Warnings;

		/** Text for the Validation tab: "Valid", or one line per error and warning. */
		COOPKITSESSIONSEDITOR_API FText ToText() const;
	};

	/** Runs the asset's IsDataValid and collects its errors and warnings. */
	COOPKITSESSIONSEDITOR_API FValidationReport ValidateData(const UObject& Data);
}
