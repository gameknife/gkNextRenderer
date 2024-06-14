#include <string>

namespace Utilities
{
    namespace Math
    {
        uint32_t GetSafeDispatchCount( uint32_t size, uint32_t divider )
        {
            return size % divider == 0 ? size / divider : size / divider + 1;
        }
    }

}
