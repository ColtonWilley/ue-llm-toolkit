// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"

struct FDebugPrintFunctionItem
{
	FString FunctionName;
	FString TargetClass;
	TMap<FString, FString> Params;
	FString Label;
};

struct FDebugPrintConfig
{
	FString Label;
	FString EventName;
	bool bPrintToScreen = false;
	bool bPrintToLog = true;
	TArray<FString> Variables;
	TArray<FDebugPrintFunctionItem> Functions;
};

struct FDebugPrintResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> CreatedNodeIds;
	int32 RemovedCount = 0;
};

class FDebugPrintBuilder
{
public:
	static FDebugPrintResult AddDebugPrint(UBlueprint* Blueprint, const FDebugPrintConfig& Config);
	static FDebugPrintResult RemoveDebugPrint(UBlueprint* Blueprint, const FString& Label);

private:
	static FString MakeNodeIdPrefix(const FString& Label);
	static UEdGraphNode* FindExistingEventNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& EventName);
	static int32 RemoveNodesByPrefix(UEdGraph* Graph, const FString& Prefix);
};
