// Copyright Natali Caggiano. All Rights Reserved.

#include "PropertySerializer.h"
#include "UnrealClaudeUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "UObject/EnumProperty.h"

TSharedPtr<FJsonValue> FPropertySerializer::PropertyToJsonValue(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			uint8 ByteVal = ByteProp->GetPropertyValue(ValuePtr);
			FString EnumName = ByteProp->Enum->GetNameStringByValue(ByteVal);
			return MakeShared<FJsonValueString>(EnumName);
		}
	}

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			return MakeShared<FJsonValueNumber>(NumProp->GetFloatingPointPropertyValue(ValuePtr));
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(NumProp->GetSignedIntPropertyValue(ValuePtr)));
	}

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}

	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}

	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		int64 IntVal = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
		FString EnumName = EnumProp->GetEnum()->GetNameStringByValue(IntVal);
		return MakeShared<FJsonValueString>(EnumName);
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return StructToJsonValue(StructProp, ValuePtr);
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		for (int32 i = 0; i < ArrayHelper.Num(); ++i)
		{
			JsonArray.Add(PropertyToJsonValue(ArrayProp->Inner, ArrayHelper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}

	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		TSharedPtr<FJsonObject> MapObj = MakeShared<FJsonObject>();
		FScriptMapHelper MapHelper(MapProp, ValuePtr);
		for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i)
		{
			if (!MapHelper.IsValidIndex(i))
			{
				continue;
			}

			FString KeyStr;
			MapProp->KeyProp->ExportTextItem_Direct(KeyStr, MapHelper.GetKeyPtr(i), nullptr, nullptr, PPF_None);
			TSharedPtr<FJsonValue> ValJson = PropertyToJsonValue(MapProp->ValueProp, MapHelper.GetValuePtr(i));
			MapObj->SetField(KeyStr, ValJson);
		}
		return MakeShared<FJsonValueObject>(MapObj);
	}

	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		UObject* Obj = ClassProp->GetObjectPropertyValue(ValuePtr);
		if (Obj)
		{
			return MakeShared<FJsonValueString>(Obj->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}

	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
	{
		FSoftObjectPtr SoftPtr = SoftClassProp->GetPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPtr.ToString());
	}

	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPtr.ToString());
	}

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (Obj)
		{
			return MakeShared<FJsonValueString>(Obj->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}

	FString ExportedText;
	Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(ExportedText);
}

TSharedPtr<FJsonValue> FPropertySerializer::StructToJsonValue(FStructProperty* StructProp, const void* ValuePtr)
{
	UScriptStruct* Struct = StructProp->Struct;

	if (Struct == TBaseStructure<FVector>::Get() || Struct->GetFName() == FName("Vector"))
	{
		const FVector& Vec = *reinterpret_cast<const FVector*>(ValuePtr);
		return MakeShared<FJsonValueObject>(UnrealClaudeJsonUtils::VectorToJson(Vec));
	}

	if (Struct == TBaseStructure<FRotator>::Get() || Struct->GetFName() == FName("Rotator"))
	{
		const FRotator& Rot = *reinterpret_cast<const FRotator*>(ValuePtr);
		return MakeShared<FJsonValueObject>(UnrealClaudeJsonUtils::RotatorToJson(Rot));
	}

	if (Struct == TBaseStructure<FColor>::Get() || Struct->GetFName() == FName("Color"))
	{
		const FColor& Color = *reinterpret_cast<const FColor*>(ValuePtr);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("r"), Color.R);
		Obj->SetNumberField(TEXT("g"), Color.G);
		Obj->SetNumberField(TEXT("b"), Color.B);
		Obj->SetNumberField(TEXT("a"), Color.A);
		return MakeShared<FJsonValueObject>(Obj);
	}

	if (Struct == TBaseStructure<FLinearColor>::Get() || Struct->GetFName() == FName("LinearColor"))
	{
		const FLinearColor& Color = *reinterpret_cast<const FLinearColor*>(ValuePtr);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("r"), Color.R);
		Obj->SetNumberField(TEXT("g"), Color.G);
		Obj->SetNumberField(TEXT("b"), Color.B);
		Obj->SetNumberField(TEXT("a"), Color.A);
		return MakeShared<FJsonValueObject>(Obj);
	}

	if (Struct == TBaseStructure<FTransform>::Get() || Struct->GetFName() == FName("Transform"))
	{
		const FTransform& Transform = *reinterpret_cast<const FTransform*>(ValuePtr);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(Transform.GetLocation()));
		Obj->SetObjectField(TEXT("rotation"), UnrealClaudeJsonUtils::RotatorToJson(Transform.Rotator()));
		Obj->SetObjectField(TEXT("scale"), UnrealClaudeJsonUtils::VectorToJson(Transform.GetScale3D()));
		return MakeShared<FJsonValueObject>(Obj);
	}

	if (Struct == TBaseStructure<FVector2D>::Get() || Struct->GetFName() == FName("Vector2D"))
	{
		const FVector2D& Vec = *reinterpret_cast<const FVector2D*>(ValuePtr);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Vec.X);
		Obj->SetNumberField(TEXT("y"), Vec.Y);
		return MakeShared<FJsonValueObject>(Obj);
	}

	FString ExportedText;
	StructProp->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(ExportedText);
}

FPropertySerializer::FPropertyPathResult FPropertySerializer::GetPropertyByPath(UObject* Object, const FString& PropertyPath)
{
	FPropertyPathResult Result;

	if (!Object)
	{
		Result.Error = TEXT("Object is null");
		return Result;
	}

	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."), true);

	if (PathParts.Num() == 0)
	{
		Result.Error = TEXT("Empty property path");
		return Result;
	}

	UStruct* CurrentStruct = Object->GetClass();
	void* CurrentContainer = Object;

	for (int32 i = 0; i < PathParts.Num() - 1; ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			Result.Error = FString::Printf(TEXT("Property '%s' not found on %s"), *PathParts[i], *CurrentStruct->GetName());
			return Result;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
			continue;
		}

		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* NestedObj = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CurrentContainer));
			if (!NestedObj)
			{
				Result.Error = FString::Printf(TEXT("Nested object '%s' is null"), *PathParts[i]);
				return Result;
			}
			CurrentContainer = NestedObj;
			CurrentStruct = NestedObj->GetClass();
			continue;
		}

		Result.Error = FString::Printf(TEXT("Cannot navigate through '%s' (type: %s) — not a struct or object property"), *PathParts[i], *Prop->GetCPPType());
		return Result;
	}

	const FString& LeafName = PathParts.Last();
	FProperty* LeafProp = CurrentStruct->FindPropertyByName(FName(*LeafName));
	if (!LeafProp)
	{
		Result.Error = FString::Printf(TEXT("Property '%s' not found on %s"), *LeafName, *CurrentStruct->GetName());
		return Result;
	}

	const void* LeafValuePtr = LeafProp->ContainerPtrToValuePtr<void>(CurrentContainer);
	Result.Value = PropertyToJsonValue(LeafProp, LeafValuePtr);
	Result.bSuccess = true;
	return Result;
}

bool FPropertySerializer::ShouldSkipProperty(FProperty* Property)
{
	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
	{
		return true;
	}

	if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property))
	{
		return true;
	}

	if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly))
	{
		return true;
	}

	return false;
}

TSharedPtr<FJsonObject> FPropertySerializer::GetCDOOverrides(UObject* CDO, UObject* ParentCDO, bool bEditableOnly)
{
	TSharedPtr<FJsonObject> Overrides = MakeShared<FJsonObject>();

	if (!CDO)
	{
		return Overrides;
	}

	UClass* BPClass = CDO->GetClass();

	for (TFieldIterator<FProperty> It(BPClass); It; ++It)
	{
		FProperty* Property = *It;

		if (bEditableOnly && ShouldSkipProperty(Property))
		{
			continue;
		}

		const void* CDOValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

		bool bIsBPAdded = (Property->GetOwnerClass() == BPClass);

		if (!bIsBPAdded && ParentCDO)
		{
			FProperty* ParentProp = ParentCDO->GetClass()->FindPropertyByName(Property->GetFName());
			if (ParentProp)
			{
				const void* ParentValuePtr = ParentProp->ContainerPtrToValuePtr<void>(ParentCDO);
				if (Property->Identical(CDOValuePtr, ParentValuePtr))
				{
					continue;
				}
			}
		}

		TSharedPtr<FJsonValue> JsonVal = PropertyToJsonValue(Property, CDOValuePtr);
		Overrides->SetField(Property->GetName(), JsonVal);
	}

	return Overrides;
}
