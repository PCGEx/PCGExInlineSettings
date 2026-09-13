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

	/** Pin labels the proxy tests forward onto. */
	static const FName AnyPinLabel;
	static const FName PointPinLabel;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	FPCGExInlineSettings Inline;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Abstract proxy interface for the tests: one Required input and one typed output, tagged like PCGEx provider bases. */
UCLASS(Abstract, Hidden, Transient, NotBlueprintable, meta = (PCGExProxyInterface))
class UPCGExProxyTestInterfaceSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	static const FName SourcePinLabel;
	static const FName ResultPinLabel;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};

/** Abstract class without the keyword: must never qualify as a proxy interface. */
UCLASS(Abstract, Hidden, Transient, NotBlueprintable)
class UPCGExProxyTestUntaggedSettings : public UPCGSettings
{
	GENERATED_BODY()
};
