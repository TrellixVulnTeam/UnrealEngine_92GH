// Copyright Epic Games, Inc. All Rights Reserved.

#include "Kismet/BlueprintTypeConversions.h"

#define MAKE_CONVERSION_FUNCTION_NAME(SourceType, DestType)													\
	Convert##SourceType##To##DestType

#define DEFINE_CONVERSION_FUNCTIONS(BASETYPE, VARIANTTYPE1, VARIANTTYPE2, IMPL1TO2, IMPL2TO1)				\
	DEFINE_FUNCTION(UBlueprintTypeConversions::MAKE_CONVERSION_EXEC_FUNCTION_NAME(VARIANTTYPE1, VARIANTTYPE2))	\
	{																										\
		void* DestAddr = Stack.MostRecentPropertyAddress;													\
		Stack.MostRecentProperty = nullptr;																	\
		Stack.StepCompiledIn(nullptr, nullptr);																\
		const void* SourceAddr = Stack.MostRecentPropertyAddress;											\
		P_FINISH;																							\
		IMPL1TO2(SourceAddr, DestAddr);																		\
	}																										\
	DEFINE_FUNCTION(UBlueprintTypeConversions::MAKE_CONVERSION_EXEC_FUNCTION_NAME(VARIANTTYPE2, VARIANTTYPE1))	\
	{																										\
		void* DestAddr = Stack.MostRecentPropertyAddress;													\
		Stack.MostRecentProperty = nullptr;																	\
		Stack.StepCompiledIn(nullptr, nullptr);																\
		const void* SourceAddr = Stack.MostRecentPropertyAddress;											\
		P_FINISH;																							\
		IMPL2TO1(SourceAddr, DestAddr);																		\
	}																										\
	UE::Kismet::BlueprintTypeConversions::Internal::FStructConversionEntry BASETYPE##Entry(					\
		&TBaseStructure<BASETYPE>::Get,																		\
		&TVariantStructure<BASETYPE>::Get,																	\
		&TVariantStructure<VARIANTTYPE1>::Get,																\
		&TVariantStructure<VARIANTTYPE2>::Get,																\
		TEXT(PREPROCESSOR_TO_STRING(MAKE_CONVERSION_FUNCTION_NAME(VARIANTTYPE1, VARIANTTYPE2))),			\
		TEXT(PREPROCESSOR_TO_STRING(MAKE_CONVERSION_FUNCTION_NAME(VARIANTTYPE2, VARIANTTYPE1))),			\
		&IMPL1TO2,																							\
		&IMPL2TO1																							\
	);

namespace UE::Kismet::BlueprintTypeConversions::Internal
{

ConversionFunctionT FindConversionFunction(const FProperty* InFromProperty, const FProperty* InToProperty)
{
	ConversionFunctionT Result = nullptr;

	auto ConvertFloatToDouble = [](const void* InFromData, void* InToData)
	{
		const float* FromFloat = reinterpret_cast<const float*>(InFromData);
		check(FromFloat);
		double* ToDouble = reinterpret_cast<double*>(InToData);
		check(ToDouble);

		*ToDouble = *FromFloat;
	};

	auto ConvertDoubleToFloat = [](const void* InFromData, void* InToData)
	{
		const double* FromDouble = reinterpret_cast<const double*>(InFromData);
		check(FromDouble);
		float* ToFloat = reinterpret_cast<float*>(InToData);
		check(ToFloat);

		*ToFloat = static_cast<float>(*FromDouble);
	};

	if (InFromProperty->IsA<FFloatProperty>() && InToProperty->IsA<FDoubleProperty>())
	{
		Result = ConvertFloatToDouble;
	}
	else if (InFromProperty->IsA<FDoubleProperty>() && InToProperty->IsA<FFloatProperty>())
	{
		Result = ConvertDoubleToFloat;
	}
	else if (InFromProperty->IsA<FStructProperty>() && InToProperty->IsA<FStructProperty>())
	{
		const FStructProperty* InFromStructProperty = CastFieldChecked<FStructProperty>(InFromProperty);
		const FStructProperty* InToStructProperty = CastFieldChecked<FStructProperty>(InToProperty);
		const UScriptStruct* InFromStruct = InFromStructProperty->Struct;
		const UScriptStruct* InToStruct = InToStructProperty->Struct;

		TOptional<ConversionFunctionPairT> ConversionPair = FStructConversionTable::Get().GetConversionFunction(InFromStruct, InToStruct);
		check(ConversionPair.IsSet());
		Result = ConversionPair->Get<0>();
	}

	return Result;
}

struct FStructConversionEntry
{
	using GetUScriptStructFunctionT = UScriptStruct * (*)(void);

	FStructConversionEntry(GetUScriptStructFunctionT InGetBaseStruct,
		GetUScriptStructFunctionT InGetVariantFromBaseStruct,
		GetUScriptStructFunctionT InGetVariantStruct1,
		GetUScriptStructFunctionT InGetVariantStruct2,
		const TCHAR* InConvertVariant1ToVariant2FunctionName,
		const TCHAR* InConvertVariant2ToVariant1FunctionName,
		UE::Kismet::BlueprintTypeConversions::ConversionFunctionT InConvertVariant1ToVariant2Impl,
		UE::Kismet::BlueprintTypeConversions::ConversionFunctionT InConvertVariant2ToVariant1Impl)
		: GetBaseStruct(InGetBaseStruct)
		, GetVariantFromBaseStruct(InGetVariantFromBaseStruct)
		, GetVariantStruct1(InGetVariantStruct1)
		, GetVariantStruct2(InGetVariantStruct2)
		, ConvertVariant1ToVariant2FunctionName(InConvertVariant1ToVariant2FunctionName)
		, ConvertVariant2ToVariant1FunctionName(InConvertVariant2ToVariant1FunctionName)
		, ConvertVariant1ToVariant2Impl(InConvertVariant1ToVariant2Impl)
		, ConvertVariant2ToVariant1Impl(InConvertVariant2ToVariant1Impl)
	{
		NextEntry = FStructConversionEntry::GetListHead();
		FStructConversionEntry::GetListHead() = this;
	}

	GetUScriptStructFunctionT GetBaseStruct;
	GetUScriptStructFunctionT GetVariantFromBaseStruct;
	GetUScriptStructFunctionT GetVariantStruct1;
	GetUScriptStructFunctionT GetVariantStruct2;
	const TCHAR* ConvertVariant1ToVariant2FunctionName;
	const TCHAR* ConvertVariant2ToVariant1FunctionName;
	UE::Kismet::BlueprintTypeConversions::ConversionFunctionT ConvertVariant1ToVariant2Impl;
	UE::Kismet::BlueprintTypeConversions::ConversionFunctionT ConvertVariant2ToVariant1Impl;
	FStructConversionEntry* NextEntry;

	static FStructConversionEntry*& GetListHead()
	{
		static FStructConversionEntry* ListHead = nullptr;
		return ListHead;
	}
};

} // namespace UE::Kismet::BlueprintTypeConversions::Internal

namespace UE::Kismet::BlueprintTypeConversions
{

FStructConversionTable* FStructConversionTable::Instance = nullptr;

FStructConversionTable::FStructConversionTable()
{
	using namespace UE::Kismet::BlueprintTypeConversions;

	UClass* BlueprintTypeConversionsClass = UBlueprintTypeConversions::StaticClass();
	check(BlueprintTypeConversionsClass);

	Internal::FStructConversionEntry* Entry = Internal::FStructConversionEntry::GetListHead();
	while (Entry)
	{
		StructVariantLookupTable.Add(Entry->GetBaseStruct(), Entry->GetVariantFromBaseStruct());

		StructVariantPairT ConversionKeyPair1 = { Entry->GetVariantStruct1(), Entry->GetVariantStruct2() };
		ConversionFunctionPairT ConversionValuePair1 = 
			{ Entry->ConvertVariant1ToVariant2Impl,
			  BlueprintTypeConversionsClass->FindFunctionByName(Entry->ConvertVariant1ToVariant2FunctionName) };

		StructVariantPairT ConversionKeyPair2 = { Entry->GetVariantStruct2(), Entry->GetVariantStruct1() };
		ConversionFunctionPairT ConversionValuePair2 = 
			{ Entry->ConvertVariant2ToVariant1Impl,
			  BlueprintTypeConversionsClass->FindFunctionByName(Entry->ConvertVariant2ToVariant1FunctionName) };

		ImplicitCastLookupTable.Add(ConversionKeyPair1, ConversionValuePair1);
		ImplicitCastLookupTable.Add(ConversionKeyPair2, ConversionValuePair2);

		Entry = Entry->NextEntry;
	}
}

const FStructConversionTable& FStructConversionTable::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FStructConversionTable();
	}

	return *Instance;
}

TOptional<ConversionFunctionPairT> FStructConversionTable::GetConversionFunction(const UScriptStruct* InFrom, const UScriptStruct* InTo) const
{
	TOptional<ConversionFunctionPairT> Result;

	StructVariantPairT Key = GetVariantsFromStructs(InFrom, InTo);

	if (const ConversionFunctionPairT* Entry = ImplicitCastLookupTable.Find(Key))
	{
		Result = *Entry;
	}

	return Result;
}

FStructConversionTable::StructVariantPairT FStructConversionTable::GetVariantsFromStructs(const UScriptStruct* InStruct1, const UScriptStruct* InStruct2) const
{
	StructVariantPairT Result;

	if (const UScriptStruct* const* Variant = StructVariantLookupTable.Find(InStruct1))
	{
		Result.Get<0>() = *Variant;
	}
	else
	{
		Result.Get<0>() = InStruct1;
	}

	if (const UScriptStruct* const* Variant = StructVariantLookupTable.Find(InStruct2))
	{
		Result.Get<1>() = *Variant;
	}
	else
	{
		Result.Get<1>() = InStruct2;
	}

	return Result;
}

} // namespace UE::Kismet::BlueprintTypeConversions

UBlueprintTypeConversions::UBlueprintTypeConversions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

DEFINE_FUNCTION(UBlueprintTypeConversions::execConvertArrayType)
{
	using namespace UE::Kismet;

	FArrayProperty* DestArrayProperty = CastFieldChecked<FArrayProperty>(Stack.MostRecentProperty);
	void* DestArrayAddr = Stack.MostRecentPropertyAddress;

	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const void* SourceArrayAddr = Stack.MostRecentPropertyAddress;
	const FArrayProperty* SourceArrayProperty = CastFieldChecked<FArrayProperty>(Stack.MostRecentProperty);

	P_FINISH;

	FScriptArrayHelper SourceArray(SourceArrayProperty, SourceArrayAddr);
	FScriptArrayHelper DestArray(DestArrayProperty, DestArrayAddr);

	int SourceArraySize = SourceArray.Num();
	DestArray.Resize(SourceArraySize);

	BlueprintTypeConversions::ConversionFunctionT ConversionFunction =
		BlueprintTypeConversions::Internal::FindConversionFunction(SourceArrayProperty->Inner, DestArrayProperty->Inner);
	check(ConversionFunction);

	for (int i = 0; i < SourceArraySize; ++i)
	{
		const void* SrcData = SourceArray.GetRawPtr(i);
		void* DestData = DestArray.GetRawPtr(i);
		(*ConversionFunction)(SrcData, DestData);
	}
}

DEFINE_FUNCTION(UBlueprintTypeConversions::execConvertSetType)
{
	using namespace UE::Kismet;

	FSetProperty* DestSetProperty = CastFieldChecked<FSetProperty>(Stack.MostRecentProperty);
	void* DestSetAddr = Stack.MostRecentPropertyAddress;

	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FSetProperty>(nullptr);
	const void* SourceSetAddr = Stack.MostRecentPropertyAddress;
	const FSetProperty* SourceSetProperty = CastFieldChecked<FSetProperty>(Stack.MostRecentProperty);

	P_FINISH;

	FScriptSetHelper SourceSet(SourceSetProperty, SourceSetAddr);
	FScriptSetHelper DestSet(DestSetProperty, DestSetAddr);

	int SourceSetSize = SourceSet.Num();
	DestSet.EmptyElements(SourceSetSize);

	BlueprintTypeConversions::ConversionFunctionT ConversionFunction = 
		BlueprintTypeConversions::Internal::FindConversionFunction(SourceSetProperty->ElementProp, DestSetProperty->ElementProp);
	check(ConversionFunction);

	for (int i = 0; i < SourceSetSize; ++i)
	{
		const void* SrcData = SourceSet.GetElementPtr(i);

		int32 NewIndex = DestSet.AddDefaultValue_Invalid_NeedsRehash();
		void* DestData = DestSet.GetElementPtr(NewIndex);
		(*ConversionFunction)(SrcData, DestData);
	}

	DestSet.Rehash();
}

DEFINE_FUNCTION(UBlueprintTypeConversions::execConvertMapType)
{
	using namespace UE::Kismet;

	FMapProperty* DestMapProperty = CastFieldChecked<FMapProperty>(Stack.MostRecentProperty);
	void* DestMapAddr = Stack.MostRecentPropertyAddress;

	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FMapProperty>(nullptr);
	const void* SourceMapAddr = Stack.MostRecentPropertyAddress;
	const FMapProperty* SourceMapProperty = CastFieldChecked<FMapProperty>(Stack.MostRecentProperty);

	P_FINISH;

	FScriptMapHelper SourceMap(SourceMapProperty, SourceMapAddr);
	FScriptMapHelper DestMap(DestMapProperty, DestMapAddr);

	int SourceMapSize = SourceMap.Num();
	DestMap.EmptyValues(SourceMapSize);

	BlueprintTypeConversions::ConversionFunctionT KeyConversionFunction = 
		BlueprintTypeConversions::Internal::FindConversionFunction(SourceMapProperty->KeyProp, DestMapProperty->KeyProp);
	BlueprintTypeConversions::ConversionFunctionT ValueConversionFunction = 
		BlueprintTypeConversions::Internal::FindConversionFunction(SourceMapProperty->ValueProp, DestMapProperty->ValueProp);

	for (int i = 0; i < SourceMapSize; ++i)
	{
		int32 NewIndex = DestMap.AddDefaultValue_Invalid_NeedsRehash();

		const void* SourceKeyRawData = SourceMap.GetKeyPtr(i);
		void* DestinationKeyRawData = DestMap.GetKeyPtr(NewIndex);

		if (KeyConversionFunction)
		{
			(*KeyConversionFunction)(SourceKeyRawData, DestinationKeyRawData);
		}
		else
		{
			SourceMapProperty->KeyProp->CopySingleValue(DestinationKeyRawData, SourceKeyRawData);
		}

		const void* SourceValueRawData = SourceMap.GetValuePtr(i);
		void* DestinationValueRawData = DestMap.GetValuePtr(NewIndex);

		if (ValueConversionFunction)
		{
			(*ValueConversionFunction)(SourceValueRawData, DestinationValueRawData);
		}
		else
		{
			SourceMapProperty->ValueProp->CopySingleValue(DestinationValueRawData, SourceValueRawData);
		}
	}

	DestMap.Rehash();
}

// Container conversions

FORCEINLINE_DEBUGGABLE void ConvertFVector3dToFVector3fImpl(const void* InFromData, void* InToData)
{
	const FVector3d* From = reinterpret_cast<const FVector3d*>(InFromData);
	check(From);
	FVector3f* To = reinterpret_cast<FVector3f*>(InToData);
	check(To);

	float x = static_cast<float>(From->X);
	float y = static_cast<float>(From->Y);
	float z = static_cast<float>(From->Z);

	*To = FVector3f(x, y, z);
}

FORCEINLINE_DEBUGGABLE void ConvertFVector3fToFVector3dImpl(const void* InFromData, void* InToData)
{
	const FVector3f* From = reinterpret_cast<const FVector3f*>(InFromData);
	check(From);
	FVector3d* To = reinterpret_cast<FVector3d*>(InToData);
	check(To);

	*To = FVector3d(From->X, From->Y, From->Z);
}

DEFINE_CONVERSION_FUNCTIONS(FVector, FVector3d, FVector3f, ConvertFVector3dToFVector3fImpl, ConvertFVector3fToFVector3dImpl)
