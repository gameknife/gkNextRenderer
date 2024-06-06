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

}
