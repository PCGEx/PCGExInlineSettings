// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExProxyInterface.h"

#include "PCGExProxy.generated.h"

class FPCGExProxyRunner;

/** How the proxy waits when the inner element pauses itself without dynamic dependencies (async work, GPU readback, Wait). */
UENUM()
enum class EPCGExProxyPauseWake : uint8
{
	/** Pause the proxy and check the inner again on the next game-thread tick. Frees the executor slot; up to one frame of latency per check. */
	Poll = 0 UMETA(DisplayName = "Poll Next Tick"),
	/** Stay active: the executor calls the proxy again every loop. No added latency; keeps a worker slot busy while waiting. */
	Redispatch = 1 UMETA(DisplayName = "Re-dispatch"),
};

/**
 * PCGEx | Proxy: runs another settings object, which can be overridden, through a chosen interface kind. Unlike the
 * stock Proxy, the interface may be an abstract settings class tagged with meta=(PCGExProxyInterface), user-declared
 * extra input pins feed matching inner pins, and inner pausing, abort, main-thread needs and GC are bridged.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGEXINLINESETTINGS_API UPCGExProxySettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGExProxySettings();

	//~ Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGExProxy")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGExProxy", "NodeTitle", "PCGEx | Proxy"); }
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }
	virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Picker filter for the interface class (true hides): concrete classes, or abstract classes tagged PCGExProxyInterface. */
	UFUNCTION()
	static bool ShouldFilterInterfaceClass(const UClass* InClass);

	/** Lets extra pins take type compositions in the Allowed Types picker. */
	UFUNCTION()
	static bool SupportsComposition() { return true; }
#endif
	virtual FString GetAdditionalTitleInformation() const override;
	virtual bool HasFlippedTitleLines() const override { return true; }
	// The interface class is a template; an unwired optional pin says nothing about the concrete inner.
	virtual bool CanCullTaskIfUnwired() const override { return false; }
	// Read at graph compile time from the node: the inner is chosen at run time, so always ask for user-parameter data.
	virtual bool RequiresDataFromPreTask() const override { return true; }

protected:
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~ End UPCGSettings interface

public:
	/** Settings run by the proxy. Overridable: feed it the Settings path of an inline settings graph parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	TObjectPtr<UPCGSettings> Settings;

	/** Where the node's pins come from and which settings it accepts. */
	UPROPERTY(EditAnywhere, NoClear, Category = "Settings", meta = (PCG_NotOverridable, ExcludeBaseStruct))
	TInstancedStruct<FPCGExProxyInterface> Interface;

	/** Extra input pins, forwarded to the inner settings' pin of the same label when the data type fits. Labels used by the interface or the proxy are ignored. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_NotOverridable, TitleProperty = "{Label}"))
	TArray<FPCGPinProperties> ExtraInputPins;

	/** Tag output data with the inner pin label it came from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bTagOutputsBasedOnOutputPins = true;

	/** How the proxy waits on an inner element that pauses itself. */
	UPROPERTY(EditAnywhere, Category = "Settings", AdvancedDisplay, meta = (PCG_NotOverridable))
	EPCGExProxyPauseWake PauseWake = EPCGExProxyPauseWake::Poll;

	const FPCGExProxyInterface* GetInterface() const { return Interface.GetPtr(); }

	/** Interface pins, from the kind or the node's own Settings. Both empty when neither is set. */
	void GetInterfacePins(TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const;

	/** ExtraInputPins as exposed on the node: no empty or duplicate labels, none used by InInterfaceInputs or the proxy itself, plain usage. */
	TArray<FPCGPinProperties> GetExtraInputPins(const TArray<FPCGPinProperties>& InInterfaceInputs) const;
};

struct PCGEXINLINESETTINGS_API FPCGExProxyContext : public FPCGContext
{
	FPCGExProxyContext();
	virtual ~FPCGExProxyContext() override;

	FPCGExProxyContext(const FPCGExProxyContext&) = delete;
	FPCGExProxyContext& operator=(const FPCGExProxyContext&) = delete;

	/** Lets the runner attach the graph executor to its inner context (protected on FPCGContext). */
	void SetupInnerGraphExecutor(FPCGContext* InInnerContext) { InitializeGraphExecutor(InInnerContext); }

	/** Settings to run, after overrides; null when missing or rejected (error already reported). */
	TObjectPtr<UPCGSettings> InnerSettings;
	TSharedPtr<FPCGExProxyRunner> Runner;
	bool bInnerDisabled = false;

	/** Created up front: GetOrCreateHandle is not thread safe and wake-ups capture it from worker threads. */
	TWeakPtr<FPCGContextHandle> WeakHandle;

protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;
};

class PCGEXINLINESETTINGS_API FPCGExProxyElement : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
	virtual void AbortInternal(FPCGContext* InContext) const override;
	// The inner converts its own inputs; converting here would turn typed data (PCGEx factories, clusters) into plain points.
	virtual bool SupportsGPUResidentData(FPCGContext* InContext) const override { return true; }
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};
