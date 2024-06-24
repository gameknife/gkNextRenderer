#include <string>

namespace Utilities
{
    namespace Math
    {
        static uint32_t GetSafeDispatchCount( uint32_t size, uint32_t divider )
        {
            return size % divider == 0 ? size / divider : size / divider + 1;
        }
    }

	static int metricFormatter(double value, char* buff, int size, void* data)	//if pass data as (void*)"b" - show info like kb, Mb, Gb
	{
		const char* unit = (const char*)data;
		static double      s_value[] = { 1000000000, 1000000, 1000, 1, 0.001, 0.000001, 0.000000001 };
		static const char* s_prefix[] = { "G", "M", "k", "", "m", "u", "n" };

		if (value == 0)
		{
			return snprintf(buff, size, " ");
		}

		if (value < 10001.f)
		{
			return snprintf(buff, size, "%.0f", value);
		}

		for (int i = 0; i < 7; ++i)
		{
			if (fabs(value) >= s_value[i])
			{
				return snprintf(buff, size, "%.2f%s%s", value / s_value[i], s_prefix[i], unit);
			}
		}
		return snprintf(buff, size, "%.2f%s%s", value / s_value[6], s_prefix[6], unit);
	}

}
