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

	static int metricFormatter(double value, char* buff, void* data)	//if pass data as (void*)"b" - show info like kb, Mb, Gb
	{
		const char* unit = (const char*)data;
		static double      s_value[] = { 1000000000, 1000000, 1000, 1, 0.001, 0.000001, 0.000000001 };
		static const char* s_prefix[] = { "G", "M", "k", "", "m", "u", "n" };

		const int s_valueSZ = sizeof(s_value);

		if (value < s_value[s_valueSZ - 1])
		{
			return sprintf(buff, " ");
		}

		if (value < 10001.f)
		{
			return sprintf(buff, "%.0f", value);
		}

		for (int i = 0; i < s_valueSZ; ++i)
		{
			if (fabs(value) >= s_value[i])
			{
				return sprintf(buff, "%.2f%s%s", value / s_value[i], s_prefix[i], unit);
			}
		}
		return sprintf(buff, "%.2f%s%s", value / s_value[6], s_prefix[6], unit);
	}

	static void get_time_str(char * s, float elapsedTime)
	{
		int hours, mins, secs;

		hours=(int)floor(elapsedTime/3600);
		mins=(int)floor((elapsedTime-hours*3600)/60);
		secs=(int)floor(elapsedTime-hours*3600-mins*60);

		s[0] = 0;

		if(elapsedTime > 59.99f)
			{
			 if(hours > 0.99f) sprintf(s,"%s%dh", s, hours);
			 if(mins > 0.99f)
				{
				if(hours > 0.99f) strcat(s," ");
				if(mins < 1.f) {
					if(secs > 0.99f) sprintf(s,"%s%ds", s, secs);
					}
				else {
						if(secs > 0.99f) sprintf(s,"%s%dm %ds", s, mins, secs);
						else sprintf(s,"%s%dm", s, mins);
					}
				} else
				if(secs > 0.99f)
				{
					if(hours > 0.99f) strcat(s," ");
					sprintf(s,"%s%ds", s, secs);
				}
			}
		else {
			sprintf(s,"%.1fs", elapsedTime);
		}
	}
}
