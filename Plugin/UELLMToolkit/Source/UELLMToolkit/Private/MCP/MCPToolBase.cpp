// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPToolBase.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

TOptional<FMCPToolResult> FMCPToolBase::ValidateEditorContext(UWorld*& OutWorld) const
{
	OutWorld = nullptr;

	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor not available"));
	}

	OutWorld = GEditor->GetEditorWorldContext().World();
	if (!OutWorld)
	{
		return FMCPToolResult::Error(TEXT("No active world"));
	}

	return TOptional<FMCPToolResult>(); // Success - no error
}

AActor* FMCPToolBase::FindActorByNameOrLabel(UWorld* World, const FString& NameOrLabel) const
{
	if (!World || NameOrLabel.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == NameOrLabel || Actor->GetActorLabel() == NameOrLabel))
		{
			return Actor;
		}
	}

	return nullptr;
}

void FMCPToolBase::MarkWorldDirty(UWorld* World) const
{
	if (World)
	{
		World->MarkPackageDirty();
	}
}

void FMCPToolBase::MarkActorDirty(AActor* Actor) const
{
	if (Actor)
	{
		Actor->MarkPackageDirty();
		if (UWorld* World = Actor->GetWorld())
		{
			World->MarkPackageDirty();
		}
	}
}

bool FMCPToolBase::SetSinglePropertyFromJson(
	FProperty* Prop,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Context,
	FString& OutError)
{
	if (!Prop || !ValuePtr || !Value.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid property, value pointer, or JSON value for '%s'"), *Context);
		return false;
	}

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		if (NumProp->IsFloatingPoint())
		{
			double DoubleVal = 0.0;
			if (Value->TryGetNumber(DoubleVal))
			{
				NumProp->SetFloatingPointPropertyValue(ValuePtr, DoubleVal);
				return true;
			}
		}
		else if (NumProp->IsInteger())
		{
			int64 IntVal = 0;
			if (Value->TryGetNumber(IntVal))
			{
				NumProp->SetIntPropertyValue(ValuePtr, IntVal);
				return true;
			}
			FString StrVal;
			if (Value->TryGetString(StrVal))
			{
				NumProp->SetIntPropertyValue(ValuePtr, FCString::Atoi64(*StrVal));
				return true;
			}
		}
		OutError = FString::Printf(TEXT("Failed to set numeric value for '%s'"), *Context);
		return false;
	}

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		bool BoolVal = false;
		if (Value->TryGetBool(BoolVal))
		{
			BoolProp->SetPropertyValue(ValuePtr, BoolVal);
			return true;
		}
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			BoolProp->SetPropertyValue(ValuePtr, StrVal.Equals(TEXT("true"), ESearchCase::IgnoreCase));
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set bool value for '%s'"), *Context);
		return false;
	}

	if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			StrProp->SetPropertyValue(ValuePtr, StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set string value for '%s'"), *Context);
		return false;
	}

	if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*StrVal));
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set name value for '%s'"), *Context);
		return false;
	}

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(StrVal);
			if (EnumVal != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
				return true;
			}
			OutError = FString::Printf(TEXT("Enum value '%s' not found for '%s'"), *StrVal, *Context);
			return false;
		}
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)NumVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set enum value for '%s'"), *Context);
		return false;
	}

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			FString StrVal;
			if (Value->TryGetString(StrVal))
			{
				int64 EnumVal = ByteProp->Enum->GetValueByNameString(StrVal);
				if (EnumVal != INDEX_NONE)
				{
					ByteProp->SetIntPropertyValue(ValuePtr, EnumVal);
					return true;
				}
				OutError = FString::Printf(TEXT("Enum value '%s' not found for '%s'"), *StrVal, *Context);
				return false;
			}
		}
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			ByteProp->SetIntPropertyValue(ValuePtr, (int64)NumVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set byte value for '%s'"), *Context);
		return false;
	}

	// FClassProperty before FObjectPropertyBase (FClassProperty IS-A FObjectPropertyBase)
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UClass* LoadedClass = LoadClass<UObject>(nullptr, *StrVal);
			if (LoadedClass)
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, LoadedClass);
				return true;
			}
			OutError = FString::Printf(TEXT("Failed to load class '%s' for '%s'"), *StrVal, *Context);
			return false;
		}
		OutError = FString::Printf(TEXT("Class property '%s' requires a string path"), *Context);
		return false;
	}

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *StrVal);
			if (LoadedObj)
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
				return true;
			}
			if (ObjProp->PropertyClass->IsChildOf(AActor::StaticClass()))
			{
				UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (World)
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						if ((*It)->GetName() == StrVal || (*It)->GetActorLabel() == StrVal)
						{
							ObjProp->SetObjectPropertyValue(ValuePtr, *It);
							return true;
						}
					}
				}
			}
			OutError = FString::Printf(TEXT("Failed to load object or find actor '%s' for '%s'"), *StrVal, *Context);
			return false;
		}
		OutError = FString::Printf(TEXT("Object property '%s' requires a string path"), *Context);
		return false;
	}

	// FSoftClassProperty before FSoftObjectProperty
	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Soft class property '%s' requires a string path"), *Context);
		return false;
	}

	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Soft object property '%s' requires a string path"), *Context);
		return false;
	}

	OutError = FString::Printf(TEXT("Unsupported property type '%s' for '%s'"), *Prop->GetCPPType(), *Context);
	return false;
}

bool FMCPToolBase::SetArrayPropertyFromJson(
	FArrayProperty* ArrayProp,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& JsonValue,
	const FString& PropertyPath,
	FString& OutError)
{
	if (!ArrayProp || !ValuePtr || !JsonValue.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid array property or value for '%s'"), *PropertyPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!JsonValue->TryGetArray(JsonArray))
	{
		OutError = FString::Printf(TEXT("Array property '%s' requires a JSON array"), *PropertyPath);
		return false;
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
	ArrayHelper.EmptyValues();
	ArrayHelper.Resize(JsonArray->Num());

	FProperty* InnerProp = ArrayProp->Inner;
	for (int32 i = 0; i < JsonArray->Num(); ++i)
	{
		FString Context = FString::Printf(TEXT("%s[%d]"), *PropertyPath, i);
		if (!SetSinglePropertyFromJson(InnerProp, ArrayHelper.GetRawPtr(i), (*JsonArray)[i], Context, OutError))
		{
			return false;
		}
	}
	return true;
}

bool FMCPToolBase::SetMapPropertyFromJson(
	FMapProperty* MapProp,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& JsonValue,
	const FString& PropertyPath,
	FString& OutError)
{
	if (!MapProp || !ValuePtr || !JsonValue.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid map property or value for '%s'"), *PropertyPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* ObjVal;
	if (!JsonValue->TryGetObject(ObjVal) || !ObjVal || !(*ObjVal).IsValid())
	{
		OutError = FString::Printf(TEXT("Map property '%s' requires a JSON object (key-value pairs)"), *PropertyPath);
		return false;
	}

	FProperty* KeyProp = MapProp->KeyProp;
	FProperty* ValueProp = MapProp->ValueProp;

	FScriptMapHelper MapHelper(MapProp, ValuePtr);
	MapHelper.EmptyValues();

	for (const auto& Pair : (*ObjVal)->Values)
	{
		int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();

		uint8* PairKeyPtr = MapHelper.GetKeyPtr(Index);
		uint8* PairValuePtr = MapHelper.GetValuePtr(Index);

		TSharedPtr<FJsonValue> KeyJsonValue = MakeShared<FJsonValueString>(Pair.Key);
		FString KeyContext = FString::Printf(TEXT("%s[key='%s']"), *PropertyPath, *Pair.Key);
		if (!SetSinglePropertyFromJson(KeyProp, PairKeyPtr, KeyJsonValue, KeyContext, OutError))
		{
			return false;
		}

		FString ValueContext = FString::Printf(TEXT("%s[value for key='%s']"), *PropertyPath, *Pair.Key);
		if (!SetSinglePropertyFromJson(ValueProp, PairValuePtr, Pair.Value, ValueContext, OutError))
		{
			return false;
		}
	}

	MapHelper.Rehash();
	return true;
}
