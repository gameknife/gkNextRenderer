#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UI/AppChrome.hpp"
#include "Engine/Runtime/Editor/UI/UiContext.hpp"
#include "Engine/Runtime/Editor/UI/UiContainers.hpp"
#include "Engine/Runtime/Editor/UiFrame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

TEST_CASE("UI metrics scale deterministically", "[Unit][UI]")
{
    using NextUI::Foundation::FUiMetrics;
    const FUiMetrics one = FUiMetrics::FromScale(1.0f);
    const FUiMetrics oneAndHalf = FUiMetrics::FromScale(1.5f);
    const FUiMetrics two = FUiMetrics::FromScale(2.0f);

    CHECK(one.titleBarHeight == 48.0f);
    CHECK(oneAndHalf.titleBarHeight == 72.0f);
    CHECK(two.toolbarHeight == 76.0f);
    CHECK(two.spacing == 12.0f);
}

TEST_CASE("UI scoped containers restore ImGui stacks", "[Unit][UI]")
{
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    REQUIRE(fontPixels != nullptr);
    ImGui::NewFrame();

    const int initialColorStack = context->ColorStack.Size;
    const int initialStyleStack = context->StyleVarStack.Size;
    {
        NextUI::Foundation::FOverlayPanelOptions options;
        options.windowId = "ScopedOverlayTest";
        options.position = ImVec2(10.0f, 10.0f);
        options.size = ImVec2(240.0f, 120.0f);
        options.padding = ImVec2(11.0f, 7.0f);
        options.borderSize = 0.0f;
        NextUI::Foundation::FScopedOverlayPanel panel(options);
        CHECK(context->StyleVarStack.Size == initialStyleStack + 4);
        CHECK(context->ColorStack.Size == initialColorStack + 2);
    }
    CHECK(context->StyleVarStack.Size == initialStyleStack);
    CHECK(context->ColorStack.Size == initialColorStack);

    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
}

TEST_CASE("UI frame layers are explicit and policy composable", "[Unit][UI]")
{
    using namespace NextUI;
    const FUiFrameResult editorResult{EUiDeveloperLayer::Console};
    CHECK(HasUiLayer(editorResult.requestedDeveloperLayers, EUiDeveloperLayer::Console));
    CHECK_FALSE(HasUiLayer(editorResult.requestedDeveloperLayers, EUiDeveloperLayer::Statistics));

    const FUiFramePolicy remotePolicy{.allowApplicationUi = true,
                                      .allowedDeveloperLayers = EUiDeveloperLayer::None};
    CHECK((editorResult.requestedDeveloperLayers & remotePolicy.allowedDeveloperLayers) ==
          EUiDeveloperLayer::None);
    CHECK(FUiFrameResult::FromLegacyHandled(false).requestedDeveloperLayers == EUiDeveloperLayer::All);
    CHECK(FUiFrameResult::FromLegacyHandled(true).requestedDeveloperLayers == EUiDeveloperLayer::None);
}

TEST_CASE("Bottom bar stays attached to the main viewport", "[Unit][UI]")
{
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    REQUIRE(fontPixels != nullptr);
    ImGui::NewFrame();

    bool contentDrawn = false;
    NextUI::Foundation::FBottomBarOptions options;
    options.windowId = "BottomBarViewportTest";
    options.height = 30.0f;
    options.drawLeftContent = [&contentDrawn]() { contentDrawn = true; };
    NextUI::Foundation::DrawBottomBar(options);

    ImGuiWindow* window = ImGui::FindWindowByName(options.windowId);
    REQUIRE(window != nullptr);
    CHECK(contentDrawn);
    CHECK(window->Viewport == ImGui::GetMainViewport());
    CHECK((window->Flags & ImGuiWindowFlags_NoDocking) != 0);
    CHECK((window->Flags & ImGuiWindowFlags_NoScrollbar) != 0);
    CHECK(window->Pos.y == 690.0f);
    CHECK(window->Size.y == 30.0f);

    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
}
