#pragma once

#include "Common/CoreMinimal.hpp"
#include "Assets/Loaders/FLDrawConfig.h"

namespace BrickPlayer::Shadow
{
    enum class ESnapKind : uint8_t
    {
        Cylinder,
    };

    enum class ESnapGender : uint8_t
    {
        Unknown,
        Male,
        Female,
    };

    struct FCylinderSection
    {
        std::string profile;
        float radius = 0.0f;
        float length = 0.0f;
    };

    struct FSnapConnector
    {
        ESnapKind kind = ESnapKind::Cylinder;
        ESnapGender gender = ESnapGender::Unknown;
        std::string id;
        std::string group;
        glm::vec3 localPosition{0.0f};
        glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
        float length = 0.0f;
        bool slide = false;
        bool centered = false;
        std::vector<FCylinderSection> sections;
    };

    class FShadowLibrary
    {
    public:
        bool Initialize(float lduToWorldScale);
        const std::vector<FSnapConnector>& GetConnectorsForPart(const std::string& partFile) const;

    private:
        std::vector<FSnapConnector> LoadConnectorsForPart(const std::string& partFile) const;

        bool initialized_ = false;
        float lduToWorldScale_ = 0.0f;
        Assets::LDrawFileResolver originalResolver_;
        Assets::LDrawFileResolver shadowResolver_;
        mutable std::unordered_map<std::string, std::vector<FSnapConnector>> connectorCache_;
    };
}
