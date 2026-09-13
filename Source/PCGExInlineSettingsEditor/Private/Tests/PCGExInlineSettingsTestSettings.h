// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGExInlineSettingsTypes.h"

#include "PCGExInlineSettingsTestSettings.generated.h"

/** Shared base of the per-class test configs, like a PCGEx provider's Config base. */
USTRUCT()
struct FPCGExInlineSettingsTestConfigBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 Strength = 1;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float Scale = 1.0f;
};

USTRUCT()
struct FPCGExInlineSettingsTestConfigA : public FPCGExInlineSettingsTestConfigBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 OnlyA = 0;
};

USTRUCT()
struct FPCGExInlineSettingsTestConfigB : public FPCGExInlineSettingsTestConfigBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	FName OnlyB = TEXT("Default");
};

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

	/** Shared with the sibling class, same type: carried over on a class change. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 Shared = 1;

	/** Shared name with the sibling class, different type: never carried over. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 Mismatched = 0;

	/** Instanced sub-object shared with the sibling class: duplicated under the new instance. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Settings")
	TObjectPtr<UPCGSettings> Nested;

	/** Per-class config; the sibling's is another type with the same base, matched member by member. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FPCGExInlineSettingsTestConfigA Config;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Sibling of the test owner sharing some property names, for the class-change carry-over test. */
UCLASS(Hidden, Transient, NotBlueprintable)
class UPCGExInlineSettingsTestSiblingSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 Shared = 1;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float Mismatched = 0.0f;

	UPROPERTY(EditAnywhere, Instanced, Category = "Settings")
	TObjectPtr<UPCGSettings> Nested;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FName OnlyHere = TEXT("Default");

	UPROPERTY(EditAnywhere, Category = "Settings")
	FPCGExInlineSettingsTestConfigB Config;

protected:
	virtual FPCGElementPtr CreateElement() const override { return nullptr; }
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
