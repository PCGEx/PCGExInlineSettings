// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPin.h"
#include "Templates/SubclassOf.h"

#include "PCGExProxyInterface.generated.h"

class FPCGExProxyRunner;
class UPCGBlueprintBaseElement;
class UPCGSettings;

namespace PCGExProxy
{
	/** Class meta opting an abstract UPCGSettings class in as a proxy interface: UCLASS(Abstract, meta=(PCGExProxyInterface)). */
	PCGEXINLINESETTINGS_API extern const FName InterfaceMetaKey;

#if WITH_EDITOR
	/** Concrete classes always qualify; abstract classes only when tagged on that exact class (the key is not inherited). */
	PCGEXINLINESETTINGS_API bool IsInterfaceClass(const UClass* InClass);
#endif
}

/**
 * Interface kind of a PCGEx | Proxy node: where the node's pins come from, which settings objects it accepts, which
 * inner pins the extra input pins may feed, and how the inner settings are hosted. Derive in any module.
 */
USTRUCT()
struct PCGEXINLINESETTINGS_API FPCGExProxyInterface
{
	GENERATED_BODY()

	virtual ~FPCGExProxyInterface() = default;

	/** Node pins. InAuthoredSettings is the node's own Settings value, for kinds that fall back to it. Empty = default pins. */
	virtual void GetPins(const UPCGSettings* InAuthoredSettings, TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const PURE_VIRTUAL(FPCGExProxyInterface::GetPins,)

	/** Whether InSettings (never null) can be hosted through this interface. */
	virtual bool Validate(const UPCGSettings* InSettings, FText& OutError) const PURE_VIRTUAL(FPCGExProxyInterface::Validate, return false;)

	/** Inner pins an extra input pin may feed by label: declared inputs plus per-param override pins. */
	virtual void GetForwardablePins(const UPCGSettings* InSettings, TArray<FPCGPinProperties>& OutPins) const;

	/** Hosting strategy for one execution. Default: the inner element runs in a nested context. */
	virtual TSharedRef<FPCGExProxyRunner> CreateRunner() const;

	/** Node subtitle. */
	virtual FString GetTitle(const UPCGSettings* InAuthoredSettings) const;

#if WITH_EDITOR
	/** Double-click target; null falls back to the settings class. */
	virtual UObject* GetJumpTarget(const UPCGSettings* InAuthoredSettings) const { return nullptr; }
#endif
};

template <>
struct TStructOpsTypeTraits<FPCGExProxyInterface> : public TStructOpsTypeTraitsBase2<FPCGExProxyInterface>
{
	enum
	{
		WithPureVirtual = true,
	};
};

/** Pins and validation from a settings class; abstract classes act as templates (their Required inputs become optional). */
USTRUCT(meta = (DisplayName = "Settings Class"))
struct PCGEXINLINESETTINGS_API FPCGExProxyInterfaceSettings : public FPCGExProxyInterface
{
	GENERATED_BODY()

	/** Pins and accepted settings. Abstract classes need the PCGExProxyInterface meta. None: pins follow the node's own Settings. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_NotOverridable, AllowAbstract, GetClassFilter = "ShouldFilterInterfaceClass"))
	TSubclassOf<UPCGSettings> InterfaceClass;

	virtual void GetPins(const UPCGSettings* InAuthoredSettings, TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const override;
	virtual bool Validate(const UPCGSettings* InSettings, FText& OutError) const override;
	virtual FString GetTitle(const UPCGSettings* InAuthoredSettings) const override;
};

/**
 * Pins and validation from a Blueprint element class; the inner settings must be a UPCGBlueprintSettings running that
 * element. Runtime-unverified: hosting a Blueprint element inline has not been tested yet.
 */
USTRUCT(meta = (DisplayName = "Blueprint Element"))
struct PCGEXINLINESETTINGS_API FPCGExProxyInterfaceBlueprint : public FPCGExProxyInterface
{
	GENERATED_BODY()

	/** Element class defining the pins and accepted inner settings. None: pins follow the node's own Settings. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_NotOverridable, AllowAbstract))
	TSubclassOf<UPCGBlueprintBaseElement> ElementClass;

	virtual void GetPins(const UPCGSettings* InAuthoredSettings, TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const override;
	virtual bool Validate(const UPCGSettings* InSettings, FText& OutError) const override;
	virtual FString GetTitle(const UPCGSettings* InAuthoredSettings) const override;
#if WITH_EDITOR
	virtual UObject* GetJumpTarget(const UPCGSettings* InAuthoredSettings) const override;
#endif
};
