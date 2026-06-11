#pragma once

#include "AirportSimTypes.h"

namespace AirportSim
{
    class AgentSystem;
    class QueueSystem;
    class FlightBoard;

    // 感知层（§5.4）：每 0.5s 扫描邻居/队列长度/航班事件，把"决策时刻"写到
    // agent.eventNote 并把 nextDecisionAt 拉到当前（高优先级插队）。
    // 纯规则即时反应（让路/站队尾）由 AgentSystem 分离力与 QueueSystem 负责。
    class PerceptionSystem
    {
    public:
        void Reset();
        void Tick(double deltaRealSeconds, double gameMinutes, AgentSystem& agents, const QueueSystem& queues,
                  FlightBoard& flights);

    private:
        double accumulator_ = 0.0;
    };
}
