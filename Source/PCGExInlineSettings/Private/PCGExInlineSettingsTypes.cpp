// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsTypes.h"

#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#pragma region FPCGExInlineSettings

FPCGExInlineSettings::FPCGExInlineSettings(TSubclassOf<UPCGSettings> InAllowedClass)
{
	AllowedClass = InAllowedClass;
}

void FPCGExInlineSettings::SyncSettings()
{
	FSoftObjectPath DerivedSettings;
	if (!External.IsNull())
	{
		DerivedSettings = External.ToSoftObjectPath();
	}
	else if (Instance)
	{
		// Built from the handle, so an unresolved Instance is never resolved (safe during load).
		DerivedSettings = FSoftObjectPath(Instance);
	}

	// Write only on change: PCG workers may be reading this value.
	if (Settings != DerivedSettings)
	{
		Settings = DerivedSettings;
	}
}

void FPCGExInlineSettings::SetInstance(UPCGSettings* InInstance)
{
	Instance = InInstance;
	SyncSettings();
}

void FPCGExInlineSettings::SetExternal(const TSoftObjectPtr<UPCGSettings>& InExternal)
{
	External = InExternal;
	SyncSettings();
}

UPCGSettings* FPCGExInlineSettings::Resolve() const
{
	return Cast<UPCGSettings>(Settings.ResolveObject());
}

bool FPCGExInlineSettings::IsAllowedClass(const UClass* InClass) const
{
	const UClass* BaseClass = AllowedClass.Get() ? AllowedClass.Get() : UPCGSettings::StaticClass();
	return InClass && InClass->IsChildOf(BaseClass);
}

bool FPCGExInlineSettings::IsAllowed(const UPCGSettings* InSettings) const
{
	return InSettings && IsAllowedClass(InSettings->GetClass());
}

void FPCGExInlineSettings::PostSerialize(const FArchive& Ar)
{
	// Load, duplication (asset duplicate, PIE) and undo/redo all move the instance. PCG's execution copies keep the
	// persistent path: their nested instance dies with the context.
	if (Ar.IsLoading() && !FPCGContext::IsInitializingSettings())
	{
		SyncSettings();
	}
}

bool FPCGExInlineSettings::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	// Default import with the native override off (it would recurse here), then re-derive: pasted text carries the source's path.
	const UScriptStruct* Struct = StaticStruct();
	const TCHAR* Result = Struct->ImportText(Buffer, static_cast<void*>(this), Parent, PortFlags, ErrorText, Struct->GetName(), /*bAllowNativeOverride=*/false);
	if (!Result)
	{
		return false;
	}

	Buffer = Result;
	SyncSettings();
	return true;
}

#pragma endregion

namespace PCGExInlineSettings
{
	EObjectFlags GetInstanceFlags(const UObject* InOuter)
	{
		check(InOuter);

		EObjectFlags Flags = InOuter->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional;
		if (InOuter->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			Flags |= RF_ArchetypeObject;
		}

		return Flags;
	}

	UPCGSettings* CreateInstance(UObject* InOuter, TSubclassOf<UPCGSettings> InClass)
	{
		check(IsInGameThread());
		check(InOuter);

		if (!ensure(InClass && !InClass->HasAnyClassFlags(CLASS_Abstract)))
		{
			return nullptr;
		}

		return NewObject<UPCGSettings>(InOuter, InClass, NAME_None, GetInstanceFlags(InOuter));
	}

	UPCGSettings* DuplicateInstance(const UPCGSettings* InSource, UObject* InOuter)
	{
		check(IsInGameThread());
		check(InOuter);

		if (!InSource)
		{
			return nullptr;
		}

		// Propagated flags come from the new outer, not from wherever the source lived.
		FObjectDuplicationParameters Params = InitStaticDuplicateObjectParams(InSource, InOuter);
		Params.FlagMask &= ~(RF_Public | RF_ArchetypeObject | RF_Transactional | RF_Transient);
		Params.ApplyFlags |= GetInstanceFlags(InOuter);

		return Cast<UPCGSettings>(StaticDuplicateObjectEx(Params));
	}

	// Node plumbing every settings class carries (debug, asset info, GPU, cached override params); Seed is the exception.
	bool IsSettingsPlumbing(const FProperty* InProperty)
	{
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, Seed))
		{
			return false;
		}

		const UClass* OwnerClass = InProperty->GetOwnerClass();
		return OwnerClass == UPCGSettings::StaticClass() || OwnerClass == UPCGSettingsInterface::StaticClass() || OwnerClass == UPCGData::StaticClass();
	}

	// Instance-editable user values only: VisibleAnywhere and EditDefaultsOnly members belong to the class.
	bool IsUserValue(const FProperty* InProperty)
	{
		return InProperty->HasAllPropertyFlags(CPF_Edit)
			&& !InProperty->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance | CPF_Transient | CPF_Deprecated)
			&& !IsSettingsPlumbing(InProperty);
	}

	// Sub-objects owned by the source get a copy owned by the target; external references are shared as-is.
	UObject* ForwardInstancedObject(UObject* InValue, const UObject* InSourceOwner, UObject* InTargetOwner)
	{
		return InValue && InValue->IsIn(InSourceOwner) ? DuplicateObject<UObject>(InValue, InTargetOwner) : InValue;
	}

	/** One matching pass: the value pointers address a single element; InSourceDefault may be null (no delta known, copy). */
	struct FCopyScope
	{
		const UObject* SourceOwner = nullptr;
		UObject* TargetOwner = nullptr;
	};

	void CopyMatchingMembers(const UStruct* InSourceType, const void* InSource, const void* InSourceDefault, const UStruct* InTargetType, void* InTarget, const FCopyScope& InScope);

	// Same-type value, one element. Instanced objects are re-owned; structs holding them are walked member by member.
	void CopySameTypeValue(const FProperty* InSourceProperty, const void* InSource, const void* InSourceDefault, const FProperty* InTargetProperty, void* InTarget, const FCopyScope& InScope)
	{
		// Deep comparison: an untouched default sub-object matches the class default by content, not by pointer.
		if (InSourceDefault && InSourceProperty->Identical(InSource, InSourceDefault, PPF_DeepComparison))
		{
			return;
		}

		if (!InTargetProperty->ContainsInstancedObjectProperty())
		{
			InTargetProperty->CopySingleValue(InTarget, InSource);
			return;
		}

		if (const FObjectProperty* SourceObject = CastField<FObjectProperty>(InSourceProperty))
		{
			UObject* Value = SourceObject->GetObjectPropertyValue(InSource);
			CastFieldChecked<FObjectProperty>(InTargetProperty)->SetObjectPropertyValue(InTarget, ForwardInstancedObject(Value, InScope.SourceOwner, InScope.TargetOwner));
			return;
		}

		if (const FStructProperty* SourceStruct = CastField<FStructProperty>(InSourceProperty))
		{
			CopyMatchingMembers(SourceStruct->Struct, InSource, InSourceDefault, SourceStruct->Struct, InTarget, InScope);

			// A nested inline settings value: its instance is not an editable member, so re-own it and re-derive the path.
			if (SourceStruct->Struct->IsChildOf(FPCGExInlineSettings::StaticStruct()))
			{
				const FPCGExInlineSettings* SourceInline = static_cast<const FPCGExInlineSettings*>(InSource);
				FPCGExInlineSettings* TargetInline = static_cast<FPCGExInlineSettings*>(InTarget);
				const bool bOwned = SourceInline->Instance && SourceInline->Instance->IsIn(InScope.SourceOwner);
				TargetInline->SetInstance(bOwned ? DuplicateInstance(SourceInline->Instance, InScope.TargetOwner) : SourceInline->Instance.Get());
			}
			return;
		}

		const FArrayProperty* SourceArray = CastField<FArrayProperty>(InSourceProperty);
		const FObjectProperty* SourceInner = SourceArray ? CastField<FObjectProperty>(SourceArray->Inner) : nullptr;
		if (!SourceInner)
		{
			// Maps, sets and arrays of structs holding instanced objects keep the target's defaults.
			return;
		}

		const FArrayProperty* TargetArray = CastFieldChecked<FArrayProperty>(InTargetProperty);
		const FObjectProperty* TargetInner = CastFieldChecked<FObjectProperty>(TargetArray->Inner);

		FScriptArrayHelper SourceHelper(SourceArray, InSource);
		FScriptArrayHelper TargetHelper(TargetArray, InTarget);
		TargetHelper.Resize(SourceHelper.Num());
		for (int32 Index = 0; Index < SourceHelper.Num(); ++Index)
		{
			UObject* Value = SourceInner->GetObjectPropertyValue(SourceHelper.GetRawPtr(Index));
			TargetInner->SetObjectPropertyValue(TargetHelper.GetRawPtr(Index), ForwardInstancedObject(Value, InScope.SourceOwner, InScope.TargetOwner));
		}
	}

	void CopyMatchingMembers(const UStruct* InSourceType, const void* InSource, const void* InSourceDefault, const UStruct* InTargetType, void* InTarget, const FCopyScope& InScope)
	{
		for (TFieldIterator<FProperty> It(InTargetType); It; ++It)
		{
			const FProperty* Target = *It;
			if (!IsUserValue(Target))
			{
				continue;
			}

			const FProperty* Source = FindFProperty<FProperty>(InSourceType, Target->GetFName());
			if (!Source || Source->ArrayDim != Target->ArrayDim)
			{
				continue;
			}

			const FStructProperty* SourceStruct = CastField<FStructProperty>(Source);
			const FStructProperty* TargetStruct = CastField<FStructProperty>(Target);
			const bool bSameType = Source->SameType(Target);
			// Structs of different types (a per-class Config deriving from a shared base) are matched member by member.
			const bool bMatchMembers = !bSameType && SourceStruct && TargetStruct;
			if (!bSameType && !bMatchMembers)
			{
				continue;
			}

			for (int32 Index = 0; Index < Target->ArrayDim; ++Index)
			{
				const void* SourceValue = Source->ContainerPtrToValuePtr<void>(InSource, Index);
				const void* SourceDefaultValue = InSourceDefault ? Source->ContainerPtrToValuePtr<void>(InSourceDefault, Index) : nullptr;
				void* TargetValue = Target->ContainerPtrToValuePtr<void>(InTarget, Index);

				if (bMatchMembers)
				{
					CopyMatchingMembers(SourceStruct->Struct, SourceValue, SourceDefaultValue, TargetStruct->Struct, TargetValue, InScope);
				}
				else
				{
					CopySameTypeValue(Source, SourceValue, SourceDefaultValue, Target, TargetValue, InScope);
				}
			}
		}
	}

	void CopyMatchingValues(const UObject* InSource, UObject* InTarget)
	{
		check(IsInGameThread());

		if (!InSource || !InTarget || InSource == InTarget)
		{
			return;
		}

		// Untouched values (identical to the source class defaults) leave the target class's own defaults in place.
		const FCopyScope Scope{InSource, InTarget};
		CopyMatchingMembers(InSource->GetClass(), InSource, InSource->GetClass()->GetDefaultObject(), InTarget->GetClass(), InTarget, Scope);

		// Blueprint elements keep their user values on the element object, which SetBlueprintElementType creates.
		const UPCGBlueprintSettings* SourceBlueprint = Cast<const UPCGBlueprintSettings>(InSource);
		UPCGBlueprintSettings* TargetBlueprint = Cast<UPCGBlueprintSettings>(InTarget);
		if (SourceBlueprint && TargetBlueprint)
		{
			CopyMatchingValues(SourceBlueprint->GetElementObject(), TargetBlueprint->GetElementObject());
		}
	}
}
