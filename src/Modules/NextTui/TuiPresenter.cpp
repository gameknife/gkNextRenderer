#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/TuiPresenter.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Modules/NextTui/NextTuiModule.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Runtime::Tui
{
    struct TuiPresenter::FReadbackState
    {
        enum class ESlotState
        {
            Free,
            Recorded,
            Ready
        };

        struct FSlot
        {
            // Keep memory before its resource so destruction releases the resource first.
            std::unique_ptr<Vulkan::DeviceMemory> ImageMemory;
            std::unique_ptr<Vulkan::Image> Image;
            std::unique_ptr<Vulkan::DeviceMemory> StagingMemory;
            std::unique_ptr<Vulkan::Buffer> StagingBuffer;
            VkExtent2D Extent{};
            bool ImageInitialized = false;
            uint64_t SubmitSerial = 0;
            uint64_t Serial = 0;
            ESlotState State = ESlotState::Free;
        };

        std::vector<FSlot> Slots{};
        uint64_t NextSerial = 1;
        uint64_t DroppedReadyFrames = 0;
        uint64_t BlockedCaptureFrames = 0;
        bool CaptureRequested = true;
    };

    namespace
    {
        using namespace std::chrono_literals;

        constexpr auto TuiResizePollInterval = 200ms;
        constexpr auto TuiResizeDebounce = 150ms;
        constexpr uint32_t TuiStatusFramesScreenshotBusy = 90;
        constexpr uint32_t TuiStatusFramesResize = 120;
        constexpr uint32_t TuiStatusFramesScreenshotSaved = 180;
        constexpr uint32_t TuiMinColumns = 10;
        constexpr uint32_t TuiMinRows = 2;

        SDL_WindowID ResolveWindowId(NextEngine& engine)
        {
            SDL_Window* window = engine.GetWindow().Handle();
            return window ? SDL_GetWindowID(window) : 0;
        }
    }

    TuiPresenter::TuiPresenter(NextEngine& engine, const Runtime::Config::Options& options)
        : engine_(engine)
        , options_(options)
        , blitter_({
            .MaxColumns = options.TuiMaxCols,
            .MaxRows = options.TuiMaxRows,
            .CellMode = options.TuiCellMode == "quadrant"
                ? ECellMode::Quadrant
                : ECellMode::HalfBlock,
        })
        , terminal_(!options.TuiNoInput)
        , readback_(std::make_unique<FReadbackState>())
    {
    }

    TuiPresenter::~TuiPresenter() = default;

    bool TuiPresenter::Start()
    {
        if (!terminal_.Start())
        {
            return false;
        }

        lastPresentTime_ = std::chrono::steady_clock::now();
        resizeState_.LastTerminalSizePollTime = lastPresentTime_ - TuiResizePollInterval;
        resizeState_.StableSince = lastPresentTime_;
        readback_->CaptureRequested = true;
        logCapture_ = Modules::NextTui::CreateProcessLogCapture();
        return logCapture_ != nullptr;
    }

    void TuiPresenter::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                   Vulkan::VulkanBaseRenderer& renderer)
    {
        if (!renderer.HasSwapChain())
        {
            return;
        }

        if (readback_->Slots.empty() && !EnsureReadbackSlots())
        {
            return;
        }

        HarvestCompletedReadbacks(renderer.CompletedSubmitSerial());

        bool hasReadySlot = false;
        bool hasRecordedSlot = false;
        for (const auto& slot : readback_->Slots)
        {
            hasReadySlot = hasReadySlot || slot.State == FReadbackState::ESlotState::Ready;
            hasRecordedSlot = hasRecordedSlot || slot.State == FReadbackState::ESlotState::Recorded;
        }
        if (!readback_->CaptureRequested && (!hasReadySlot || hasRecordedSlot))
        {
            return;
        }

        FReadbackState::FSlot* freeSlot = nullptr;
        for (auto& slot : readback_->Slots)
        {
            if (slot.State == FReadbackState::ESlotState::Free)
            {
                freeSlot = &slot;
                break;
            }
        }
        if (!freeSlot)
        {
            readback_->BlockedCaptureFrames++;
            return;
        }

        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        const VkExtent2D sourceExtent = swapChain.Extent();
        const FRenderTargetSize outputSize = ComputeOutputSize(terminal_.GetSize());
        if (sourceExtent.width == 0 || sourceExtent.height == 0 ||
            outputSize.Width == 0 || outputSize.Height == 0)
        {
            return;
        }
        if (!swapChain.SupportsUsage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        {
            return;
        }

        const VkImage swapImage = swapChain.Images()[imageIndex];
        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, 0, VK_ACCESS_TRANSFER_READ_BIT,
                                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Vulkan::ImageMemoryBarrier::FullInsert(
            commandBuffer, freeSlot->Image->Handle(),
            freeSlot->ImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            freeSlot->ImageInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit blitRegion{};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {
            static_cast<int32_t>(sourceExtent.width),
            static_cast<int32_t>(sourceExtent.height),
            1};
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {
            static_cast<int32_t>(freeSlot->Extent.width),
            static_cast<int32_t>(freeSlot->Extent.height),
            1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        const bool sameExtent = sourceExtent.width == freeSlot->Extent.width &&
            sourceExtent.height == freeSlot->Extent.height;
        if (sameExtent)
        {
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {sourceExtent.width, sourceExtent.height, 1};
            vkCmdCopyImage(commandBuffer,
                           swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           freeSlot->Image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);
        }
        else
        {
            vkCmdBlitImage(commandBuffer,
                           swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           freeSlot->Image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blitRegion, VK_FILTER_LINEAR);
        }

        Vulkan::ImageMemoryBarrier::FullInsert(
            commandBuffer, freeSlot->Image->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy bufferCopyRegion{};
        bufferCopyRegion.bufferOffset = 0;
        bufferCopyRegion.bufferRowLength = 0;
        bufferCopyRegion.bufferImageHeight = 0;
        bufferCopyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bufferCopyRegion.imageOffset = {0, 0, 0};
        bufferCopyRegion.imageExtent = {freeSlot->Extent.width, freeSlot->Extent.height, 1};
        vkCmdCopyImageToBuffer(
            commandBuffer, freeSlot->Image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            freeSlot->StagingBuffer->Handle(), 1, &bufferCopyRegion);
        Vulkan::BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            freeSlot->StagingBuffer->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);

        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, VK_ACCESS_TRANSFER_READ_BIT, 0,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        freeSlot->ImageInitialized = true;
        freeSlot->SubmitSerial = renderer.RecordingSubmitSerial();
        freeSlot->Serial = readback_->NextSerial++;
        freeSlot->State = FReadbackState::ESlotState::Recorded;
        readback_->CaptureRequested = false;
    }

    void TuiPresenter::Tick()
    {
        const auto now = std::chrono::steady_clock::now();
        FlushQueuedKeyReleases();
        HandleInput();
        MaybeHandleTerminalResize(now);

        const uint32_t fps = std::max(1u, options_.TuiFps);
        const auto minInterval = std::chrono::milliseconds(1000 / fps);
        const bool frameDue = engine_.GetTotalFrames() == 0 || now - lastPresentTime_ >= minInterval;
        if (!frameDue)
        {
            return;
        }

        std::vector<FRgb8> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        if (TryConsumeCapturedFrame(pixels, width, height))
        {
            const FTerminalSize terminalSize = terminal_.GetSize();
            const std::string output = blitter_.EncodeFrame(
                pixels, width, height, terminalSize.Columns, terminalSize.Rows,
                BuildStatusLine(terminalSize.Columns, terminalSize.Rows));
            if (!output.empty())
            {
                terminal_.Write(output);
                lastPresentTime_ = now;
            }
        }

        if (!HasPendingCapture())
        {
            readback_->CaptureRequested = true;
        }
    }

    void TuiPresenter::OnRendererDeleteSwapChain()
    {
        blitter_.Reset();
        readback_->Slots.clear();
        readback_->CaptureRequested = true;
        inputState_.LastPointerPosition.reset();
    }

    bool TuiPresenter::EnsureReadbackSlots()
    {
        Vulkan::VulkanBaseRenderer& renderer = engine_.GetRenderer();
        if (!renderer.HasSwapChain())
        {
            return false;
        }

        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        const VkExtent2D sourceExtent = swapChain.Extent();
        const FRenderTargetSize outputSize = ComputeOutputSize(terminal_.GetSize());
        if (sourceExtent.width == 0 || sourceExtent.height == 0 ||
            outputSize.Width == 0 || outputSize.Height == 0)
        {
            return false;
        }

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(
            renderer.Device().PhysicalDevice(), swapChain.Format(), &formatProperties);
        const bool optimalTilingSupportsTransferSrc =
            (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
        const bool optimalTilingSupportsTransferDst =
            (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
        if (!optimalTilingSupportsTransferSrc || !optimalTilingSupportsTransferDst)
        {
            SPDLOG_WARN("TUI GPU readback is unavailable for swapchain format {} with optimal tiling",
                        static_cast<int>(swapChain.Format()));
            return false;
        }
        const bool canGpuDownscale =
            swapChain.SupportsUsage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
            (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0 &&
            (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
        const VkExtent2D readbackExtent = canGpuDownscale
            ? VkExtent2D{outputSize.Width, outputSize.Height}
            : sourceExtent;
        if (!canGpuDownscale &&
            (sourceExtent.width != outputSize.Width || sourceExtent.height != outputSize.Height))
        {
            SPDLOG_WARN("TUI SSAA GPU readback downscale is unavailable for swapchain format {}; falling back to CPU downscale",
                        static_cast<int>(swapChain.Format()));
        }

        const size_t slotCount = std::max<size_t>(swapChain.Images().size(), 2);
        readback_->Slots.reserve(slotCount);
        const VkDeviceSize stagingSize = static_cast<VkDeviceSize>(readbackExtent.width) *
            static_cast<VkDeviceSize>(readbackExtent.height) * 4;
        for (size_t i = 0; i < slotCount; ++i)
        {
            auto image = std::make_unique<Vulkan::Image>(
                renderer.Device(), readbackExtent, 1, swapChain.Format(),
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            auto imageMemory = std::make_unique<Vulkan::DeviceMemory>(
                image->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
            auto stagingBuffer = std::make_unique<Vulkan::Buffer>(
                renderer.Device(), static_cast<size_t>(stagingSize), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            auto stagingMemory = std::make_unique<Vulkan::DeviceMemory>(
                stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

            auto& slot = readback_->Slots.emplace_back();
            slot.ImageMemory = std::move(imageMemory);
            slot.Image = std::move(image);
            slot.StagingMemory = std::move(stagingMemory);
            slot.StagingBuffer = std::move(stagingBuffer);
            slot.Extent = readbackExtent;
        }
        return true;
    }

    void TuiPresenter::HarvestCompletedReadbacks(const uint64_t completedSubmitSerial)
    {
        for (auto& slot : readback_->Slots)
        {
            if (slot.State == FReadbackState::ESlotState::Recorded && slot.SubmitSerial <= completedSubmitSerial)
            {
                slot.State = FReadbackState::ESlotState::Ready;
            }
        }
    }

    bool TuiPresenter::TryConsumeCapturedFrame(std::vector<FRgb8>& pixels, uint32_t& width, uint32_t& height)
    {
        FReadbackState::FSlot* selectedSlot = nullptr;
        for (auto& slot : readback_->Slots)
        {
            if (slot.State != FReadbackState::ESlotState::Ready)
            {
                continue;
            }
            if (!selectedSlot || slot.Serial > selectedSlot->Serial)
            {
                selectedSlot = &slot;
            }
        }
        if (!selectedSlot)
        {
            return false;
        }
        for (auto& slot : readback_->Slots)
        {
            if (&slot != selectedSlot && slot.State == FReadbackState::ESlotState::Ready)
            {
                readback_->DroppedReadyFrames++;
                slot.State = FReadbackState::ESlotState::Free;
            }
        }

        width = selectedSlot->Extent.width;
        height = selectedSlot->Extent.height;
        if (width == 0 || height == 0 || !selectedSlot->StagingBuffer || !selectedSlot->StagingMemory)
        {
            selectedSlot->State = FReadbackState::ESlotState::Free;
            return false;
        }

        uint8_t* imageData = static_cast<uint8_t*>(selectedSlot->StagingMemory->Map(0, VK_WHOLE_SIZE));
        const uint32_t rowPitch = width * 4;
        pixels.resize(width * height);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* row = imageData + y * rowPitch;
            for (uint32_t x = 0; x < width; ++x)
            {
                uint32_t pixel = 0;
                std::memcpy(&pixel, row + x * 4, sizeof(pixel));
                FRgb8& outPixel = pixels[y * width + x];
                outPixel.r = static_cast<uint8_t>((pixel >> 16) & 0xff);
                outPixel.g = static_cast<uint8_t>((pixel >> 8) & 0xff);
                outPixel.b = static_cast<uint8_t>(pixel & 0xff);
            }
        }
        selectedSlot->StagingMemory->Unmap();
        selectedSlot->State = FReadbackState::ESlotState::Free;
        return true;
    }

    bool TuiPresenter::HasPendingCapture() const
    {
        if (readback_->CaptureRequested)
        {
            return true;
        }
        for (const auto& slot : readback_->Slots)
        {
            if (slot.State != FReadbackState::ESlotState::Free)
            {
                return true;
            }
        }
        return false;
    }

    void TuiPresenter::FlushQueuedKeyReleases()
    {
        const uint32_t frame = engine_.GetTotalFrames();
        for (auto it = inputState_.QueuedKeyReleases.begin(); it != inputState_.QueuedKeyReleases.end();)
        {
            if (frame >= it->ReleaseFrame)
            {
                PushKeyEvent(it->Scancode, it->Key, it->Mod, false);
                it = inputState_.QueuedKeyReleases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void TuiPresenter::HandleInput()
    {
        for (const FTerminalInputEvent& input : terminal_.PollInput())
        {
            const std::optional<FViewportPointer> pointer = MapPointerToViewport(input);
            const bool isMouseInput = input.Kind == ETerminalInputKind::MouseMove ||
                input.Kind == ETerminalInputKind::MouseButtonDown ||
                input.Kind == ETerminalInputKind::MouseButtonUp ||
                input.Kind == ETerminalInputKind::MouseWheel;
            if (isMouseInput && !pointer.has_value())
            {
                continue;
            }
            if (pointer.has_value())
            {
                const FViewportPointer previous = inputState_.LastPointerPosition.value_or(pointer.value());
                const FViewportPointer delta{
                    .X = pointer->X - previous.X,
                    .Y = pointer->Y - previous.Y,
                };
                if (!inputState_.LastPointerPosition.has_value() ||
                    std::abs(delta.X) > std::numeric_limits<float>::epsilon() ||
                    std::abs(delta.Y) > std::numeric_limits<float>::epsilon())
                {
                    PushMouseMotionEvent(pointer.value(), delta);
                }
                inputState_.LastPointerPosition = pointer;
            }

            switch (input.Kind)
            {
            case ETerminalInputKind::CtrlC:
                engine_.RequestClose();
                break;
            case ETerminalInputKind::ArrowUp:
                QueueKeyPress(SDL_SCANCODE_UP, SDLK_UP);
                break;
            case ETerminalInputKind::ArrowDown:
                QueueKeyPress(SDL_SCANCODE_DOWN, SDLK_DOWN);
                break;
            case ETerminalInputKind::ArrowLeft:
                QueueKeyPress(SDL_SCANCODE_LEFT, SDLK_LEFT);
                break;
            case ETerminalInputKind::ArrowRight:
                QueueKeyPress(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
                break;
            case ETerminalInputKind::Character:
                {
                    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(input.Character)));
                    if (lower == 'q')
                    {
                        engine_.RequestClose();
                        break;
                    }
                    if (lower == 'r')
                    {
                        RequestScreenshot();
                        break;
                    }

                    if (const std::optional<FKeyboardInput> keyboard = MapCharacter(input.Character))
                    {
                        QueueKeyPress(keyboard->Scancode, keyboard->Key, keyboard->Mod);
                    }
                }
                break;
            case ETerminalInputKind::MouseMove:
                break;
            case ETerminalInputKind::MouseButtonDown:
                PushMouseButtonEvent(input.MouseButton, true, pointer.value_or(FViewportPointer{}));
                break;
            case ETerminalInputKind::MouseButtonUp:
                PushMouseButtonEvent(input.MouseButton, false, pointer.value_or(FViewportPointer{}));
                break;
            case ETerminalInputKind::MouseWheel:
                PushMouseWheelEvent(input.WheelX, input.WheelY);
                break;
            }
        }
    }

    void TuiPresenter::MaybeHandleTerminalResize(const std::chrono::steady_clock::time_point now)
    {
        if (now - resizeState_.LastTerminalSizePollTime < TuiResizePollInterval)
        {
            return;
        }
        resizeState_.LastTerminalSizePollTime = now;

        const FTerminalSize terminalSize = terminal_.GetSize();
        if (terminalSize.Columns == 0 || terminalSize.Rows == 0)
        {
            return;
        }

        const FRenderTargetSize desiredSize = ComputeRenderTargetSize(terminalSize);
        if (desiredSize.Width == 0 || desiredSize.Height == 0)
        {
            return;
        }

        if (terminalSize.Columns != resizeState_.LastTerminalSize.Columns ||
            terminalSize.Rows != resizeState_.LastTerminalSize.Rows)
        {
            resizeState_.LastTerminalSize = terminalSize;
            resizeState_.PendingRenderTargetSize = desiredSize;
            resizeState_.StableSince = now;
            return;
        }

        if (!resizeState_.PendingRenderTargetSize.has_value())
        {
            if (!(desiredSize == resizeState_.CurrentRenderTargetSize))
            {
                resizeState_.PendingRenderTargetSize = desiredSize;
                resizeState_.StableSince = now;
            }
            return;
        }

        if (!(resizeState_.PendingRenderTargetSize.value() == desiredSize))
        {
            resizeState_.PendingRenderTargetSize = desiredSize;
            resizeState_.StableSince = now;
            return;
        }

        if (desiredSize == resizeState_.CurrentRenderTargetSize)
        {
            resizeState_.PendingRenderTargetSize.reset();
            return;
        }

        if (now - resizeState_.StableSince < TuiResizeDebounce)
        {
            return;
        }

        if (engine_.GetWindow().SetSize(desiredSize.Width, desiredSize.Height))
        {
            engine_.GetRenderer().RequestRecreateSwapChain();
            resizeState_.CurrentRenderTargetSize = desiredSize;
            blitter_.Reset();
            readback_->Slots.clear();
            readback_->CaptureRequested = true;
            inputState_.LastPointerPosition.reset();
            statusState_.Message = fmt::format("resize -> {}x{}", desiredSize.Width, desiredSize.Height);
            statusState_.UntilFrame = engine_.GetTotalFrames() + TuiStatusFramesResize;
        }
        resizeState_.PendingRenderTargetSize.reset();
    }

    std::optional<TuiPresenter::FViewportPointer> TuiPresenter::MapPointerToViewport(const FTerminalInputEvent& input) const
    {
        if (input.Kind != ETerminalInputKind::MouseMove &&
            input.Kind != ETerminalInputKind::MouseButtonDown &&
            input.Kind != ETerminalInputKind::MouseButtonUp &&
            input.Kind != ETerminalInputKind::MouseWheel)
        {
            return std::nullopt;
        }

        Vulkan::VulkanBaseRenderer& renderer = engine_.GetRenderer();
        if (!renderer.HasSwapChain())
        {
            return std::nullopt;
        }

        const FTerminalSize terminalSize = terminal_.GetSize();
        const uint32_t columns = options_.TuiMaxCols > 0
            ? std::min(terminalSize.Columns, options_.TuiMaxCols)
            : terminalSize.Columns;
        const uint32_t rows = options_.TuiMaxRows > 0
            ? std::min(terminalSize.Rows, options_.TuiMaxRows)
            : terminalSize.Rows;
        if (columns == 0 || rows <= 1)
        {
            return std::nullopt;
        }

        const uint32_t imageRows = rows - 1;
        if (input.Column >= columns || input.Row >= imageRows)
        {
            return std::nullopt;
        }

        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        const VkOffset2D outputOffset = swapChain.OutputOffset();
        const VkExtent2D outputExtent = swapChain.OutputExtent();
        if (outputExtent.width == 0 || outputExtent.height == 0)
        {
            return std::nullopt;
        }

        return FViewportPointer{
            .X = outputOffset.x + ((static_cast<float>(input.Column) + 0.5f) / static_cast<float>(columns)) *
                static_cast<float>(outputExtent.width),
            .Y = outputOffset.y + ((static_cast<float>(input.Row) + 0.5f) / static_cast<float>(imageRows)) *
                static_cast<float>(outputExtent.height),
        };
    }

    void TuiPresenter::QueueKeyPress(SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod)
    {
        PushKeyEvent(scancode, key, mod, true);
        inputState_.QueuedKeyReleases.push_back({
            .Scancode = scancode,
            .Key = key,
            .Mod = mod,
            .ReleaseFrame = engine_.GetTotalFrames() + 1,
        });
    }

    void TuiPresenter::RequestScreenshot()
    {
#if __linux__
        statusState_.Message = "screenshot disabled on Linux";
        statusState_.UntilFrame = engine_.GetTotalFrames() + TuiStatusFramesScreenshotSaved;
        return;
#else
        if (engine_.IsCapturingScreenShot())
        {
            statusState_.Message = "screenshot already in progress";
            statusState_.UntilFrame = engine_.GetTotalFrames() + TuiStatusFramesScreenshotBusy;
            return;
        }

        const std::string resolvedPath = Utilities::FileHelper::GetWritableFilePath("screenshots/tui_capture");
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(resolvedPath).parent_path().string());
        engine_.RequestScreenShot({.filename = resolvedPath});
        statusState_.Message = "shot -> " + resolvedPath + ".jpg";
        statusState_.UntilFrame = engine_.GetTotalFrames() + TuiStatusFramesScreenshotSaved;
#endif
    }

    std::string TuiPresenter::BuildStatusLine(uint32_t columns, uint32_t rows) const
    {
        std::ostringstream stream;
        stream << "TUI " << static_cast<int>(engine_.GetFrameRate()) << " fps"
               << " | frame " << engine_.GetTotalFrames()
               << " | term " << columns << "x" << rows
               << " | q quit";
        if (!options_.TuiNoInput)
        {
            stream << " | wasd/arrows input | r shot";
        }
        if (readback_ && (readback_->DroppedReadyFrames > 0 || readback_->BlockedCaptureFrames > 0))
        {
            stream << " | rb";
            if (readback_->DroppedReadyFrames > 0)
            {
                stream << " drop " << readback_->DroppedReadyFrames;
            }
            if (readback_->BlockedCaptureFrames > 0)
            {
                stream << " stall " << readback_->BlockedCaptureFrames;
            }
        }
        if (!statusState_.Message.empty() && engine_.GetTotalFrames() <= statusState_.UntilFrame)
        {
            stream << " | " << statusState_.Message;
        }
        return stream.str();
    }

    TuiPresenter::FRenderTargetSize TuiPresenter::ComputeRenderTargetSize(const FTerminalSize terminalSize) const
    {
        if (terminalSize.Columns < TuiMinColumns || terminalSize.Rows < TuiMinRows)
        {
            return {};
        }

        const TerminalBlitter::FSourceExtent renderExtent =
            blitter_.GetRenderExtent(terminalSize.Columns, terminalSize.Rows);
        if (renderExtent.Width == 0 || renderExtent.Height == 0)
        {
            return {};
        }

        const uint32_t ssaa = std::max(1u, options_.TuiSsaa);
        return {
            .Width = renderExtent.Width * ssaa,
            .Height = renderExtent.Height * ssaa,
        };
    }

    TuiPresenter::FRenderTargetSize TuiPresenter::ComputeOutputSize(const FTerminalSize terminalSize) const
    {
        if (terminalSize.Columns < TuiMinColumns || terminalSize.Rows < TuiMinRows)
        {
            return {};
        }

        const TerminalBlitter::FSourceExtent sourceExtent =
            blitter_.GetSourceExtent(terminalSize.Columns, terminalSize.Rows);
        if (sourceExtent.Width == 0 || sourceExtent.Height == 0)
        {
            return {};
        }

        return {
            .Width = sourceExtent.Width,
            .Height = sourceExtent.Height,
        };
    }

    std::optional<TuiPresenter::FKeyboardInput> TuiPresenter::MapCharacter(const char value) const
    {
        const unsigned char raw = static_cast<unsigned char>(value);
        if (raw >= 'a' && raw <= 'z')
        {
            const SDL_Keycode key = static_cast<SDL_Keycode>(SDLK_A + (raw - 'a'));
            const SDL_Scancode scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (raw - 'a'));
            return FKeyboardInput{.Scancode = scancode, .Key = key};
        }
        if (raw >= 'A' && raw <= 'Z')
        {
            const SDL_Keycode key = static_cast<SDL_Keycode>(SDLK_A + (raw - 'A'));
            const SDL_Scancode scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (raw - 'A'));
            return FKeyboardInput{.Scancode = scancode, .Key = key, .Mod = SDL_KMOD_SHIFT};
        }
        if (raw >= '1' && raw <= '9')
        {
            const SDL_Keycode key = static_cast<SDL_Keycode>(SDLK_1 + (raw - '1'));
            const SDL_Scancode scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (raw - '1'));
            return FKeyboardInput{.Scancode = scancode, .Key = key};
        }
        if (raw == '0')
        {
            return FKeyboardInput{.Scancode = SDL_SCANCODE_0, .Key = SDLK_0};
        }

        switch (value)
        {
        case ' ':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_SPACE, .Key = SDLK_SPACE};
        case '\r':
        case '\n':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_RETURN, .Key = SDLK_RETURN};
        case '\t':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_TAB, .Key = SDLK_TAB};
        case '\b':
        case 127:
            return FKeyboardInput{.Scancode = SDL_SCANCODE_BACKSPACE, .Key = SDLK_BACKSPACE};
        case '-':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_MINUS, .Key = SDLK_MINUS};
        case '=':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_EQUALS, .Key = SDLK_EQUALS};
        case '[':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_LEFTBRACKET, .Key = SDLK_LEFTBRACKET};
        case ']':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_RIGHTBRACKET, .Key = SDLK_RIGHTBRACKET};
        case ';':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_SEMICOLON, .Key = SDLK_SEMICOLON};
        case '\'':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_APOSTROPHE, .Key = SDLK_APOSTROPHE};
        case ',':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_COMMA, .Key = SDLK_COMMA};
        case '.':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_PERIOD, .Key = SDLK_PERIOD};
        case '/':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_SLASH, .Key = SDLK_SLASH};
        case '\\':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_BACKSLASH, .Key = SDLK_BACKSLASH};
        case '`':
            return FKeyboardInput{.Scancode = SDL_SCANCODE_GRAVE, .Key = SDLK_GRAVE};
        default:
            break;
        }

        return std::nullopt;
    }

    void TuiPresenter::PushKeyEvent(SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod, bool down) const
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.type = static_cast<SDL_EventType>(event.type);
        event.key.timestamp = SDL_GetTicksNS();
        event.key.windowID = ResolveWindowId(engine_);
        event.key.scancode = scancode;
        event.key.mod = mod;
        event.key.key = key;
        event.key.down = down;
        event.key.repeat = false;
        SDL_PushEvent(&event);
    }

    void TuiPresenter::PushMouseMotionEvent(const FViewportPointer& position, const FViewportPointer& delta) const
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.type = static_cast<SDL_EventType>(event.type);
        event.motion.timestamp = SDL_GetTicksNS();
        event.motion.windowID = ResolveWindowId(engine_);
        event.motion.x = position.X;
        event.motion.y = position.Y;
        event.motion.xrel = delta.X;
        event.motion.yrel = delta.Y;
        SDL_PushEvent(&event);
    }

    void TuiPresenter::PushMouseButtonEvent(const ETerminalMouseButton button,
                                            const bool down,
                                            const FViewportPointer& position) const
    {
        uint8_t sdlButton = 0;
        switch (button)
        {
        case ETerminalMouseButton::Left:
            sdlButton = SDL_BUTTON_LEFT;
            break;
        case ETerminalMouseButton::Middle:
            sdlButton = SDL_BUTTON_MIDDLE;
            break;
        case ETerminalMouseButton::Right:
            sdlButton = SDL_BUTTON_RIGHT;
            break;
        case ETerminalMouseButton::None:
            return;
        }

        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.type = static_cast<SDL_EventType>(event.type);
        event.button.timestamp = SDL_GetTicksNS();
        event.button.windowID = ResolveWindowId(engine_);
        event.button.button = sdlButton;
        event.button.down = down;
        event.button.x = position.X;
        event.button.y = position.Y;
        SDL_PushEvent(&event);
    }

    void TuiPresenter::PushMouseWheelEvent(const float x, const float y) const
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.type = static_cast<SDL_EventType>(event.type);
        event.wheel.timestamp = SDL_GetTicksNS();
        event.wheel.windowID = ResolveWindowId(engine_);
        event.wheel.x = x;
        event.wheel.y = y;
        SDL_PushEvent(&event);
    }
}
