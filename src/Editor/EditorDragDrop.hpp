#pragma once

#include "Common/CoreMinimal.hpp"

#include <cstdint>

namespace Editor
{
    constexpr const char* kEditorDragDropPayload = "GK_EDITOR_DND";

    enum class EEditorDragPayloadType : uint8_t
    {
        Scene = 0,
        Material,
    };

    struct EditorDragDropPayload
    {
        EEditorDragPayloadType type = EEditorDragPayloadType::Scene;
        char path[512]{};
        uint32_t materialId = 0;
    };
} // namespace Editor
