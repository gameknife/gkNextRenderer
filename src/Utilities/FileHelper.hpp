#pragma once

#include <filesystem>
#include <string>

namespace Utilities
{
    namespace FileHelper
    {
        static std::string GetPlatformFilePath( const char* srcPath )
        {
#if ANDROID
            return ("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath;
#else
            return std::string("../") + srcPath;
#endif
        }
        
        static std::string GetNormalizedFilePath( const char* srcPath )
        {
            std::string normlizedPath {};
#if ANDROID
            normlizedPath = std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath;
#else
            normlizedPath = std::string("../") + srcPath;
#endif
            std::filesystem::path fullPath(normlizedPath);
            std::filesystem::path directory = fullPath.parent_path();
            std::string pattern = fullPath.filename().string();

            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file() && entry.path().filename().string() == pattern) {
                    normlizedPath =  std::filesystem::absolute(entry.path()).string();
                    break;
                }
            }

            return normlizedPath;
        }  
    }

}
