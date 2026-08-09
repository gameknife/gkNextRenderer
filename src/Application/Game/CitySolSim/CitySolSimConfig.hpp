#pragma once

#include <array>

#include <glm/glm.hpp>

namespace CitySolSim::Config
{
    inline constexpr const char* kScenePath = "assets/scad/source/habor_city_v2.scad";
    inline constexpr const char* kCitizenRigPath = "assets/scad/characters/agent_basic.scad";

    constexpr int kVehicleCount = 36;
    constexpr int kCitizenCount = 32;
    constexpr float kGroundY = 0.36f;
    constexpr float kVehicleGroundY = 0.22f;
    constexpr float kDefaultTimeScale = 8.0f;
    constexpr float kBaseWalkSpeed = 2.4f;

    // habor_city_v2.scad is Z-up. SCAD y maps to engine -z.
    inline constexpr std::array<float, 11> kRoadX = {
        -280.0f, -224.0f, -168.0f, -112.0f, -56.0f, 0.0f, 56.0f, 112.0f, 168.0f, 224.0f, 280.0f,
    };
    inline constexpr std::array<float, 7> kRoadZ = {
        30.0f, -20.0f, -70.0f, -120.0f, -170.0f, -220.0f, -270.0f,
    };
    inline constexpr std::array<float, 12> kBlockX = {
        -308.0f, -252.0f, -196.0f, -140.0f, -84.0f, -28.0f,
        28.0f, 84.0f, 140.0f, 196.0f, 252.0f, 308.0f,
    };
    inline constexpr std::array<float, 6> kBlockScadY = {-5.0f, 45.0f, 95.0f, 145.0f, 195.0f, 245.0f};

    struct FZone
    {
        glm::vec3 position;
        const char* label;
    };

    constexpr glm::vec3 BlockFront(int column, int row, float xOffset = 0.0f)
    {
        return {kBlockX[static_cast<size_t>(column)] + xOffset, kGroundY,
                -kBlockScadY[static_cast<size_t>(row)] + 18.0f};
    }

    inline constexpr std::array<FZone, 12> kHomes = {{
        {BlockFront(0, 1), "西湾住宅"}, {BlockFront(1, 1), "棕榈公寓"},
        {BlockFront(3, 1), "中央公寓"}, {BlockFront(9, 1), "东港公寓"},
        {BlockFront(11, 1), "海岬住宅"}, {BlockFront(0, 2), "西山社区"},
        {BlockFront(3, 2), "学院公寓"}, {BlockFront(10, 2), "东城公寓"},
        {BlockFront(1, 3), "林荫社区"}, {BlockFront(7, 3), "湖畔公寓"},
        {BlockFront(4, 4), "北丘住宅"}, {BlockFront(8, 5), "风谷住宅"},
    }};

    inline constexpr std::array<FZone, 12> kWorkplaces = {{
        {BlockFront(2, 1), "港湾医院"}, {BlockFront(4, 1), "金融中心"},
        {BlockFront(5, 1), "市政厅"}, {BlockFront(7, 1), "蓝湾科技"},
        {BlockFront(8, 1), "警消中心"}, {BlockFront(2, 2), "海港学校"},
        {BlockFront(4, 2), "创意大厦"}, {BlockFront(6, 2), "航运大厦"},
        {BlockFront(9, 0), "西码头物流"}, {BlockFront(10, 0), "东码头物流"},
        {BlockFront(3, 0), "海鲜市场"}, {BlockFront(6, 1), "中央车站"},
    }};

    inline constexpr std::array<FZone, 10> kLeisure = {{
        {BlockFront(8, 2), "中央公园"}, {BlockFront(2, 3), "西城公园"},
        {BlockFront(5, 3), "市民花园"}, {BlockFront(9, 3), "东城公园"},
        {BlockFront(0, 0), "西海滨"}, {BlockFront(11, 0), "东海滨"},
        {glm::vec3(-120.0f, kGroundY, 37.0f), "滨海步道"},
        {glm::vec3(18.0f, kGroundY, 42.0f), "游艇港"},
        {BlockFront(5, 0), "交通广场"}, {BlockFront(3, 0), "夜市"},
    }};

    inline constexpr std::array<glm::vec3, 10> kCitizenColors = {{
        {0.91f, 0.30f, 0.24f}, {0.20f, 0.55f, 0.86f}, {0.25f, 0.72f, 0.48f},
        {0.94f, 0.68f, 0.18f}, {0.65f, 0.38f, 0.78f}, {0.95f, 0.48f, 0.63f},
        {0.20f, 0.70f, 0.72f}, {0.83f, 0.45f, 0.20f}, {0.46f, 0.60f, 0.88f},
        {0.58f, 0.75f, 0.25f},
    }};

    inline constexpr std::array<const char*, 32> kCitizenNames = {{
        "陈晨", "林夏", "周航", "叶青", "苏然", "方宁", "高远", "许晴",
        "顾川", "唐雨", "沈星", "陆遥", "宋安", "白露", "韩松", "江月",
        "程野", "罗伊", "谢岚", "钟鸣", "夏禾", "吴桐", "秦风", "何夕",
        "乔木", "温言", "余光", "穆青", "季川", "简宁", "杜衡", "顾海",
    }};
}
