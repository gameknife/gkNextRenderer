#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Data/Animation.hpp"

class NextAnimation final
{
public:
    GK_NON_COPIABLE(NextAnimation)

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
