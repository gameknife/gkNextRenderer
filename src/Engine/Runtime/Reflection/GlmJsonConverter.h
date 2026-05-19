#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace glm
{
    // JSON serialization for glm::vec2
    inline void to_json(nlohmann::json& j, const vec2& v)
    {
        j = nlohmann::json{{"x", v.x}, {"y", v.y}};
    }

    inline void from_json(const nlohmann::json& j, vec2& v)
    {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
    }

    // JSON serialization for glm::vec3
    inline void to_json(nlohmann::json& j, const vec3& v)
    {
        j = nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }

    inline void from_json(const nlohmann::json& j, vec3& v)
    {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
        v.z = j.at("z").get<float>();
    }

    // JSON serialization for glm::vec4
    inline void to_json(nlohmann::json& j, const vec4& v)
    {
        j = nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
    }

    inline void from_json(const nlohmann::json& j, vec4& v)
    {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
        v.z = j.at("z").get<float>();
        v.w = j.at("w").get<float>();
    }

    // JSON serialization for glm::quat
    inline void to_json(nlohmann::json& j, const quat& q)
    {
        j = nlohmann::json{{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}};
    }

    inline void from_json(const nlohmann::json& j, quat& q)
    {
        q.x = j.at("x").get<float>();
        q.y = j.at("y").get<float>();
        q.z = j.at("z").get<float>();
        q.w = j.at("w").get<float>();
    }

    // JSON serialization for glm::mat4
    inline void to_json(nlohmann::json& j, const mat4& m)
    {
        j = nlohmann::json::array();
        for (int i = 0; i < 4; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                j.push_back(m[i][k]);
            }
        }
    }

    inline void from_json(const nlohmann::json& j, mat4& m)
    {
        int idx = 0;
        for (int i = 0; i < 4; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                m[i][k] = j.at(idx++).get<float>();
            }
        }
    }
}
