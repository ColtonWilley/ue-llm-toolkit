// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

struct FGraphLayoutConfig
{
	float SpacingX = 400.0f;
	float SpacingY = 200.0f;
	bool bPreserveExisting = false;
	bool bReverseDepth = false;
};

struct FGraphLayoutResult
{
	int32 TotalNodes = 0;
	int32 LayoutNodes = 0;
	int32 SkippedNodes = 0;
	int32 DisconnectedNodes = 0;
	int32 DataOnlyNodes = 0;
	int32 EntryPoints = 0;
};

using FEdgePolicyFunc = TFunction<TArray<UEdGraphNode*>(UEdGraphNode*)>;
using FEntryFinderFunc = TFunction<TArray<UEdGraphNode*>(UEdGraph*)>;
using FDataConsumerFinderFunc = TFunction<TArray<UEdGraphNode*>(UEdGraphNode*)>;

class FGraphLayoutHelper
{
public:
	static FGraphLayoutResult LayoutGraph(
		UEdGraph* Graph,
		const FGraphLayoutConfig& Config,
		const FEdgePolicyFunc& EdgePolicy,
		const FEntryFinderFunc& EntryFinder,
		const FDataConsumerFinderFunc& DataConsumerFinder
	);

	static FEdgePolicyFunc MakeK2ExecPolicy();
	static FEdgePolicyFunc MakeAnimPosePolicy();
	static FEntryFinderFunc MakeK2EntryFinder();
	static FEntryFinderFunc MakeAnimEntryFinder();
	static FDataConsumerFinderFunc MakeDataConsumerFinder();
};
