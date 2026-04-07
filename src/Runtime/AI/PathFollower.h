#pragma once
#include <glm/glm.hpp>
#include <vector>

struct FPathFollower
{
    std::vector<glm::vec3> waypoints;
    size_t currentIndex = 0;
    float repathTimer = 0.0f;
    glm::vec3 lastTargetPosition{0.0f};

    glm::vec3 GetMoveDirection(const glm::vec3& currentPos, float arrivalThreshold = 0.8f)
    {
        if (waypoints.empty() || currentIndex >= waypoints.size())
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 toWaypoint = waypoints[currentIndex] - currentPos;
        toWaypoint.y = 0.0f;
        float dist = glm::length(toWaypoint);

        while (dist <= arrivalThreshold && currentIndex + 1 < waypoints.size())
        {
            currentIndex++;
            toWaypoint = waypoints[currentIndex] - currentPos;
            toWaypoint.y = 0.0f;
            dist = glm::length(toWaypoint);
        }

        if (dist <= 0.001f)
        {
            return glm::vec3(0.0f);
        }
        return toWaypoint / dist;
    }

    bool IsFinished(const glm::vec3& currentPos, float threshold = 0.8f) const
    {
        if (waypoints.empty())
        {
            return true;
        }
        glm::vec3 toEnd = waypoints.back() - currentPos;
        toEnd.y = 0.0f;
        return glm::length(toEnd) <= threshold;
    }

    bool NeedsRepath(const glm::vec3& targetPos, float deltaSeconds,
                     float repathInterval = 0.5f, float targetMovedThreshold = 2.0f)
    {
        repathTimer += deltaSeconds;
        float targetMoved = glm::length(targetPos - lastTargetPosition);
        if (repathTimer >= repathInterval || targetMoved >= targetMovedThreshold || waypoints.empty())
        {
            repathTimer = 0.0f;
            lastTargetPosition = targetPos;
            return true;
        }
        return false;
    }

    void SetPath(std::vector<glm::vec3> newPath, const glm::vec3& targetPos)
    {
        waypoints = std::move(newPath);
        currentIndex = 0;
        lastTargetPosition = targetPos;
        repathTimer = 0.0f;
    }

    void Clear()
    {
        waypoints.clear();
        currentIndex = 0;
        repathTimer = 0.0f;
    }
};
