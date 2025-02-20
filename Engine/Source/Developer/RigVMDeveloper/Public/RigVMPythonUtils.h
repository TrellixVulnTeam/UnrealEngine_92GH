﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMDeveloperModule.h"
#include "UObject/Class.h"

namespace RigVMPythonUtils
{
	RIGVMDEVELOPER_API FString NameToPep8(const FString& Name);

	RIGVMDEVELOPER_API FString TransformToPythonString(const FTransform& Transform);

	RIGVMDEVELOPER_API FString Vector2DToPythonString(const FVector2D& Vector);

	RIGVMDEVELOPER_API FString LinearColorToPythonString(const FLinearColor& Color);

	RIGVMDEVELOPER_API FString EnumValueToPythonString(UEnum* Enum, int64 Value);
	
	template<typename T>
	FORCEINLINE FString EnumValueToPythonString(int64 Value)
	{
		return EnumValueToPythonString(StaticEnum<T>(), Value);
	}
	
#if WITH_EDITOR
	RIGVMDEVELOPER_API void Print(const FString& BlueprintTitle, const FString& InMessage);

	RIGVMDEVELOPER_API void PrintPythonContext(const FString& InBlueprintName);

#endif
}
