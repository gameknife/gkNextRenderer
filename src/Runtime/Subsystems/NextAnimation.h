#pragma once
#include "Vulkan/Vulkan.hpp"
#include "Assets/Data/Animation.hpp"

class NextAnimation final
{
public:
    VULKAN_NON_COPIABLE(NextAnimation)

    NextAnimation();
    ~NextAnimation();

    void Start();
    void Tick(double DeltaSeconds);
    void Stop();

#if WITH_OZZ
    std::unique_ptr<ozz::animation::Skeleton> skeleton_;
    std::unique_ptr<ozz::animation::Animation> animation_;
#endif
};
