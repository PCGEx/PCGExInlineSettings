// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "PCGExInlineSettingsTypes.h"

#include "PCGExInlineSettingsLibrary.generated.h"

class UPCGGraphInterface;
class UPCGSettings;

/**
 * Blueprint access to inline settings. FPCGExInlineSettings has no Blueprint-visible members and PCG's graph parameter
 * helpers cover no struct types, so these are the only way to reach the settings object a value holds.
 */
UCLASS()
class PCGEXINLINESETTINGS_API UPCGExInlineSettingsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Settings object held by Value, cast to SettingsClass. Outputs the inline instance, or the referenced asset when
	 * the value is external and that asset is already loaded.
	 */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Inline Settings", meta = (DeterminesOutputType = "SettingsClass", DynamicOutputParam = "Settings"))
	static void GetInlineSettings(
		const FPCGExInlineSettings& Value, TSubclassOf<UPCGSettings> SettingsClass,
		UPCGSettings*& Settings, bool& bIsValid, bool& bIsExternal);

	/** Same, for the inline settings graph parameter named ParameterName on GraphInterface. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Inline Settings", meta = (DeterminesOutputType = "SettingsClass", DynamicOutputParam = "Settings"))
	static void GetInlineSettingsParameter(
		const UPCGGraphInterface* GraphInterface, FName ParameterName, TSubclassOf<UPCGSettings> SettingsClass,
		UPCGSettings*& Settings, bool& bIsValid, bool& bIsExternal);

	/**
	 * Call after writing to a settings object from Blueprint: those writes bypass PostEditChangeProperty, which is what
	 * refreshes UPCGSettings' cached Crc, so the graph would otherwise re-run against results cached before the write.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Inline Settings")
	static void NotifySettingsChanged(UPCGSettings* Settings);
};
