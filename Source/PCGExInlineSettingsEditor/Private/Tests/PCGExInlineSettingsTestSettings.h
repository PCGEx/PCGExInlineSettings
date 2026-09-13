// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGExInlineSettingsTypes.h"

#include "PCGExInlineSettingsTestSettings.generated.h"

/** Owner used by the plugin's automation tests. Hidden from the node palette and the inline settings picker. */
UCLASS(Hidden, Transient, NotBlueprintable)
class UPCGExInlineSettingsTestSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGExInlineSettingsTestSettings();

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	FPCGExInlineSettings Inline;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};
