#pragma once

#include "Options.hpp"
#include <vector>
#include <list>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>

#define SCOPED_GPU_TIMER_FOLDER(name, folder) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name, folder)
#define SCOPED_GPU_TIMER(name) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name)
#define SCOPED_CPU_TIMER(name) ScopedCpuTimer scopedCpuTimer(GpuTimer(), name)
#define BENCH_MARK_CHECK() if(!GOption->HardwareQuery) return

namespace Vulkan 
{
	class VulkanGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(VulkanGpuTimer)
		
		VulkanGpuTimer(VkDevice device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop);
		virtual ~VulkanGpuTimer();

		void Reset(VkCommandBuffer commandBuffer)
		{
			BENCH_MARK_CHECK();
			vkCmdResetQueryPool(commandBuffer, query_pool_timestamps, 0, static_cast<uint32_t>(time_stamps.size()));
			queryIdx = 0;
			started_ = true;
			
			CalculatieGpuStats();
			for(auto& [name, query] : gpu_timer_query_map)
			{
				std::get<1>(gpu_timer_query_map[name]) = 0;
				std::get<0>(gpu_timer_query_map[name]) = 0;
			}
		}

		void CpuFrameEnd()
		{
			// iterate the cpu_timer_query_map
			for(auto& [name, query] : cpu_timer_query_map)
			{
				std::get<2>(cpu_timer_query_map[name]) = std::get<1>(cpu_timer_query_map[name]) - std::get<0>(cpu_timer_query_map[name]);
			}
		}

		void FrameEnd(VkCommandBuffer commandBuffer)
		{
			BENCH_MARK_CHECK();
			if(started_)
			{
				started_ = false;
			}
			else
			{
				return;
			}
			vkGetQueryPoolResults(
				device_,
				query_pool_timestamps,
				0,
				queryIdx,
				time_stamps.size() * sizeof(uint64_t),
				time_stamps.data(),
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

			for(auto& [name, query] : gpu_timer_query_map)
			{
				std::get<2>(gpu_timer_query_map[name]) = (time_stamps[ std::get<1>(gpu_timer_query_map[name]) ] - time_stamps[ std::get<0>(gpu_timer_query_map[name])]);
			}
		}

		void Start(VkCommandBuffer commandBuffer, const char* name)
		{
			BENCH_MARK_CHECK();
			if( gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
			{
				gpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
			}
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<0>(gpu_timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		void End(VkCommandBuffer commandBuffer, const char* name)
		{
			BENCH_MARK_CHECK();
			assert( gpu_timer_query_map.find(name) != gpu_timer_query_map.end() );
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<1>(gpu_timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		void StartCpuTimer(const char* name)
		{
			if( cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				cpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
			}
			std::get<0>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		void EndCpuTimer(const char* name)
		{
			assert( cpu_timer_query_map.find(name) != cpu_timer_query_map.end() );
			std::get<1>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		float GetGpuTime(const char* name)
		{
			if(gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
			{
				return 0;
			}
			return std::get<2>(gpu_timer_query_map[name]) * timeStampPeriod_ * 1e-6f;
		}
		float GetCpuTime(const char* name)
		{
			if(cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				return 0;
			}
			return std::get<2>(cpu_timer_query_map[name]) * 1e-6f;
		}

		void CalculatieGpuStats()
		{
			lastStats.clear();
			std::list<std::tuple<std::string, float, uint64_t, uint64_t> > order_list;
			for(auto& [name, query] : gpu_timer_query_map)
			{
				if ( std::get<2>(gpu_timer_query_map[name]) == 0)
				{
					continue;
				}
				order_list.insert(order_list.begin(), (std::make_tuple(name, GetGpuTime(name.c_str()), std::get<0>(query), std::get<1>(query))));
			}
			
			order_list.sort([](const std::tuple<std::string, float, uint64_t, uint64_t>& a, const std::tuple<std::string, float, uint64_t, uint64_t>& b) -> bool
			{
				return std::get<2>(a) < std::get<2>(b);
			});
			
			std::vector<std::tuple<std::string, float, uint64_t, uint64_t>> activeTimers;
			for(auto& [name, time, startIdx, endIdx] : order_list)
			{
				while (!activeTimers.empty() && std::get<3>(activeTimers.back()) < startIdx) {
					activeTimers.pop_back();
				}
				
				int stackDepth = static_cast<int>(activeTimers.size());
				activeTimers.push_back(std::make_tuple(name, time, startIdx, endIdx));
				lastStats.push_back(std::make_tuple(name, stackDepth, time));
			}
		}
		std::vector<std::tuple<std::string, float> > FetchAllTimes( int maxStack )
		{
			std::vector<std::tuple<std::string, float> > result;
			for(auto& [name, stackDepth, time] : lastStats)
			{
				std::string prefix = "";
			    for (size_t i = 0; i < stackDepth; i++) {
			    	prefix += (i == stackDepth - 1) ? " - " : "   ";
			    }
				if (maxStack > stackDepth)
				{
					result.push_back(std::make_tuple(prefix + name, time));
				}
			}
			return result;
		}

		std::vector<std::tuple<std::string, int, float> > lastStats; // name, depth, duration seconds
		VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
		std::vector<uint64_t> time_stamps{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > gpu_timer_query_map{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > cpu_timer_query_map{};
		VkDevice device_ = VK_NULL_HANDLE;
		uint32_t queryIdx = 0;
		float timeStampPeriod_ = 1;
		bool started_ = false;
	};

	class ScopedGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedGpuTimer)

		ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name, const char* foldername ):commandBuffer_(commandBuffer),timer_(timer), name_(name)
		{
			timer_->Start(commandBuffer_, name_.c_str());
			folderTimer = true;
			PushFolder(foldername);
		}
		ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name ):commandBuffer_(commandBuffer),timer_(timer), name_(name)
		{
			timer_->Start(commandBuffer_, (folderName_ + name_).c_str());
		}
		virtual ~ScopedGpuTimer()
		{
			if (folderTimer)
			{
				PopFolder();
				timer_->End(commandBuffer_, name_.c_str());
			}
			else
			{
				timer_->End(commandBuffer_, (folderName_ + name_).c_str());
			}
		}
		VkCommandBuffer commandBuffer_;
		VulkanGpuTimer* timer_;
		std::string name_;
		bool folderTimer = false;

		static std::string folderName_;
		static void PushFolder(const std::string& name)
		{
			folderName_ = name;
		}
		static void PopFolder()
		{
			folderName_ = "";
		}
	};

	inline std::string ScopedGpuTimer::folderName_;

	class ScopedCpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedCpuTimer)
		
		ScopedCpuTimer(VulkanGpuTimer* timer, const char* name ):timer_(timer), name_(name)
		{
			timer_->StartCpuTimer((folderName_ + name_).c_str());
		}
		virtual ~ScopedCpuTimer()
		{
			timer_->EndCpuTimer( (folderName_ + name_).c_str());
		}
		VulkanGpuTimer* timer_;
		std::string name_;

		static std::string folderName_;
		static void PushFolder(const std::string& name)
		{
			folderName_ = name;
		}
		static void PopFolder(const std::string& name)
		{
			folderName_ = "";
		}
	};

	inline std::string ScopedCpuTimer::folderName_;
}
