// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliPinType.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

namespace UECli::PinType
{
	bool FromSpec(const FString& InSpec, FEdGraphPinType& OutType, FString& OutError)
	{
		OutType = FEdGraphPinType();

		FString Spec = InSpec.TrimStartAndEnd();
		if (Spec.EndsWith(TEXT("[]")))
		{
			OutType.ContainerType = EPinContainerType::Array;
			Spec = Spec.LeftChop(2).TrimStartAndEnd();
		}

		const FName K = *Spec;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		if (Spec.Equals(TEXT("bool"), ESearchCase::IgnoreCase))       { OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true; }
		if (Spec.Equals(TEXT("int"), ESearchCase::IgnoreCase) ||
			Spec.Equals(TEXT("int32"), ESearchCase::IgnoreCase))      { OutType.PinCategory = UEdGraphSchema_K2::PC_Int; return true; }
		if (Spec.Equals(TEXT("int64"), ESearchCase::IgnoreCase))      { OutType.PinCategory = UEdGraphSchema_K2::PC_Int64; return true; }
		if (Spec.Equals(TEXT("byte"), ESearchCase::IgnoreCase))       { OutType.PinCategory = UEdGraphSchema_K2::PC_Byte; return true; }
		if (Spec.Equals(TEXT("float"), ESearchCase::IgnoreCase) ||
			Spec.Equals(TEXT("real"), ESearchCase::IgnoreCase))       { OutType.PinCategory = UEdGraphSchema_K2::PC_Real; OutType.PinSubCategory = UEdGraphSchema_K2::PC_Float; return true; }
		if (Spec.Equals(TEXT("double"), ESearchCase::IgnoreCase))     { OutType.PinCategory = UEdGraphSchema_K2::PC_Real; OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double; return true; }
		if (Spec.Equals(TEXT("string"), ESearchCase::IgnoreCase))     { OutType.PinCategory = UEdGraphSchema_K2::PC_String; return true; }
		if (Spec.Equals(TEXT("name"), ESearchCase::IgnoreCase))       { OutType.PinCategory = UEdGraphSchema_K2::PC_Name; return true; }
		if (Spec.Equals(TEXT("text"), ESearchCase::IgnoreCase))       { OutType.PinCategory = UEdGraphSchema_K2::PC_Text; return true; }

		// Common structs
		auto AsStruct = [&](const TCHAR* Name, UScriptStruct* Struct)
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = Struct;
		};
		if (Spec.Equals(TEXT("vector"), ESearchCase::IgnoreCase))     { AsStruct(TEXT("Vector"), TBaseStructure<FVector>::Get()); return true; }
		if (Spec.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))    { AsStruct(TEXT("Rotator"), TBaseStructure<FRotator>::Get()); return true; }
		if (Spec.Equals(TEXT("transform"), ESearchCase::IgnoreCase))  { AsStruct(TEXT("Transform"), TBaseStructure<FTransform>::Get()); return true; }
		if (Spec.Equals(TEXT("color"), ESearchCase::IgnoreCase) ||
			Spec.Equals(TEXT("linearcolor"), ESearchCase::IgnoreCase)){ AsStruct(TEXT("LinearColor"), TBaseStructure<FLinearColor>::Get()); return true; }

		// Object reference: Blueprint path or native class name.
		UClass* ObjectClass = nullptr;
		if (Spec.StartsWith(TEXT("/")))
		{
			FString ObjectPath = Spec;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				FString AssetName;
				Spec.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				ObjectPath = Spec + TEXT(".") + AssetName;
			}
			ObjectClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
		}
		else
		{
			ObjectClass = FindFirstObject<UClass>(*Spec, EFindFirstObjectOptions::NativeFirst);
		}

		if (ObjectClass)
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutType.PinSubCategoryObject = ObjectClass;
			return true;
		}

		// A raw struct name (FMyStruct without the F).
		if (UScriptStruct* AnyStruct = FindFirstObject<UScriptStruct>(*Spec, EFindFirstObjectOptions::NativeFirst))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = AnyStruct;
			return true;
		}

		OutError = FString::Printf(TEXT("Could not interpret '%s' as a variable type."), *InSpec);
		return false;
	}
}
