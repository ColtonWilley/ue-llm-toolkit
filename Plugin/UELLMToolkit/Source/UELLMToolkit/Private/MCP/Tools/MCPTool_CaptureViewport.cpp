// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_CaptureViewport.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "SEditorViewport.h"
#include "PersonaTabs.h"
#include "BlueprintEditorTabs.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "EngineUtils.h"
#include "RenderingThread.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr int32 TargetWidth = 1024;
	constexpr int32 TargetHeight = 576;
	constexpr int32 JPEGQuality = 70;

	void ResizePixels(const TArray<FColor>& InPixels, int32 InWidth, int32 InHeight,
		TArray<FColor>& OutPixels, int32 OutWidth, int32 OutHeight)
	{
		OutPixels.SetNumUninitialized(OutWidth * OutHeight);

		const float ScaleX = static_cast<float>(InWidth) / OutWidth;
		const float ScaleY = static_cast<float>(InHeight) / OutHeight;

		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			for (int32 X = 0; X < OutWidth; ++X)
			{
				const int32 SrcX = FMath::Clamp(static_cast<int32>(X * ScaleX), 0, InWidth - 1);
				const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * ScaleY), 0, InHeight - 1);
				OutPixels[Y * OutWidth + X] = InPixels[SrcY * InWidth + SrcX];
			}
		}
	}

	FMCPToolResult CaptureFromViewport(FViewport* Viewport, const FString& ViewportType,
		const TSharedPtr<FJsonObject>& ExtraFields = nullptr)
	{
		const FIntPoint ViewportSize = Viewport->GetSizeXY();
		if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
		{
			return FMCPToolResult::Error(TEXT("Viewport has invalid size."));
		}

		TArray<FColor> Pixels;
		if (!Viewport->ReadPixels(Pixels))
		{
			return FMCPToolResult::Error(TEXT("Failed to read viewport pixels."));
		}

		const int32 ExpectedPixels = ViewportSize.X * ViewportSize.Y;
		if (Pixels.Num() != ExpectedPixels)
		{
			return FMCPToolResult::Error(TEXT("Pixel array size mismatch."));
		}

		TArray<FColor> ResizedPixels;
		ResizePixels(Pixels, ViewportSize.X, ViewportSize.Y, ResizedPixels, TargetWidth, TargetHeight);

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

		if (!ImageWrapper.IsValid())
		{
			return FMCPToolResult::Error(TEXT("Failed to create image wrapper."));
		}

		if (!ImageWrapper->SetRaw(ResizedPixels.GetData(), ResizedPixels.Num() * sizeof(FColor),
			TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
		{
			return FMCPToolResult::Error(TEXT("Failed to set image data."));
		}

		TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JPEGQuality);
		if (CompressedData.Num() == 0)
		{
			return FMCPToolResult::Error(TEXT("Failed to compress image to JPEG."));
		}

		const FString Base64Image = FBase64::Encode(CompressedData.GetData(), CompressedData.Num());

		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("image_base64"), Base64Image);
		ResultData->SetNumberField(TEXT("width"), TargetWidth);
		ResultData->SetNumberField(TEXT("height"), TargetHeight);
		ResultData->SetStringField(TEXT("format"), TEXT("jpeg"));
		ResultData->SetNumberField(TEXT("quality"), JPEGQuality);
		ResultData->SetStringField(TEXT("viewport_type"), ViewportType);
		ResultData->SetNumberField(TEXT("original_width"), ViewportSize.X);
		ResultData->SetNumberField(TEXT("original_height"), ViewportSize.Y);

		FViewportClient* Client = Viewport->GetClient();
		if (Client)
		{
			FEditorViewportClient* EditorClient = static_cast<FEditorViewportClient*>(Client);
			FVector CamLoc = EditorClient->GetViewLocation();
			FRotator CamRot = EditorClient->GetViewRotation();

			TSharedPtr<FJsonObject> LocJson = MakeShared<FJsonObject>();
			LocJson->SetNumberField(TEXT("x"), CamLoc.X);
			LocJson->SetNumberField(TEXT("y"), CamLoc.Y);
			LocJson->SetNumberField(TEXT("z"), CamLoc.Z);
			ResultData->SetObjectField(TEXT("camera_location"), LocJson);

			TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
			RotJson->SetNumberField(TEXT("pitch"), CamRot.Pitch);
			RotJson->SetNumberField(TEXT("yaw"), CamRot.Yaw);
			RotJson->SetNumberField(TEXT("roll"), CamRot.Roll);
			ResultData->SetObjectField(TEXT("camera_rotation"), RotJson);
		}

		if (ExtraFields.IsValid())
		{
			for (const auto& Pair : ExtraFields->Values)
			{
				ResultData->SetField(Pair.Key, Pair.Value);
			}
		}

		UE_LOG(LogUnrealClaude, Log, TEXT("Captured %s viewport: %dx%d -> %dx%d JPEG (%d bytes base64)"),
			*ViewportType, ViewportSize.X, ViewportSize.Y, TargetWidth, TargetHeight, Base64Image.Len());

		return FMCPToolResult::Success(
			FString::Printf(TEXT("Captured %s viewport: %dx%d JPEG"), *ViewportType, TargetWidth, TargetHeight),
			ResultData
		);
	}

	bool IsEditorViewportType(const FName& TypeName)
	{
		static const FName SEditorViewportName("SEditorViewport");
		static const FName SAnimEditorViewportName("SAnimationEditorViewport");
		return TypeName == SEditorViewportName || TypeName == SAnimEditorViewportName;
	}

	bool ApplyCamera(FViewport* Viewport, const TSharedRef<FJsonObject>& Params, TSharedPtr<FJsonObject>& ExtraFields)
	{
		const TSharedPtr<FJsonObject>* LocObj = nullptr;
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		bool bHasLocation = Params->TryGetObjectField(TEXT("camera_location"), LocObj);
		bool bHasRotation = Params->TryGetObjectField(TEXT("camera_rotation"), RotObj);

		if (!bHasLocation && !bHasRotation)
		{
			return false;
		}

		FViewportClient* Client = Viewport->GetClient();
		if (!Client)
		{
			return false;
		}

		FEditorViewportClient* EditorClient = static_cast<FEditorViewportClient*>(Client);

		if (bHasLocation && LocObj && LocObj->IsValid())
		{
			FVector Loc(ForceInit);
			(*LocObj)->TryGetNumberField(TEXT("x"), Loc.X);
			(*LocObj)->TryGetNumberField(TEXT("y"), Loc.Y);
			(*LocObj)->TryGetNumberField(TEXT("z"), Loc.Z);
			EditorClient->SetViewLocation(Loc);

			if (ExtraFields.IsValid())
			{
				TSharedPtr<FJsonObject> LocJson = MakeShared<FJsonObject>();
				LocJson->SetNumberField(TEXT("x"), Loc.X);
				LocJson->SetNumberField(TEXT("y"), Loc.Y);
				LocJson->SetNumberField(TEXT("z"), Loc.Z);
				ExtraFields->SetObjectField(TEXT("camera_location"), LocJson);
			}
		}

		if (bHasRotation && RotObj && RotObj->IsValid())
		{
			FRotator Rot(ForceInit);
			(*RotObj)->TryGetNumberField(TEXT("pitch"), Rot.Pitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), Rot.Yaw);
			(*RotObj)->TryGetNumberField(TEXT("roll"), Rot.Roll);
			EditorClient->SetViewRotation(Rot);

			if (ExtraFields.IsValid())
			{
				TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
				RotJson->SetNumberField(TEXT("pitch"), Rot.Pitch);
				RotJson->SetNumberField(TEXT("yaw"), Rot.Yaw);
				RotJson->SetNumberField(TEXT("roll"), Rot.Roll);
				ExtraFields->SetObjectField(TEXT("camera_rotation"), RotJson);
			}
		}

		EditorClient->Invalidate();
		Viewport->Draw(false);
		FlushRenderingCommands();

		return true;
	}

	TSharedPtr<FSceneViewport> FindSceneViewportInTree(TSharedRef<SWidget> Root)
	{
		if (IsEditorViewportType(Root->GetType()))
		{
			SEditorViewport& Viewport = static_cast<SEditorViewport&>(Root.Get());
			TSharedPtr<FSceneViewport> Scene = Viewport.GetSceneViewport();
			if (Scene.IsValid())
			{
				return Scene;
			}
		}

		FChildren* Children = Root->GetChildren();
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			TSharedPtr<FSceneViewport> Found = FindSceneViewportInTree(Children->GetChildAt(i));
			if (Found.IsValid())
			{
				return Found;
			}
		}

		return nullptr;
	}
}

FMCPToolResult FMCPTool_CaptureViewport::Execute(const TSharedRef<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor is not available."));
	}

	FString AssetPath;
	if (Params->TryGetStringField(TEXT("asset_path"), AssetPath) && !AssetPath.IsEmpty())
	{
		UObject* Asset = FSoftObjectPath(AssetPath).TryLoad();
		if (!Asset)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		}

		UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!EditorSubsystem)
		{
			return FMCPToolResult::Error(TEXT("AssetEditorSubsystem not available."));
		}

		IAssetEditorInstance* Editor = EditorSubsystem->FindEditorForAsset(Asset, false);
		if (!Editor)
		{
			UE_LOG(LogUnrealClaude, Log, TEXT("CaptureViewport: No editor open for %s, opening now"), *AssetPath);
			EditorSubsystem->OpenEditorForAsset(Asset);
			Editor = EditorSubsystem->FindEditorForAsset(Asset, false);
			if (!Editor)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to open editor for asset: %s"), *AssetPath));
			}
		}

		TSharedPtr<FTabManager> TabManager = Editor->GetAssociatedTabManager();
		if (!TabManager.IsValid())
		{
			return FMCPToolResult::Error(TEXT("Could not get tab manager from asset editor."));
		}

		TSharedPtr<SDockTab> ViewportTab = TabManager->FindExistingLiveTab(FPersonaTabs::PreviewViewportID);
		if (!ViewportTab.IsValid())
		{
			ViewportTab = TabManager->FindExistingLiveTab(FBlueprintEditorTabs::SCSViewportID);
		}
		if (!ViewportTab.IsValid())
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("No preview viewport tab found for asset: %s. This editor type may not have a 3D preview."), *AssetPath));
		}

		TSharedRef<SWidget> TabContent = ViewportTab->GetContent();

		TSharedPtr<FSceneViewport> SceneViewport = FindSceneViewportInTree(TabContent);
		if (!SceneViewport.IsValid())
		{
			return FMCPToolResult::Error(TEXT("Could not find a valid scene viewport in the preview tab."));
		}

		TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("asset_path"), AssetPath);

		double FrameDouble = -1.0;
		Params->TryGetNumberField(TEXT("frame"), FrameDouble);
		const int32 RequestedFrame = (FrameDouble >= 0.0) ? FMath::RoundToInt32(FrameDouble) : -1;

		USkeletalMeshComponent* PreviewSkelComp = nullptr;

		if (RequestedFrame >= 0)
		{
			UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(Asset);
			if (!AnimAsset)
			{
				return FMCPToolResult::Error(TEXT("'frame' parameter requires an animation asset (AnimSequence, AnimMontage, etc.)."));
			}

			float TargetTime = 0.f;
			int32 TotalFrames = 0;
			FFrameRate FrameRate(30, 1);

			if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(AnimAsset))
			{
				FrameRate = AnimSeq->GetSamplingFrameRate();
				TotalFrames = AnimSeq->GetNumberOfSampledKeys();
				TargetTime = RequestedFrame / static_cast<float>(FrameRate.AsDecimal());
			}
			else
			{
				TargetTime = RequestedFrame / 30.0f;
			}

			const float PlayLength = AnimAsset->GetPlayLength();
			TargetTime = FMath::Clamp(TargetTime, 0.f, PlayLength);

			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World) continue;

				for (TActorIterator<AActor> It(World); It; ++It)
				{
					TArray<USkeletalMeshComponent*> SkelComps;
					(*It)->GetComponents<USkeletalMeshComponent>(SkelComps);
					for (USkeletalMeshComponent* SkelComp : SkelComps)
					{
						UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(SkelComp->GetAnimInstance());
						if (SingleNode && SingleNode->GetAnimationAsset() == AnimAsset)
						{
							SingleNode->SetPlaying(false);
							SingleNode->SetPosition(TargetTime, false);
							SkelComp->TickAnimation(0.f, false);
							SkelComp->RefreshBoneTransforms();
							PreviewSkelComp = SkelComp;
							break;
						}
					}
					if (PreviewSkelComp) break;
				}
				if (PreviewSkelComp) break;
			}

			if (!PreviewSkelComp)
			{
				return FMCPToolResult::Error(TEXT("Could not find preview component playing this animation. Make sure the animation preview is visible."));
			}

			Extra->SetNumberField(TEXT("frame"), RequestedFrame);
			Extra->SetNumberField(TEXT("frame_time"), TargetTime);
			Extra->SetNumberField(TEXT("play_length"), PlayLength);
			Extra->SetNumberField(TEXT("total_frames"), TotalFrames);
			Extra->SetStringField(TEXT("frame_rate"), FString::Printf(TEXT("%d/%d"), FrameRate.Numerator, FrameRate.Denominator));

			UE_LOG(LogUnrealClaude, Log, TEXT("Scrubbed animation to frame %d (%.3fs / %.3fs)"),
				RequestedFrame, TargetTime, PlayLength);
		}

		FEditorViewportClient* EditorClient = static_cast<FEditorViewportClient*>(SceneViewport->GetClient());

		const TSharedPtr<FJsonObject>* TestLoc = nullptr;
		const TSharedPtr<FJsonObject>* TestRot = nullptr;
		bool bHasCameraOverride = Params->TryGetObjectField(TEXT("camera_location"), TestLoc)
			|| Params->TryGetObjectField(TEXT("camera_rotation"), TestRot);

		FString CameraPreset;
		Params->TryGetStringField(TEXT("camera"), CameraPreset);

		if (bHasCameraOverride)
		{
			ApplyCamera(SceneViewport.Get(), Params, Extra);
		}
		else if (EditorClient)
		{
			FVector CamPos(61.84, 702.57, 117.44);
			FRotator CamRot(-5.78, -97.59, 0.0);

			if (CameraPreset.Equals(TEXT("from_left"), ESearchCase::IgnoreCase))
			{
				CamPos = FVector(-668.44, 238.13, 122.10);
				CamRot = FRotator(-4.18, -14.59, 0.0);
			}
			else if (CameraPreset.Equals(TEXT("from_right"), ESearchCase::IgnoreCase))
			{
				CamPos = FVector(539.56, 322.97, 131.99);
				CamRot = FRotator(-4.98, -158.99, 0.0);
			}

			EditorClient->SetViewLocation(CamPos);
			EditorClient->SetViewRotation(CamRot);

			Extra->SetStringField(TEXT("camera_preset"), CameraPreset.IsEmpty() ? TEXT("front") : CameraPreset);
		}

		EditorClient->Invalidate();
		SceneViewport->Draw(false);
		FlushRenderingCommands();

		FIntPoint CheckSize = SceneViewport->GetSizeXY();
		if (CheckSize.X <= 0 || CheckSize.Y <= 0)
		{
			for (int32 Retry = 0; Retry < 5; ++Retry)
			{
				FPlatformProcess::Sleep(0.1f);
				FSlateApplication::Get().Tick();
				FlushRenderingCommands();

				EditorClient->Invalidate();
				SceneViewport->Draw(false);
				FlushRenderingCommands();

				CheckSize = SceneViewport->GetSizeXY();
				if (CheckSize.X > 0 && CheckSize.Y > 0)
				{
					UE_LOG(LogUnrealClaude, Log, TEXT("CaptureViewport: Viewport became valid after %d retries"), Retry + 1);
					break;
				}
			}
		}

		return CaptureFromViewport(SceneViewport.Get(), TEXT("AssetEditor"), Extra);
	}

	FViewport* Viewport = GEditor->GetPIEViewport();
	FString ViewportType = TEXT("PIE");

	if (!Viewport)
	{
		Viewport = GEditor->GetActiveViewport();
		ViewportType = TEXT("Editor");
	}

	if (!Viewport)
	{
		return FMCPToolResult::Error(TEXT("No viewport available. Open a level or start PIE."));
	}

	TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
	ApplyCamera(Viewport, Params, Extra);

	return CaptureFromViewport(Viewport, ViewportType, Extra->Values.Num() > 0 ? Extra : nullptr);
}
