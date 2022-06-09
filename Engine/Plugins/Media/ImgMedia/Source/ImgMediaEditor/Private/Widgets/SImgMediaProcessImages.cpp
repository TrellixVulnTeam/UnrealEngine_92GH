// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SImgMediaProcessImages.h"

#include "Async/Async.h"
#include "Customizations/ImgMediaFilePathCustomization.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapperModule.h"
#include "IImgMediaModule.h"
#include "ImageUtils.h"
#include "ImageWrapperHelper.h"
#include "ImgMediaEditorModule.h"
#include "ImgMediaProcessImagesOptions.h"
#include "Math/UnrealMathUtility.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PropertyEditorModule.h"
#include "SlateOptMacros.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#if IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
#include "OpenExrWrapper.h"
#endif // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM

#define LOCTEXT_NAMESPACE "ImgMediaProcessImages"

SImgMediaProcessImages::~SImgMediaProcessImages()
{
	CleanUp();
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SImgMediaProcessImages::Construct(const FArguments& InArgs)
{
	// Set up widgets.
	TSharedPtr<SBox> DetailsViewBox;
	
	ChildSlot
	[
		SNew(SScrollBox)

		// Add details view.
		+ SScrollBox::Slot()
			[
				SAssignNew(DetailsViewBox, SBox)
			]
			
		// Add process images button.
		+ SScrollBox::Slot()
			.Padding(4.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(StartButton, SButton)
							.OnClicked(this, &SImgMediaProcessImages::OnProcessImagesClicked)
							.Text(LOCTEXT("StartProcessImages", "Process Images"))
							.ToolTipText(LOCTEXT("StartProcesssImagesButtonToolTip", "Start processing images."))
					]

				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(CancelButton, SButton)
							.OnClicked(this, &SImgMediaProcessImages::OnCancelClicked)
							.Text(LOCTEXT("CancelProcessImages", "Cancel"))
							.ToolTipText(LOCTEXT("CancelProcesssImagesButtonToolTip", "Cancel processing images."))
					]
			]
	];
	bIsProcessing = false;
	bIsCancelling = false;
	UpdateWidgets();

	// Create object with our options.
	Options = TStrongObjectPtr<UImgMediaProcessImagesOptions>(NewObject<UImgMediaProcessImagesOptions>(GetTransientPackage(), NAME_None));

	// Create detail view with our options.
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->RegisterInstancedCustomPropertyTypeLayout(FName(TEXT("FilePath")),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(FImgMediaFilePathCustomization::MakeInstance));
	DetailsView->SetObject(Options.Get());

	DetailsViewBox->SetContent(DetailsView->AsShared());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SImgMediaProcessImages::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bUsePlayer)
	{
		HandleProcessing();
	}
}

void SImgMediaProcessImages::UpdateWidgets()
{
	StartButton->SetEnabled(!bIsProcessing);
	CancelButton->SetEnabled(bIsProcessing && (!bIsCancelling));
}

FReply SImgMediaProcessImages::OnProcessImagesClicked()
{
	if (bIsProcessing == false)
	{
		// Set that we are processing now.
		bIsProcessing = true;
		bUsePlayer = Options->bUsePlayer;
		UpdateWidgets();

		// Create notification.
		FNotificationInfo Info(FText::GetEmpty());
		Info.bFireAndForget = false;

		ConfirmNotification = FSlateNotificationManager::Get().AddNotification(Info);

		// Are we using a player?
		if (bUsePlayer)
		{
			// Create player.
			MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage(), "MediaPlayer", RF_Transient);
			MediaPlayer->SetLooping(true);
			MediaPlayer->PlayOnOpen = true;
			MediaPlayer->AddToRoot();

			// Create texture.
			MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), "MediaTexture", RF_Transient);
			MediaTexture->SetMediaPlayer(MediaPlayer);
			MediaTexture->UpdateResource();
			MediaTexture->AddToRoot();

			// Create media source.
			MediaSource = UMediaSource::SpawnMediaSourceForString(Options->SequencePath.FilePath, GetTransientPackage());
			if (MediaSource == nullptr)
			{
				return FReply::Handled();
			}
			MediaSource->AddToRoot();

			// Start playing.
			CurrentFrameIndex = 0;
			CurrentTime = FTimespan::FromSeconds(0.0f);
			MediaPlayer->SetBlockOnTimeRange(TRange<FTimespan>(CurrentTime,
				CurrentTime + FTimespan::FromSeconds(1.0f / 100000.0f)));
			MediaPlayer->OpenSource(MediaSource);
		}
		else
		{
			// Start async task to process files.
			Async(EAsyncExecution::Thread, [this]()
			{
				ProcessAllImages();
			});
		}
	}

	return FReply::Handled();
}

FReply SImgMediaProcessImages::OnCancelClicked()
{
	if (bIsProcessing)
	{
		bIsCancelling = true;
		UpdateWidgets();
	}

	return FReply::Handled();
}

void SImgMediaProcessImages::ProcessAllImages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessAllImages);

	bool bUseCustomFormat = Options->bUseCustomFormat;
	int32 InTileWidth = Options->bEnableTiling ? Options->TileSizeX : 0;
	int32 InTileHeight = Options->bEnableTiling ? Options->TileSizeY : 0;
	int32 TileBorder = 0; // Note: virtual texture support is shelved for now.
	bool bEnableMips = Options->bEnableMipMapping;

	// Create output directory.
	FString OutPath = Options->OutputPath.Path;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutPath);

	// Get source files.
	FString SequencePath = FPaths::GetPath(Options->SequencePath.FilePath);
	
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *SequencePath, TEXT("*"));
	FoundFiles.Sort();
	UE_LOG(LogImgMediaEditor, Warning, TEXT("Found %i image files in %s to import."), FoundFiles.Num(), *SequencePath);
	if (FoundFiles.Num() == 0)
	{
		UE_LOG(LogImgMediaEditor, Error, TEXT("No files to import."));
	}
	else
	{
		// Create image wrapper
		FString Ext = FPaths::GetExtension(FoundFiles[0]);
		EImageFormat ImageFormat = ImageWrapperHelper::GetImageFormat(Ext);

		if (ImageFormat == EImageFormat::Invalid)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("Invalid file format %s"), *Ext);
		}
		else
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
			
			// ImageWrapper is always returning an alpha channel for RGB, so check if we really have one.
			bool bHasAlphaChannel = HasAlphaChannel(Ext, FPaths::Combine(SequencePath, FoundFiles[0]));

			// Get number of threads to use.
			int32 NumThreads = Options->NumThreads;
			if (NumThreads <= 0)
			{
				NumThreads = 8;
			}

			// Loop through all files.
			int NumDone = 0;
			int TotalNum = FoundFiles.Num();
			std::atomic<int32> NumActive = 0;
			TSharedPtr<SNotificationItem> LocalConfirmNotification = ConfirmNotification;
			for (const FString& FileName : FoundFiles)
			{
				// Wait for threads to finish if we have to many.
				while (NumActive >= NumThreads)
				{
					FPlatformProcess::Sleep(0.1f);
				}
				NumActive++;
				
				// Update notification with current status.
				Async(EAsyncExecution::TaskGraphMainThread, [LocalConfirmNotification, NumDone, TotalNum]()
				{
					if (LocalConfirmNotification.IsValid())
					{
						LocalConfirmNotification->SetText(
							FText::Format(LOCTEXT("ImgMediaCompleted", "ImgMedia Completed {0}/{1}"),
								FText::AsNumber(NumDone), FText::AsNumber(TotalNum)));
					}
				});
				NumDone++;

				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
				Async(EAsyncExecution::Thread, [this, SequencePath, FileName, OutPath,
					bUseCustomFormat, Ext, &NumActive,
					ImageWrapper, InTileWidth, InTileHeight,
					TileBorder, bEnableMips, bHasAlphaChannel]() mutable
				{
					FString FullFileName = FPaths::Combine(SequencePath, FileName);

					// Load image into buffer.
					TArray64<uint8> InputBuffer;
					if (!FFileHelper::LoadFileToArray(InputBuffer, *FullFileName))
					{
						UE_LOG(LogImgMediaEditor, Error, TEXT("Failed to load %s"), *FullFileName);
						NumActive--;
						return;
					}
					if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(InputBuffer.GetData(), InputBuffer.Num()))
					{
						UE_LOG(LogImgMediaEditor, Error, TEXT("Failed to create image wrapper for %s"), *FullFileName);
						NumActive--;
						return;
					}

					// Import this image.
					FString Name = FPaths::Combine(OutPath, FileName);
					if (bUseCustomFormat)
					{
						ProcessImageCustom(ImageWrapper, InTileWidth, InTileHeight, TileBorder,
							bEnableMips, bHasAlphaChannel, Name);

					}
					else
					{
						Name = FPaths::ChangeExtension(Name, TEXT(""));
						ProcessImage(ImageWrapper, InTileWidth, InTileHeight, Name, Ext);
					}
					NumActive--;
				});

				// Do we want to cancel?
				if (bIsCancelling)
				{
					break;
				}
			}

			// Wait for all our tasks to finish.
			while (NumActive > 0)
			{
				FPlatformProcess::Sleep(0.2f);
			}
		}
	}

	// Close notification. Must be run on the main thread.
	Async(EAsyncExecution::TaskGraphMainThread, [this]()
	{
		if (ConfirmNotification.IsValid())
		{
			ConfirmNotification->SetEnabled(false);
			ConfirmNotification->SetCompletionState(bIsCancelling ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
			ConfirmNotification->ExpireAndFadeout();
		}

		// Done with processing.
		bIsProcessing = false;
		bIsCancelling = false;
		UpdateWidgets();
	});
}

bool SImgMediaProcessImages::HasAlphaChannel(const FString& Ext, const FString& File)
{
	bool bHasAlpha = true;
	// We just support EXR at the moment.
	if (Ext == TEXT("exr"))
	{
		FRgbaInputFile InputFile(File);
		bHasAlpha = InputFile.GetNumChannels() == 4;
	}

	return bHasAlpha;
}

void SImgMediaProcessImages::ProcessImage(TSharedPtr<IImageWrapper>& InImageWrapper,
	int32 InTileWidth, int32 InTileHeight, const FString& InName, const FString& FileExtension)
{
	// Get image data.
	ERGBFormat Format = InImageWrapper->GetFormat();
	int32 Width = InImageWrapper->GetWidth();
	int32 Height = InImageWrapper->GetHeight();
	int32 BitDepth = InImageWrapper->GetBitDepth();
	TArray64<uint8> RawData;
	InImageWrapper->GetRaw(Format, BitDepth, RawData);

	int32 NumTilesX = InTileWidth > 0 ? Width / InTileWidth : 1;
	int32 NumTilesY = InTileHeight > 0 ? Height / InTileHeight : 1;
	int32 TileWidth = Width / NumTilesX;
	int32 TileHeight = Height / NumTilesY;
	int32 BytesPerPixel = RawData.Num() / (Width * Height);
	TArray64<uint8> TileRawData;
	TileRawData.AddZeroed(TileWidth * TileHeight * BytesPerPixel);
	bool bIsTiled = (NumTilesX > 1) || (NumTilesY > 1);

	// Create a directory if we have tiles.
	FString FileName;
	if (bIsTiled)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*InName);
		FileName = FPaths::Combine(InName, FPaths::GetCleanFilename(InName));;
	}
	else
	{
		FileName = InName;
	}

	// Loop over y tiles.
	for (int TileY = 0; TileY < NumTilesY; ++TileY)
	{
		// Loop over x tiles.
		for (int TileX = 0; TileX < NumTilesX; ++TileX)
		{
			// Copy tile line by line.
			uint8* DestPtr = TileRawData.GetData();
			uint8* SrcPtr = RawData.GetData() + TileX * TileWidth * BytesPerPixel +
				TileY * TileHeight * Width * BytesPerPixel;
			for (int LineY = 0; LineY < TileHeight; ++LineY)
			{
				FMemory::Memcpy(DestPtr, SrcPtr,
					TileWidth * BytesPerPixel);
				DestPtr += TileWidth * BytesPerPixel;
				SrcPtr += Width * BytesPerPixel;
			}

			// Compress data.
			InImageWrapper->SetRaw(TileRawData.GetData(), TileRawData.Num(),
				TileWidth, TileHeight, Format, BitDepth);
			const TArray64<uint8> CompressedData = InImageWrapper->GetCompressed((int32)EImageCompressionQuality::Uncompressed);

			// Write out tile.
			FString Name = FString::Format(TEXT("{0}_x{1}_y{2}.{3}"),
				{*FileName, TileX, TileY, *FileExtension});
			FFileHelper::SaveArrayToFile(CompressedData, *Name);
		}
	}
}

void SImgMediaProcessImages::ProcessImageCustom(TSharedPtr<IImageWrapper>& InImageWrapper,
	int32 InTileWidth, int32 InTileHeight, int32 InTileBorder, bool bInEnableMips,
	bool bHasAlphaChannel, const FString& InName)
{
#if IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustom);
	// Get image data.
	ERGBFormat Format = InImageWrapper->GetFormat();
	int32 Width = InImageWrapper->GetWidth();
	int32 Height = InImageWrapper->GetHeight();
	int32 BitDepth = InImageWrapper->GetBitDepth();
	TArray64<uint8> RawData;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustom:GetRaw);
		InImageWrapper->GetRaw(Format, BitDepth, RawData);
	}
	ProcessImageCustomRawData(RawData, Width, Height, BitDepth,
		InTileWidth, InTileHeight, InTileBorder, bInEnableMips,
		bHasAlphaChannel, InName);
#else // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	UE_LOG(LogImgMediaEditor, Error, TEXT("EXR not supported on this platform."));
#endif // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
}

void SImgMediaProcessImages::ProcessImageCustomRawData(TArray64<uint8>& RawData,
	int32 Width, int32 Height, int32 BitDepth,
	int32 InTileWidth, int32 InTileHeight, int32 InTileBorder, bool bInEnableMips,
	bool bHasAlphaChannel, const FString& InName)
{
#if IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustomRawData);
	int32 DestWidth = Width;
	int32 DestHeight = Height;
	int32 NumTilesX = InTileWidth > 0 ? (Width + InTileWidth - 1) / InTileWidth : 1;
	int32 NumTilesY = InTileHeight > 0 ? (Height + InTileHeight - 1) / InTileHeight : 1;
	int32 TileWidth = InTileWidth;
	int32 TileHeight = InTileHeight;
	int32 BytesPerPixel = RawData.Num() / (Width * Height);
	int32 BytesPerPixelPerChannel = BitDepth / 8;
	int32 NumChannels = BytesPerPixel / BytesPerPixelPerChannel;
	int32 DestNumChannels = NumChannels;
	// ImageWrapper always returns an alpha channel, so make sure we really have one.
	if ((DestNumChannels == 4) && (bHasAlphaChannel == false))
	{
		// Remove the alpha channel as its not needed.
		RemoveAlphaChannel(RawData);
		NumChannels = 3;
		DestNumChannels = 3;
		BytesPerPixel = BytesPerPixelPerChannel * NumChannels;
	}

	TArray64<uint8> TileBuffer;
	TArray64<uint8> TintBuffer;
	bool bIsTiled = (NumTilesX > 1) || (NumTilesY > 1);
	if (bIsTiled)
	{
		// Take border into account.
		DestWidth = Width + InTileBorder * 2 * NumTilesX;
		DestHeight = Height + InTileBorder * 2 * NumTilesY;
	}

	uint8* RawDataPtr = RawData.GetData();

	// Names for our channels.
	const FString RChannelName = FString(TEXT("R"));
	const FString GChannelName = FString(TEXT("G"));
	const FString BChannelName = FString(TEXT("B"));
	const FString AChannelName = FString(TEXT("A"));

	FIntPoint Stride(2, 0);

	// Create tiled exr file.
	FTiledOutputFile OutFile(FIntPoint(0, 0), FIntPoint(DestWidth - 1, DestHeight - 1),
		FIntPoint(0, 0), FIntPoint(DestWidth - 1, DestHeight - 1));

	// Add attributes.
	OutFile.AddIntAttribute(IImgMediaModule::CustomFormatAttributeName.Resolve().ToString(), 1);

	// These attributes will not be added and therefore not found by EXR reader if it is not tiled.
	if (bIsTiled)
	{
		OutFile.AddIntAttribute(IImgMediaModule::CustomFormatTileWidthAttributeName.Resolve().ToString(), TileWidth);
		OutFile.AddIntAttribute(IImgMediaModule::CustomFormatTileHeightAttributeName.Resolve().ToString(),TileHeight);
		OutFile.AddIntAttribute(IImgMediaModule::CustomFormatTileBorderAttributeName.Resolve().ToString(), InTileBorder);
	}

	// Add channels.
	if (DestNumChannels == 4)
	{
		OutFile.AddChannel(AChannelName);
	}
	if (DestNumChannels >= 3)
	{
		OutFile.AddChannel(BChannelName);
		OutFile.AddChannel(GChannelName);
		OutFile.AddChannel(RChannelName);
	}

	// Create output.
	OutFile.CreateOutputFile(InName, DestWidth, DestHeight, bInEnableMips, 1);
	if (DestNumChannels == 4)
	{
		OutFile.AddFrameBufferChannel(AChannelName, nullptr, Stride);
	}
	if (DestNumChannels >= 3)
	{
		OutFile.AddFrameBufferChannel(BChannelName, nullptr, Stride);
		OutFile.AddFrameBufferChannel(GChannelName, nullptr, Stride);
		OutFile.AddFrameBufferChannel(RChannelName, nullptr, Stride);
	}

	// Flip between 2 buffers making mips.
	TArray64<uint8> RawData2;
	uint8* MipBuffer[2];
	MipBuffer[0] = RawDataPtr;
	MipBuffer[1] = nullptr;
	int32 CurrentMipBufferIndex = 0;

	// Loop over each mip level.
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustom:CreateMips);
	int32 NumMips = OutFile.GetNumberOfMipLevels();
	int32 MipSourceWidth = Width;
	int32 MipSourceHeight = Height;
	for (int32 MipLevel = 0; MipLevel < NumMips; MipLevel++)
	{
		int32 MipWidth = OutFile.GetMipWidth(MipLevel);
		int32 MipHeight = OutFile.GetMipHeight(MipLevel);
		uint8* CurrentBuffer = MipBuffer[CurrentMipBufferIndex];
		uint8* LastBuffer = MipBuffer[CurrentMipBufferIndex ^ 1];

		// Allocate space for the other buffer.
		if (CurrentBuffer == nullptr)
		{
			RawData2.AddUninitialized(MipWidth* MipHeight* BytesPerPixel);
			MipBuffer[CurrentMipBufferIndex] = RawData2.GetData();
			CurrentBuffer = MipBuffer[CurrentMipBufferIndex];
		}

		// Generate mip data.
		if (MipLevel != 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustom:GenerateMipData);
			int32 SourceStrideX = NumChannels;
			int32 SourceStrideY = MipWidth * NumChannels * 2;
			for (int32 PixelY = 0; PixelY < MipHeight; ++PixelY)
			{
				for (int32 PixelX = 0; PixelX < MipWidth; ++PixelX)
				{
					int32 PixelOffset = (PixelX + PixelY * MipWidth) * NumChannels;
					for (int32 Channel = 0; Channel < NumChannels; ++Channel)
					{
						// Box filter.
						int32 SourceWidth = MipWidth * 2;
						int32 SourcePixelOffset = (PixelX + PixelY * SourceWidth) * NumChannels * 2 +
							Channel;
						FFloat16 SourcePixels[4];
						SourcePixels[0] =
							((FFloat16*)LastBuffer)[SourcePixelOffset];
						SourcePixels[1] =
							((FFloat16*)LastBuffer)[SourcePixelOffset + SourceStrideX];
						SourcePixels[2] =
							((FFloat16*)LastBuffer)[SourcePixelOffset + SourceStrideY];
						SourcePixels[3] =
							((FFloat16*)LastBuffer)[SourcePixelOffset + SourceStrideX + SourceStrideY];

						((FFloat16*)CurrentBuffer)[PixelOffset + Channel] =
							(SourcePixels[0] + SourcePixels[1] + SourcePixels[2] + SourcePixels[3]) * 0.25f;
					}
				}
			}
		}

		// Tint mip levels?
		if (Options->bEnableMipLevelTint)
		{
			TintData(CurrentBuffer, TintBuffer,
				MipLevel, MipWidth, MipHeight,
				NumChannels);
			CurrentBuffer = TintBuffer.GetData();
		}

		// Do we need to tile this mip?
		// Need to also check that this is actually a valid mip level.
		if ((bIsTiled) && (MipSourceWidth > 0) && (MipSourceHeight > 0))
		{
			int32 MipTileWidth = TileWidth;
			int32 MipTileHeight = TileHeight;

			// A tile could be larger than the mip level when dealing with mips.
			if (MipTileWidth > MipSourceWidth)
			{
				MipTileWidth = MipSourceWidth;
			}
			if (MipTileHeight > MipSourceHeight)
			{
				MipTileHeight = MipSourceHeight;
			}

			int32 OutputWidth = 0;
			int32 OutputHeight = 0;
			int32 MipNumTilesX = (MipSourceWidth + MipTileWidth - 1) / MipTileWidth;
			int32 MipNumTilesY = (MipSourceHeight + MipTileHeight - 1) / MipTileHeight;

			// Make sure our sizes match the mip size we get from EXR.
			int32 ExpectedMipWidth = MipSourceWidth + MipNumTilesX * InTileBorder * 2;
			if (ExpectedMipWidth != MipWidth)
			{
				UE_LOG(LogImgMediaEditor, Error,
					TEXT("Expected mip level width of %d, but got %d (SourceWidth:%d NumTiles:%d TileBorder:%d"),
					ExpectedMipWidth, MipHeight,
					MipSourceWidth, MipNumTilesX, InTileBorder);
			}
			int32 ExpectedMipHeight = MipSourceHeight + MipNumTilesY * InTileBorder * 2;
			if (ExpectedMipHeight != MipHeight)
			{
				UE_LOG(LogImgMediaEditor, Error,
					TEXT("Expected mip level height of %d, but got %d (SourceHeight:%d NumTiles:%d TileBorder:%d"),
					ExpectedMipHeight, MipHeight,
					MipSourceHeight, MipNumTilesY, InTileBorder);
			}

			// Tile the buffer.
			TileData(CurrentBuffer, TileBuffer,
				MipSourceWidth, MipSourceHeight, MipWidth, MipHeight,
				MipNumTilesX, MipNumTilesY,
				MipTileWidth, MipTileHeight, InTileBorder,
				BytesPerPixel);
			CurrentBuffer = TileBuffer.GetData();
		}

		// Write to EXR.
		TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::ProcessImageCustom:WriteEXR);
		Stride.Y = MipWidth * BytesPerPixel;
		int64 BufferOffset = 0;
		int64 SingleBufferOffset = MipWidth * BytesPerPixelPerChannel;
		if (DestNumChannels == 4)
		{
			OutFile.UpdateFrameBufferChannel(AChannelName, CurrentBuffer, Stride);
			BufferOffset += SingleBufferOffset;
		}

		OutFile.UpdateFrameBufferChannel(BChannelName, CurrentBuffer + BufferOffset, Stride);
		BufferOffset += SingleBufferOffset;
		OutFile.UpdateFrameBufferChannel(GChannelName, CurrentBuffer + BufferOffset, Stride);
		BufferOffset += SingleBufferOffset;
		OutFile.UpdateFrameBufferChannel(RChannelName, CurrentBuffer + BufferOffset, Stride);
		BufferOffset += SingleBufferOffset;

		OutFile.SetFrameBuffer();

		OutFile.WriteTile(0, 0, MipLevel);

		// Switch buffers.
		CurrentMipBufferIndex ^= 1;
		MipSourceHeight /= 2;
		MipSourceWidth /= 2;
	}

#else // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	UE_LOG(LogImgMediaEditor, Error, TEXT("EXR not supported on this platform."));
#endif // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
}

void SImgMediaProcessImages::RemoveAlphaChannel(TArray64<uint8>& Buffer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::RemoveAlphaChannel);

	int32 BytesPerPixelPerChannel = 2;
	int64 BufferSize = Buffer.Num() / BytesPerPixelPerChannel;
	uint16* BufferPtr = (uint16*)(Buffer.GetData());
	
	// Loop through the buffer.
	int64 OutIndex = 0;
	for (int64 Index = 0; Index < BufferSize; ++Index)
	{
		// Skip every fourth channel (i.e. the alpha channel).
		if ((Index & 0x3) != 3)
		{
			// Copy the data in place.
			BufferPtr[OutIndex] = BufferPtr[Index];
			OutIndex++;
		}
	}

	// Don't bother shrinking as its just a waste and extra work.
	Buffer.SetNum((BufferSize * 3) / 4, false);
}

void SImgMediaProcessImages::TintData(uint8* SourceData, TArray64<uint8>& DestArray,
	int32 InMipLevel, int32 InWidth, int32 InHeight, int32 InNumChannels)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::TintData);

	// Get tint colour.
	FLinearColor TintColor = FLinearColor::White;
	if (Options->MipLevelTints.Num() > 0)
	{
		TintColor = Options->MipLevelTints[InMipLevel % Options->MipLevelTints.Num()];
	}

	// Set up destination buffer.
	DestArray.Reset();
	DestArray.AddUninitialized(InWidth * InHeight * InNumChannels * 2);
	FFloat16* Buffer = (FFloat16*)SourceData;
	FFloat16* OutBuffer = (FFloat16*)DestArray.GetData();
		
	// Tint buffer.
	for (int32 PixelY = 0; PixelY < InHeight; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < InWidth; ++PixelX)
		{
			OutBuffer[0] = (Buffer[0].GetFloat() + TintColor.R) * 0.5f;
			OutBuffer[1] = (Buffer[1].GetFloat() + TintColor.G) * 0.5f;
			OutBuffer[2] = (Buffer[2].GetFloat() + TintColor.B) * 0.5f;
			if (InNumChannels == 4)
			{
				OutBuffer[3] = Buffer[3];
			}
				
			Buffer += InNumChannels;
			OutBuffer += InNumChannels;
		}
	}
}

void SImgMediaProcessImages::TileData(uint8* SourceData, TArray64<uint8>& DestArray,
	int32 SourceWidth, int32 SourceHeight, int32 DestWidth, int32 DestHeight,
	int32 NumTilesX, int32 NumTilesY,
	int32 TileWidth, int32 TileHeight, int32 InTileBorder,
	int32 BytesPerPixel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::TileData);

	// We don't support tile borders larger than a tile size,
	// but this shuld not happen in practice.
	if ((InTileBorder > TileWidth) || (InTileBorder > TileHeight))
	{
		UE_LOG(LogImgMediaEditor, Error, TEXT("Tile border is larger than tile size. Clamping to tile size."));
		InTileBorder = FMath::Min(TileWidth, TileHeight);
	}

	// Set up destination buffer.
	DestArray.Reset();
	DestArray.AddUninitialized(DestWidth * DestHeight * BytesPerPixel);

	uint8* DestData = DestArray.GetData();
	int32 DestTileWidth = TileWidth + InTileBorder * 2;
	int32 DestTileHeight = TileHeight + InTileBorder * 2;

	// Make sure our output tile size is not bigger than the output size.
	if ((DestTileWidth > DestWidth) || (DestTileHeight > DestHeight))
	{
		// This is not a valid mip level, so just ignore.
		return;
	}

	int32 BytesPerTile = TileWidth * TileHeight * BytesPerPixel;
	int32 ByterPerDestTile = DestTileWidth * DestTileHeight * BytesPerPixel;
	uint8* DestTile = DestData;

	// Loop over y tiles.
	for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
	{
		// Loop over x tiles.
		for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
		{
			// Get address of the source and destination tiles.
			uint8* SourceTile = SourceData +
				(TileX * TileWidth + TileY * SourceWidth * TileHeight) * BytesPerPixel;
			
			// If this tile is over the right edge of our image, then make this tile smaller
			// so it does not exceed the image size.
			int32 NumberOfPixelsToCopy = TileWidth;
			int32 ThisDestTileWidth = DestTileWidth;
			if ((TileX + 1) * TileWidth > SourceWidth)
			{
				NumberOfPixelsToCopy = SourceWidth - TileX * TileWidth;
				ThisDestTileWidth -= TileWidth - NumberOfPixelsToCopy;
			}

			// Create a left border.
			int32 DestTileOffset = 0;
			if (TileX > 0)
			{
				NumberOfPixelsToCopy += InTileBorder;
				// Offset the source to get the extra pixels.
				SourceTile -= InTileBorder * BytesPerPixel;
			}
			else
			{
				// Offset the destination as we are skipping this border as we have no data.
				DestTileOffset = InTileBorder * BytesPerPixel;
				DestTile += DestTileOffset;
			}

			// Create a right border.
			if (TileX < NumTilesX - 1)
			{
				NumberOfPixelsToCopy += InTileBorder;
			}

			// If this tile is over the bottom edge of our image, then make this tile smaller
			// so it does not exceed the image size.
			int32 ThisDestTileHeight = DestTileHeight;
			if ((TileY + 1) * TileHeight > SourceHeight)
			{
				ThisDestTileHeight = SourceHeight - TileY * TileHeight;
			}

			// Loop over each row in the tile.
			for (int32 Row = 0; Row < ThisDestTileHeight; ++Row)
			{
				// Make sure we don't go beyond the source data.
				int32 SourceRow = Row - InTileBorder;
				if (TileY == 0)
				{
					SourceRow = FMath::Max(SourceRow, 0);
				}
				if (TileY == NumTilesY - 1)
				{
					SourceRow = FMath::Min(SourceRow, TileHeight - 1);
				}

				uint8* SourceLine = SourceTile + SourceRow * SourceWidth * BytesPerPixel;
				uint8* DestLine = DestTile;

				// Copy the main data.
				FMemory::Memcpy(DestLine, SourceLine, NumberOfPixelsToCopy * BytesPerPixel);

				// Increment our pointer to the next tile.
				// We have to remove any DestTileOffset we applied earlier.
				DestTile += ThisDestTileWidth * BytesPerPixel - DestTileOffset;
			}
		}
	}
}

void SImgMediaProcessImages::HandleProcessing()
{
	// Are we processing?
	if (bIsProcessing)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SImgMediaProcessImages::HandleProcessing);
		// We did not cancel yet?
		bool bShouldExit = false;
		if ((MediaPlayer != nullptr) && (bIsCancelling == false))
		{
			// Get which frame the player is on.
			int32 PlayerFrame = 0;
			if (FrameDuration.GetTotalSeconds() > 0.0f)
			{
				PlayerFrame = (int32)FMath::RoundHalfToEven(MediaPlayer->GetTime().GetTotalSeconds() / FrameDuration.GetTotalSeconds());
			}

			UE_LOG(LogImgMediaEditor, Verbose,
				TEXT("ProcessImages Time:%f PlayerTime:%f Duration:%f Frame:%d"),
				CurrentTime.GetTotalSeconds(),
				MediaPlayer->GetTime().GetTotalSeconds(),
				MediaPlayer->GetDuration().GetTotalSeconds(), PlayerFrame);

			// Has the player stopped playing?
			if (MediaPlayer->IsClosed())
			{
				bShouldExit = true;
			}
			// Is this the frame we want?
			else if ((MediaPlayer->IsPreparing() == false) && (CurrentFrameIndex == PlayerFrame))
			{
				// Are we set up yet?
				if (RenderTarget == nullptr)
				{
					CreateRenderTarget();
					// Get frame duration.
					float FrameRate = MediaPlayer->GetVideoTrackFrameRate(INDEX_NONE, INDEX_NONE);
					if (FrameRate <= 0.0f)
					{
						FrameRate = 24.0f;
					}
					FrameDuration = FTimespan::FromSeconds(1.0f / FrameRate);
				}

				// Copy media texture to our render target.
				DrawTextureToRenderTarget();

				// Process this render.
				TArray64<uint8> RawData;
				bool bReadSuccess = FImageUtils::GetRawData(RenderTarget, RawData);
				if (bReadSuccess)
				{
					int32 Width = RenderTarget->GetSurfaceWidth();
					int32 Height = RenderTarget->GetSurfaceHeight();
					int32 BitDepth = 16;
					bool bUseCustomFormat = Options->bUseCustomFormat;
					int32 InTileWidth = Options->bEnableTiling ? Options->TileSizeX : 0;
					int32 InTileHeight = Options->bEnableTiling ? Options->TileSizeY : 0;
					int32 TileBorder = 0; // Note: virtual texture support is shelved for now.
					bool bEnableMips = Options->bEnableMipMapping;
					bool bHasAlphaChannel = false;
					FString OutPath = Options->OutputPath.Path;
					FString FileName = FString::Printf(TEXT("image%05d.exr"), CurrentFrameIndex);
					FString Name = FPaths::Combine(OutPath, FileName);

					Async(EAsyncExecution::Thread, [this, RawData = MoveTemp(RawData), Width, Height, BitDepth,
						InTileWidth, InTileHeight, TileBorder, bEnableMips,
						bHasAlphaChannel, Name]() mutable
					{
						ProcessImageCustomRawData(RawData, Width, Height, BitDepth,
							InTileWidth, InTileHeight, TileBorder, bEnableMips,
							bHasAlphaChannel, Name);
					});
				}
				else
				{
					UE_LOG(LogImgMediaEditor, Error, TEXT("ProcessImages failed to get raw data."));
				}

				// Update notification.
				if (ConfirmNotification.IsValid())
				{
					ConfirmNotification->SetText(
						FText::Format(LOCTEXT("ImgMediaCompleted2", "ImgMedia Completed {0}"),
							FText::AsNumber(CurrentFrameIndex)));
				}

				// Next frame.
				CurrentTime += FrameDuration;
				CurrentFrameIndex++;
				if (CurrentTime >= MediaPlayer->GetDuration())
				{
					bShouldExit = true;
				}
				else
				{
					MediaPlayer->SetBlockOnTimeRange(TRange<FTimespan>(CurrentTime, CurrentTime + FrameDuration));
				}
			}
		}
		else
		{
			bShouldExit = true;
		}

		// Are we done?
		if (bShouldExit)
		{
			// Remove notification.
			if (ConfirmNotification.IsValid())
			{
				ConfirmNotification->SetEnabled(false);
				ConfirmNotification->SetCompletionState(bIsCancelling ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
				ConfirmNotification->ExpireAndFadeout();
				ConfirmNotification.Reset();
			}

			// Clean up.
			bIsCancelling = false;
			bIsProcessing = false;
			CleanUp();
			UpdateWidgets();
		}
	}
}

void SImgMediaProcessImages::CreateRenderTarget()
{
	if (MediaTexture != nullptr)
	{
		int32 Width = MediaTexture->GetWidth();
		int32 Height = MediaTexture->GetHeight();

		RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("ImgMediaProcessImages"));
		RenderTarget->RenderTargetFormat = RTF_RGBA16f;
		RenderTarget->InitAutoFormat(Width, Height);
		RenderTarget->AddToRoot();
		RenderTarget->UpdateResourceImmediate(true);
	}
}

void SImgMediaProcessImages::DrawTextureToRenderTarget()
{
	UWorld* World = nullptr;
	World = GEditor->GetEditorWorldContext().World();
	World->FlushDeferredParameterCollectionInstanceUpdates();

	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

	UCanvas* Canvas = World->GetCanvasForDrawMaterialToRenderTarget();
	FCanvas RenderCanvas(
		RenderTargetResource,
		nullptr,
		World,
		World->FeatureLevel);
	Canvas->Init(RenderTarget->SizeX, RenderTarget->SizeY, nullptr, &RenderCanvas);
	Canvas->Update();

	{
		ENQUEUE_RENDER_COMMAND(FlushDeferredResourceUpdateCommand)(
			[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
		{
			RenderTargetResource->FlushDeferredResourceUpdate(RHICmdList);
		});

		Canvas->K2_DrawTexture(MediaTexture, FVector2D(0, 0), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), FVector2D(0, 0));

		RenderCanvas.Flush_GameThread();
		Canvas->Canvas = nullptr;
		RenderTarget->UpdateResourceImmediate(false);
	}
}

void SImgMediaProcessImages::CleanUp()
{
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
		MediaPlayer->RemoveFromRoot();
		MediaPlayer = nullptr;
	}
	if (MediaTexture != nullptr)
	{
		MediaTexture->RemoveFromRoot();
		MediaTexture = nullptr;
	}
	if (MediaSource != nullptr)
	{
		MediaSource->RemoveFromRoot();
		MediaSource = nullptr;
	}
	if (RenderTarget != nullptr)
	{
		RenderTarget->RemoveFromRoot();
		RenderTarget = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
