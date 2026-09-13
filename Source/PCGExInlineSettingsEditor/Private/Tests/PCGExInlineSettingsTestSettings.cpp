// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tests/PCGExInlineSettingsTestSettings.h"

#include "PCGCommon.h"
#include "PCGPin.h"

#pragma region UPCGExInlineSettingsTestSettings

const FName UPCGExInlineSettingsTestSettings::AnyPinLabel(TEXT("In"));
const FName UPCGExInlineSettingsTestSettings::PointPinLabel(TEXT("Points"));

UPCGExInlineSettingsTestSettings::UPCGExInlineSettingsTestSettings()
{
#if WITH_EDITORONLY_DATA
	bExposeToLibrary = false;
#endif
}

TArray<FPCGPinProperties> UPCGExInlineSettingsTestSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(AnyPinLabel, EPCGDataType::Any);
	Pins.Emplace(PointPinLabel, EPCGDataType::Point, /*bInAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	return Pins;
}

FPCGElementPtr UPCGExInlineSettingsTestSettings::CreateElement() const
{
	// Never executed: the tests only inspect overridable params, pins and serialization.
	return nullptr;
}

#pragma endregion

#pragma region UPCGExProxyTestInterfaceSettings

const FName UPCGExProxyTestInterfaceSettings::SourcePinLabel(TEXT("Source"));
const FName UPCGExProxyTestInterfaceSettings::ResultPinLabel(TEXT("Result"));

TArray<FPCGPinProperties> UPCGExProxyTestInterfaceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(SourcePinLabel, EPCGDataType::Point).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGExProxyTestInterfaceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(ResultPinLabel, EPCGDataType::Param);
	return Pins;
}

#pragma endregion
