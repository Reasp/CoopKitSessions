// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsDiagnostics.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

/**
 * The setup window of CoopKit Sessions (Tools > CoopKit Sessions Setup): checks the Steam and EOS configuration of the open
 * project and lists what is wrong with a fix for each finding. Three buttons apply the recommended setup of Null, Steam or EOS
 * for this engine version to DefaultEngine.ini (and enable the engine plugins), after a confirmation. Everything works with
 * the mouse.
 *
 * Widget tags (SWidget::GetTag) for tests and screenshots: CoopKitSessionsSetup.Check, CoopKitSessionsSetup.Apply.Null,
 * CoopKitSessionsSetup.Apply.Steam, CoopKitSessionsSetup.Apply.Eos.
 */
class COOPKITSESSIONSEDITOR_API SCKSessionsSetup final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCKSessionsSetup)
		: _bChangePlugins(true)
	{
	}
		/** Ini file the Apply buttons write; empty is the project's Config/DefaultEngine.ini. Tests point it at a copy. */
		SLATE_ARGUMENT(FString, IniPath)
		/** False leaves the project file (.uproject) alone when applying; tests set it. */
		SLATE_ARGUMENT(bool, bChangePlugins)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gathers the project's snapshot again and lists the findings. */
	void Check();

	/**
	 * What an Apply button does after its confirmation: writes the recommended setup into the ini file, enables the plugins,
	 * and says what changed and that the editor needs a restart. Returns false (with the reason in the report) on failure.
	 */
	bool ApplySetup(CKSessions::Diagnostics::EService Service);

	/** Findings of the last check. */
	const TArray<FCKSessionsFinding>& GetFindings() const
	{
		return Findings;
	}

	/** The line below the buttons: what the last apply did, or empty. */
	const FText& GetReport() const
	{
		return Report;
	}

private:
	FReply OnApplyClicked(CKSessions::Diagnostics::EService Service);
	TSharedRef<SWidget> MakeApplyButton(CKSessions::Diagnostics::EService Service, const FText& Label, const FName& ButtonTag);

	TArray<FCKSessionsFinding> Findings;
	FText Report;
	FString IniPath;
	bool bChangePlugins = true;
	TSharedPtr<SVerticalBox> FindingsBox;
};
