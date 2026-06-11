#include "DecisionScheduler.h"

#include "AgentSystem.h"
#include "AirportMap.h"
#include "AirportSimConfig.hpp"
#include "JourneySystem.h"

#include <algorithm>
#include <iterator>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AIService.hpp"

namespace AirportSim
{
    namespace
    {
        constexpr double kLlmTimeoutGameMinutes = 25.0; // ≈10 真实秒（默认 2 min/s）
        constexpr int kMaxChatChain = 2;                // 对话最多一来一回，之后不再插队

        // 性格决定开口预算：健谈型决策更频繁、引导语更鼓励；沉静型反之。
        bool IsChattyPersonality(const std::string& personality)
        {
            return personality.find("健谈") != std::string::npos || personality.find("话痨") != std::string::npos ||
                   personality.find("爱聊天") != std::string::npos || personality.find("爱吆喝") != std::string::npos ||
                   personality.find("幽默") != std::string::npos || personality.find("热心") != std::string::npos;
        }

        bool IsQuietPersonality(const std::string& personality)
        {
            return personality.find("话少") != std::string::npos || personality.find("沉稳") != std::string::npos ||
                   personality.find("安静") != std::string::npos || personality.find("沉默") != std::string::npos ||
                   personality.find("不苟言笑") != std::string::npos;
        }

        double DecisionCooldownFor(const FAgent& agent)
        {
            if (IsChattyPersonality(agent.personality))
            {
                return Config::kDecisionCooldownMinutes * 0.6;
            }
            if (IsQuietPersonality(agent.personality))
            {
                return Config::kDecisionCooldownMinutes * 1.5;
            }
            return Config::kDecisionCooldownMinutes;
        }
        constexpr size_t kLogLimit = 60;

        // ---- 预制台词库（fallback，§5.3）----
        const char* kGreetLines[] = {"嗨，今天人真多", "又见面啦", "辛苦辛苦", "吃了吗您", "今天天气不错"};
        const char* kQueueLines[] = {"怎么还不动啊", "前面快点啊…", "早知道走自助了", "要赶不上了吧", "唉，排到天黑"};
        const char* kBoardingLines[] = {"该登机了！", "走走走，登机去", "终于登机了", "别忘了登机牌"};
        const char* kShopLines[] = {"来杯咖啡提提神", "买点路上吃的", "这个挺有意思", "候机就得逛逛"};
        const char* kSitLines[] = {"坐会儿歇歇脚", "还有点时间", "刷会儿手机", "眯一会儿"};
        const char* kStaffIdleLines[] = {"今天客流还行", "整理一下台面", "喝口水先", "下一位～"};
        const char* kClerkGreetLines[] = {"欢迎光临～", "随便看看哈", "这款卖得最好", "需要帮忙喊我"};
        const char* kNightLines[] = {"夜里真安静", "再巡一圈", "地拖完就收工", "哼哼小曲～"};

        const char* Pick(std::mt19937& rng, const char* const* lines, size_t count)
        {
            return lines[rng() % count];
        }

        FDecisionResult ParseDecision(const std::string& text)
        {
            FDecisionResult result;
            const size_t open = text.find('{');
            const size_t close = text.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return result;
            }
            try
            {
                const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
                result.action = json.value("action", std::string("idle"));
                result.target = json.value("target", std::string());
                result.say = json.value("say", std::string());
                result.mood = MoodFromString(json.value("mood", std::string("neutral")));
                result.valid = true;
            }
            catch (...)
            {
                result.valid = false;
            }
            return result;
        }

        bool IsEligible(const FAgent& agent, double gameMinutes)
        {
            if (!agent.active || agent.decisionPending || gameMinutes < agent.nextDecisionAt)
            {
                return false;
            }
            if (!agent.eventNote.empty())
            {
                return true;
            }
            if (agent.role == EAgentRole::Passenger)
            {
                return agent.pstate == EPassengerState::AirsideIdle || agent.pstate == EPassengerState::AirsideUse;
            }
            return agent.sstate == EStaffState::OnDuty || agent.sstate == EStaffState::Patrol;
        }

        std::string BuildPrompt(const FAgent& agent, double gameMinutes, bool isNight, AgentSystem& agents,
                                const AirportMap& map)
        {
            int hh = 0, mm = 0;
            MinutesToHHMM(gameMinutes, hh, mm);

            // 周边感知列表（≤5 个邻居）。
            std::string neighbors;
            int neighborCount = 0;
            for (const auto& other : agents.Agents())
            {
                if (!other.active || other.id == agent.id || neighborCount >= 5)
                {
                    continue;
                }
                const glm::vec3 d = agent.position - other.position;
                if (glm::length(glm::vec2(d.x, d.z)) > Config::kNeighborRadius)
                {
                    continue;
                }
                neighbors += fmt::format("{}（{}）", other.name, RoleLabelZh(other.role));
                neighbors += ' ';
                ++neighborCount;
            }
            if (neighbors.empty())
            {
                neighbors = "无";
            }

            // 白名单动作集（由 Layer 0 按当前状态给出，§5.3）。
            std::string actions;
            std::string poiList;
            const bool isAirsidePassenger = agent.role == EAgentRole::Passenger;
            if (isAirsidePassenger)
            {
                actions = "goto（去一个空侧点位）/ say_to / emote / idle";
                for (const auto& poi : map.Points())
                {
                    if (JourneySystem::IsAirsideLeisureCategory(poi.category))
                    {
                        poiList += poi.name;
                        poiList += ' ';
                    }
                }
            }
            else
            {
                actions = "say_to（对身边人说话）/ emote / idle";
                poiList = "（你在岗位上，不能离岗）";
            }

            std::string situation;
            if (agent.role == EAgentRole::Passenger)
            {
                const bool browsing = agent.pstate == EPassengerState::AirsideUse && !agent.targetPoi.empty() &&
                                      agent.seatPoi.empty();
                situation = fmt::format("你已过安检在候机区自由活动，正在 {}。",
                                        agent.targetPoi.empty() ? "闲逛" : agent.targetPoi);
                if (browsing)
                {
                    situation += "你在逛店，可以对商品、价格、旅途发一句感想。";
                }
            }
            else
            {
                situation = fmt::format("你在岗位 {} 上{}。", agent.postPoi,
                                        agent.sstate == EStaffState::Patrol ? "巡逻" : "值班");
            }

            std::string eventLine;
            if (!agent.eventNote.empty())
            {
                const bool isIncomingChat = agent.eventNote.find("对你说") != std::string::npos;
                eventLine = fmt::format("刚刚发生：{}。{}\n", agent.eventNote,
                                        isIncomingChat ? "简短回应一句即可，回应完话题就结束，不要反问、不要再起新话头。"
                                                       : "请针对这件事反应。");
            }

            // 对话记忆：把自己最近说过的话和上个聊天对象喂回去，防止复读和缠着同一个人聊。
            std::string chatMemory;
            if (!agent.recentSpeech.empty())
            {
                chatMemory = "你最近说过：";
                for (const auto& line : agent.recentSpeech)
                {
                    chatMemory += fmt::format("「{}」", line);
                }
                if (!agent.lastChatWith.empty())
                {
                    chatMemory += fmt::format("，上一个聊天对象是{}", agent.lastChatWith);
                }
                chatMemory += "。不要重复类似的话，也不要反复找同一个人聊。\n";
            }

            return fmt::format(
                "你是机场里的{}「{}」，性格：{}。现在是{:02d}:{:02d}，{}。\n"
                "{}{}{}"
                "你周围的人：{}\n"
                "你可选动作：{}\n"
                "可去点位：{}\n"
                "决定接下来做一件事。{}"
                "刚和人聊过就让话题告一段落，选 emote 或 idle。只输出一个JSON对象，不要解释：\n"
                "{{\"action\":\"goto|say_to|emote|idle\",\"target\":\"<点位名或人名，可空>\","
                "\"say\":\"<不超过20字的台词，可空>\",\"mood\":\"neutral|happy|tired|annoyed|excited|anxious\"}}",
                RoleLabelZh(agent.role), agent.name, agent.personality, hh, mm, isNight ? "夜深人静" : "航站楼运转中",
                situation, eventLine, chatMemory, neighbors, actions, poiList,
                IsChattyPersonality(agent.personality)
                    ? "你性格外向，看到什么新鲜事都乐意聊一句（自言自语的感想也行），但每次要说不一样的话。"
                    : (IsQuietPersonality(agent.personality)
                           ? "你性格安静：大部分时候默默做自己的事（say 留空），只有值得回应的事才简短开口。"
                           : "有贴合你职业和眼前情境的新鲜话就说一句，没有就安静做事（say 留空）。"));
        }
    }

    void DecisionScheduler::PushLog(std::string line)
    {
        log_.push_back(std::move(line));
        if (log_.size() > kLogLimit)
        {
            log_.erase(log_.begin(), log_.begin() + static_cast<long long>(log_.size() - kLogLimit));
        }
    }

    void DecisionScheduler::Tick(double gameMinutes, NextAI::FAIService* ai, AgentSystem& agents, AirportMap& map,
                                 JourneySystem& journey, bool isNight)
    {
        // 1. 排空 worker 完成的决策，主线程 apply。
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pending : completed_)
            {
                if (FAgent* agent = agents.FindById(pending.agentId))
                {
                    if (pending.result.valid)
                    {
                        ApplyResult(*agent, pending.result, gameMinutes, agents, map, journey);
                    }
                    else
                    {
                        ApplyFallback(*agent, gameMinutes, agents, map, journey, isNight);
                    }
                    agent->decisionPending = false;
                }
                if (pending.agentId == inFlightAgentId_)
                {
                    inFlight_ = false;
                    inFlightAgentId_ = -1;
                }
            }
            completed_.clear();
        }

        // 超时弃单 → fallback 兜底（§10 风险表）。
        if (inFlight_ && gameMinutes - inFlightSince_ > kLlmTimeoutGameMinutes)
        {
            ++generation_; // 迟到的结果作废
            if (FAgent* agent = agents.FindById(inFlightAgentId_))
            {
                ApplyFallback(*agent, gameMinutes, agents, map, journey, isNight);
                agent->decisionPending = false;
            }
            inFlight_ = false;
            inFlightAgentId_ = -1;
            PushLog("[超时] LLM 请求弃单，走 fallback");
        }

        if (inFlight_)
        {
            return;
        }

        // 2. 选下一个决策对象：感知事件优先，其次空闲冷却到期。
        // 从轮询游标开始扫描，保证串行预算在全场角色间公平分配——
        // 否则池下标小、又互为邻居的固定岗（值机柜台）会垄断所有决策时刻。
        auto& all = agents.Agents();
        const size_t count = all.size();
        FAgent* chosen = nullptr;
        FAgent* chosenIdle = nullptr;
        for (size_t k = 0; k < count; ++k)
        {
            FAgent& agent = all[(scanCursor_ + k) % count];
            if (!IsEligible(agent, gameMinutes))
            {
                continue;
            }
            if (!agent.eventNote.empty())
            {
                chosen = &agent;
                break;
            }
            if (chosenIdle == nullptr)
            {
                chosenIdle = &agent;
            }
        }
        if (chosen == nullptr)
        {
            chosen = chosenIdle;
        }
        if (chosen == nullptr)
        {
            return;
        }
        // 游标推进到被选者之后，下一轮从别人开始。
        for (size_t k = 0; k < count; ++k)
        {
            if (&all[k] == chosen)
            {
                scanCursor_ = (k + 1) % count;
                break;
            }
        }

        chosen->nextDecisionAt = gameMinutes + DecisionCooldownFor(*chosen);
        ++decisionsMade_;

        if (ai == nullptr)
        {
            ApplyFallback(*chosen, gameMinutes, agents, map, journey, isNight);
            return;
        }

        const std::string prompt = BuildPrompt(*chosen, gameMinutes, isNight, agents, map);
        chosen->decisionPending = true;
        inFlight_ = true;
        inFlightSince_ = gameMinutes;
        inFlightAgentId_ = chosen->id;

        const int agentId = chosen->id;
        const uint64_t generation = generation_;
        ai->GenerateTextAsync(prompt,
                              [this, agentId, generation](NextAI::FAIResponse response)
                              {
                                  // Worker 线程：只解析 + 入队，绝不碰 Scene/agents（§7.4）。
                                  FDecisionResult result =
                                      ParseDecision(response.success ? response.text : std::string());
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  completed_.push_back({agentId, result});
                              });
    }

    void DecisionScheduler::ApplyResult(FAgent& agent, const FDecisionResult& result, double gameMinutes,
                                        AgentSystem& agents, AirportMap& map, JourneySystem& journey)
    {
        agent.mood = result.mood;
        const std::string incoming = agent.eventNote; // 本次决策的触发事件
        agent.eventNote.clear();

        if (!result.say.empty())
        {
            agent.bubbleText = result.say;
            agent.bubbleUntil = gameMinutes + Config::kBubbleDurationMinutes;
            // 记住自己最近说过的话，喂回 prompt 防复读。
            agent.recentSpeech.push_back(result.say);
            while (agent.recentSpeech.size() > 3)
            {
                agent.recentSpeech.erase(agent.recentSpeech.begin());
            }
        }

        if ((result.action == "goto" || result.action == "use_poi") && agent.role == EAgentRole::Passenger)
        {
            agent.chatChain = 0;
            journey.ApplyAirsideChoice(agent, result.target, agents, map);
        }
        else if (result.action == "say_to" && !result.target.empty() && !result.say.empty())
        {
            // 对话链约束：被搭话后的回话继承链深，主动开启新话题清零。
            // 一来一回（链深 < kMaxChatChain）允许对方插队回应；超过后不再重置对方冷却，
            // 话题自然收尾——否则相邻固定岗会乒乓对话，垄断串行 LLM 预算，全场其他人失声。
            const bool isReply = incoming.find("对你说") != std::string::npos;
            if (!isReply)
            {
                agent.chatChain = 0;
            }
            agent.lastChatWith = result.target;
            for (auto& other : agents.Agents())
            {
                if (other.active && other.id != agent.id && other.name == result.target && other.eventNote.empty())
                {
                    other.eventNote = fmt::format("{}对你说「{}」", agent.name, result.say);
                    other.chatChain = agent.chatChain + 1;
                    other.lastChatWith = agent.name;
                    if (other.chatChain < kMaxChatChain)
                    {
                        other.nextDecisionAt = gameMinutes; // 插队回应
                    }
                    break;
                }
            }
        }
        else
        {
            agent.chatChain = 0; // 非对话动作 = 话题结束
        }

        PushLog(fmt::format("[LLM] {}: {} {} 「{}」({})", agent.name, result.action, result.target, result.say,
                            MoodName(result.mood)));
        SPDLOG_INFO("AirportSim/LLM {}: action={} target='{}' say='{}' mood={}", agent.name, result.action,
                    result.target, result.say, MoodName(result.mood));
    }

    void DecisionScheduler::ApplyFallback(FAgent& agent, double gameMinutes, AgentSystem& agents, AirportMap& map,
                                          JourneySystem& journey, bool isNight)
    {
        ++fallbacksUsed_;
        const char* line = nullptr;
        EMood mood = EMood::Neutral;

        if (agent.eventNote.find("顾客") != std::string::npos)
        {
            line = Pick(rng_, kClerkGreetLines, std::size(kClerkGreetLines));
            mood = EMood::Happy;
        }
        else if (agent.eventNote.find("长队") != std::string::npos)
        {
            line = Pick(rng_, kQueueLines, std::size(kQueueLines));
            mood = EMood::Annoyed;
        }
        else if (agent.eventNote.find("登机") != std::string::npos)
        {
            line = Pick(rng_, kBoardingLines, std::size(kBoardingLines));
            mood = EMood::Excited;
        }
        else if (agent.eventNote.find("遇到") != std::string::npos ||
                 agent.eventNote.find("对你说") != std::string::npos)
        {
            line = Pick(rng_, kGreetLines, std::size(kGreetLines));
            mood = EMood::Happy;
        }
        else if (isNight)
        {
            line = Pick(rng_, kNightLines, std::size(kNightLines));
            mood = EMood::Tired;
        }
        else if (agent.role != EAgentRole::Passenger)
        {
            line = Pick(rng_, kStaffIdleLines, std::size(kStaffIdleLines));
        }
        else if (agent.pstate == EPassengerState::AirsideUse)
        {
            line = agent.seatPoi.empty() ? Pick(rng_, kShopLines, std::size(kShopLines))
                                         : Pick(rng_, kSitLines, std::size(kSitLines));
        }

        agent.eventNote.clear();
        agent.chatChain = 0; // 规则 fallback 不发起对话链
        agent.mood = mood;
        if (line != nullptr)
        {
            agent.bubbleText = line;
            agent.bubbleUntil = gameMinutes + Config::kBubbleDurationMinutes;
            agent.recentSpeech.push_back(line);
            while (agent.recentSpeech.size() > 3)
            {
                agent.recentSpeech.erase(agent.recentSpeech.begin());
            }
        }
        PushLog(fmt::format("[规则] {}: 「{}」({})", agent.name, line != nullptr ? line : "", MoodName(mood)));
    }

    void DecisionScheduler::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        completed_.clear();
        inFlight_ = false;
        inFlightAgentId_ = -1;
        decisionsMade_ = 0;
        fallbacksUsed_ = 0;
        log_.clear();
    }
}
