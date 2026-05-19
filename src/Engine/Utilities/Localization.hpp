#pragma once

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextLocalization.h"

namespace Utilities
{
    namespace Localization
    {
        inline const char* GetLocText(const char* srcText)
        {
            static thread_local std::string localized;
            if (NextEngine* engine = NextEngine::GetInstance())
            {
                if (NextLocalization* localization = engine->GetLocalization())
                {
                    localized = localization->Get(srcText, srcText);
                    return localized.c_str();
                }
            }
            return srcText;
        }
    }
}

#define LOCTEXT(A) Utilities::Localization::GetLocText(A)
