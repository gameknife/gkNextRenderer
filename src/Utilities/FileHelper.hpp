#include <random>
#include <string>

namespace Utilities
{
    namespace FileHelper
    {
        static std::string GetPlatformFilePath( const char* srcPath )
        {
#if ANDROID
            return std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath;
#else
            return std::string("../") + srcPath;
#endif
        }
    }

    namespace NameHelper
    {
        static std::string RandomName(size_t length)
        {
            const std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::random_device rd;
            std::mt19937 generator(rd());
            std::uniform_int_distribution<> distribution(0, characters.size() - 1);

            std::string randomName;
            for (size_t i = 0; i < length; ++i) {
                randomName += characters[distribution(generator)];
            }

            return randomName;
        }
    }
}
