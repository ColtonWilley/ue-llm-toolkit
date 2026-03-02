// Copyright Natali Caggiano. All Rights Reserved.

#include "GraphLayoutHelper.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateResult.h"
#include "Animation/AnimNodeBase.h"

FGraphLayoutResult FGraphLayoutHelper::LayoutGraph(
	UEdGraph* Graph,
	const FGraphLayoutConfig& Config,
	const FEdgePolicyFunc& EdgePolicy,
	const FEntryFinderFunc& EntryFinder,
	const FDataConsumerFinderFunc& DataConsumerFinder)
{
	FGraphLayoutResult Result;
	if (!Graph) return Result;

	TArray<UEdGraphNode*> AllNodes = Graph->Nodes;
	Result.TotalNodes = AllNodes.Num();
	if (Result.TotalNodes == 0) return Result;

	TSet<UEdGraphNode*> Visited;
	TMap<UEdGraphNode*, int32> DepthMap;
	TMap<UEdGraphNode*, int32> YIndexMap;

	TSet<UEdGraphNode*> SkippedSet;
	if (Config.bPreserveExisting)
	{
		for (UEdGraphNode* Node : AllNodes)
		{
			if (Node && (Node->NodePosX != 0 || Node->NodePosY != 0))
			{
				SkippedSet.Add(Node);
			}
		}
		Result.SkippedNodes = SkippedSet.Num();
	}

	TArray<UEdGraphNode*> EntryNodes = EntryFinder(Graph);
	Result.EntryPoints = EntryNodes.Num();

	int32 GlobalYOffset = 0;

	for (UEdGraphNode* EntryNode : EntryNodes)
	{
		if (!EntryNode || Visited.Contains(EntryNode) || SkippedSet.Contains(EntryNode))
		{
			continue;
		}

		TQueue<UEdGraphNode*> Queue;
		Queue.Enqueue(EntryNode);
		Visited.Add(EntryNode);
		DepthMap.Add(EntryNode, 0);

		TMap<int32, int32> DepthYCounter;
		DepthYCounter.Add(0, 0);

		YIndexMap.Add(EntryNode, GlobalYOffset);

		int32 SubgraphMaxY = GlobalYOffset;

		UEdGraphNode* Current = nullptr;
		while (Queue.Dequeue(Current))
		{
			if (SkippedSet.Contains(Current)) continue;

			int32 CurrentDepth = DepthMap[Current];
			TArray<UEdGraphNode*> Successors = EdgePolicy(Current);

			for (UEdGraphNode* Succ : Successors)
			{
				if (!Succ || Visited.Contains(Succ) || SkippedSet.Contains(Succ)) continue;

				Visited.Add(Succ);
				int32 SuccDepth = CurrentDepth + 1;
				DepthMap.Add(Succ, SuccDepth);

				if (!DepthYCounter.Contains(SuccDepth))
				{
					DepthYCounter.Add(SuccDepth, 0);
				}
				int32 YIdx = GlobalYOffset + DepthYCounter[SuccDepth];
				DepthYCounter[SuccDepth]++;
				YIndexMap.Add(Succ, YIdx);

				if (YIdx > SubgraphMaxY) SubgraphMaxY = YIdx;

				Queue.Enqueue(Succ);
			}
		}

		GlobalYOffset = SubgraphMaxY + 2;
	}

	if (DataConsumerFinder)
	{
		TSet<TPair<int32,int32>> OccupiedSlots;
		for (auto& Pair : DepthMap)
		{
			if (YIndexMap.Contains(Pair.Key))
			{
				OccupiedSlots.Add(TPair<int32,int32>(Pair.Value, YIndexMap[Pair.Key]));
			}
		}

		for (UEdGraphNode* Node : AllNodes)
		{
			if (!Node || Visited.Contains(Node) || SkippedSet.Contains(Node)) continue;

			TArray<UEdGraphNode*> Consumers = DataConsumerFinder(Node);
			UEdGraphNode* BestConsumer = nullptr;
			for (UEdGraphNode* Consumer : Consumers)
			{
				if (DepthMap.Contains(Consumer))
				{
					if (!BestConsumer || DepthMap[Consumer] < DepthMap[BestConsumer])
					{
						BestConsumer = Consumer;
					}
				}
			}

			if (BestConsumer)
			{
				int32 ConsumerDepth = DepthMap[BestConsumer];
				int32 ConsumerY = YIndexMap[BestConsumer];
				int32 DataDepth = FMath::Max(0, ConsumerDepth - 1);

				int32 CandidateY = ConsumerY;
				while (OccupiedSlots.Contains(TPair<int32,int32>(DataDepth, CandidateY)))
				{
					CandidateY++;
				}
				OccupiedSlots.Add(TPair<int32,int32>(DataDepth, CandidateY));

				DepthMap.Add(Node, DataDepth);
				YIndexMap.Add(Node, CandidateY);
				Visited.Add(Node);
				Result.DataOnlyNodes++;

				if (CandidateY >= GlobalYOffset)
				{
					GlobalYOffset = CandidateY + 1;
				}
			}
		}
	}

	for (UEdGraphNode* Node : AllNodes)
	{
		if (!Node || Visited.Contains(Node) || SkippedSet.Contains(Node)) continue;

		DepthMap.Add(Node, 0);
		YIndexMap.Add(Node, GlobalYOffset);
		Visited.Add(Node);
		GlobalYOffset++;
		Result.DisconnectedNodes++;
	}

	int32 MaxDepth = 0;
	if (Config.bReverseDepth)
	{
		for (auto& Pair : DepthMap)
		{
			if (Pair.Value > MaxDepth) MaxDepth = Pair.Value;
		}
	}

	for (UEdGraphNode* Node : AllNodes)
	{
		if (!Node || SkippedSet.Contains(Node)) continue;
		if (!DepthMap.Contains(Node) || !YIndexMap.Contains(Node)) continue;

		int32 Depth = Config.bReverseDepth ? (MaxDepth - DepthMap[Node]) : DepthMap[Node];
		Node->NodePosX = Depth * Config.SpacingX;
		Node->NodePosY = YIndexMap[Node] * Config.SpacingY;
		Result.LayoutNodes++;
	}

	return Result;
}

FEdgePolicyFunc FGraphLayoutHelper::MakeK2ExecPolicy()
{
	return [](UEdGraphNode* Node) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Successors;
		if (!Node) return Successors;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				!Pin->bHidden)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode())
					{
						Successors.AddUnique(LinkedPin->GetOwningNode());
					}
				}
			}
		}
		return Successors;
	};
}

FEdgePolicyFunc FGraphLayoutHelper::MakeAnimPosePolicy()
{
	return [](UEdGraphNode* Node) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Successors;
		if (!Node) return Successors;

		static UScriptStruct* PoseLinkBase = FPoseLinkBase::StaticStruct();

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Input || Pin->bHidden) continue;

			UObject* SubObj = Pin->PinType.PinSubCategoryObject.Get();
			UScriptStruct* Struct = Cast<UScriptStruct>(SubObj);
			if (!Struct || !Struct->IsChildOf(PoseLinkBase)) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					Successors.AddUnique(LinkedPin->GetOwningNode());
				}
			}
		}
		return Successors;
	};
}

FEntryFinderFunc FGraphLayoutHelper::MakeK2EntryFinder()
{
	return [](UEdGraph* Graph) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Entries;
		if (!Graph) return Entries;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node_Event>(Node) ||
				Cast<UK2Node_CustomEvent>(Node) ||
				Cast<UK2Node_FunctionEntry>(Node))
			{
				Entries.Add(Node);
			}
		}
		return Entries;
	};
}

FEntryFinderFunc FGraphLayoutHelper::MakeAnimEntryFinder()
{
	return [](UEdGraph* Graph) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Entries;
		if (!Graph) return Entries;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UAnimGraphNode_Root>(Node) ||
				Cast<UAnimGraphNode_StateResult>(Node))
			{
				Entries.Add(Node);
			}
		}
		return Entries;
	};
}

FDataConsumerFinderFunc FGraphLayoutHelper::MakeDataConsumerFinder()
{
	return [](UEdGraphNode* Node) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Consumers;
		if (!Node) return Consumers;

		static UScriptStruct* PoseLinkBase = FPoseLinkBase::StaticStruct();

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Output || Pin->bHidden) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			UObject* SubObj = Pin->PinType.PinSubCategoryObject.Get();
			UScriptStruct* Struct = Cast<UScriptStruct>(SubObj);
			if (Struct && Struct->IsChildOf(PoseLinkBase)) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					Consumers.AddUnique(LinkedPin->GetOwningNode());
				}
			}
		}
		return Consumers;
	};
}
