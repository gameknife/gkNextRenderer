## 2024-05-22 - Batching Vulkan Barriers
**Learning:** Vulkan memory barriers (`vkCmdPipelineBarrier`) are expensive. Issuing them individually causes driver overhead and pipeline stalls. Batching them reduces this overhead significantly.
**Action:** Always collect barriers and issue a single `vkCmdPipelineBarrier` call per synchronization point.
