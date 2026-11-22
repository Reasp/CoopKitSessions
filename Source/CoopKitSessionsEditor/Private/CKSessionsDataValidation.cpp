// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDataValidation.h"

#define LOCTEXT_NAMESPACE "CKSessionsDataValidation"

namespace CKSessions::Editor
{
	FText FValidationReport::ToText() const
	{
		if (Errors.IsEmpty() && Warnings.IsEmpty())
		{
			return Result == EDataValidationResult::Invalid
				? LOCTEXT("InvalidWithoutMessages", "Invalid (no details reported).")
				: LOCTEXT("Valid", "Valid: no errors or warnings.");
		}

		TArray<FText> Lines;
		for (const FText& Error : Errors)
		{
			Lines.Add(FText::Format(LOCTEXT("ErrorLine", "Error: {0}"), Error));
		}
		for (const FText& Warning : Warnings)
		{
			Lines.Add(FText::Format(LOCTEXT("WarningLine", "Warning: {0}"), Warning));
		}
		return FText::Join(FText::FromString(TEXT("\n")), Lines);
	}

	FValidationReport ValidateData(const UObject& Data)
	{
		FValidationReport Report;
		FDataValidationContext Context;
		Report.Result = Data.IsDataValid(Context);
		Context.SplitIssues(Report.Warnings, Report.Errors);
		if (!Report.Errors.IsEmpty())
		{
			// An asset may report errors without returning Invalid; the editor treats any error as invalid data.
			Report.Result = EDataValidationResult::Invalid;
		}
		return Report;
	}
}

#undef LOCTEXT_NAMESPACE
