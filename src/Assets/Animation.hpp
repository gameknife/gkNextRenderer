#pragma once
#include "Common/CoreMinimal.hpp"

namespace  ozz
{
	namespace animation
	{
		class Skeleton;
		class Animation;
	}
}
namespace Assets
{
	bool LoadSkeleton(const char* _filename, ozz::animation::Skeleton* _skeleton);

	bool LoadAnimation(const char* _filename, ozz::animation::Animation* _animation);
}