// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "PCGExInlineSettingsEditorSettings.generated.h"

/** Which properties of an inline settings instance the details panel shows. Per-value force lists on the struct win over these. */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "PCGEx | Inline Settings"))
class PCGEXINLINESETTINGSEDITOR_API UPCGExInlineSettingsEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPCGExInlineSettingsEditorSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("PCGEx | Inline Settings"); }
	//~ End UDeveloperSettings interface

	/** Hide the properties every PCG node has (debug, asset info, GPU, determinism...), i.e. those declared on UPCGSettings and its bases. */
	UPROPERTY(EditAnywhere, Config, Category = "Inline Instance")
	bool bHideBaseProperties = true;

	/** Base-class properties shown even when the above is on. */
	UPROPERTY(EditAnywhere, Config, Category = "Inline Instance", meta = (EditCondition = "bHideBaseProperties"))
	TArray<FName> ShownBaseProperties;

	/** Property names hidden on every inline instance. */
	UPROPERTY(EditAnywhere, Config, Category = "Inline Instance")
	TArray<FName> HiddenProperties;

	/** Top-level category names hidden on every inline instance. */
	UPROPERTY(EditAnywhere, Config, Category = "Inline Instance")
	TArray<FName> HiddenCategories;
};
