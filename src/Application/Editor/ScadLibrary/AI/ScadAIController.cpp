#include "Engine/Common/CoreMinimal.hpp"
#include "ScadAIController.hpp"

#include "ScadAIValidationPolicy.hpp"

#include <nlohmann/json.hpp>

namespace ScadLibrary::AI
{
    namespace
    {
        std::string BuildUserMessage(const FScadAIRequestEnvelope& request)
        {
            nlohmann::json payload{
                {"instruction", request.instruction},
                {"target",
                 {
                     {"kind", EditKindName(request.target.kind)},
                     {"document", request.target.documentKey},
                     {"primaryId", request.target.primaryId},
                     {"secondaryIds", request.target.secondaryIds},
                 }},
                {"snapshot", request.snapshot},
            };
            return payload.dump(2);
        }

        std::string RepairMessage(const FScadAIValidationResult& validation)
        {
            nlohmann::json issues = nlohmann::json::array();
            for (const FScadAIValidationIssue& issue : validation.issues)
            {
                issues.push_back({{"code", issue.code}, {"message", issue.message}});
            }
            return "The previous artifact was rejected by deterministic local validation. "
                   "Return one corrected JSON artifact only. Validation errors:\n" +
                issues.dump(2);
        }
    } // namespace

    FScadAIController::FScadAIController(std::unique_ptr<IScadAITransport> transport,
                                         std::filesystem::path historyRoot) :
        transport_(std::move(transport)), historyStore_(std::move(historyRoot))
    {
    }

    FScadAIController::~FScadAIController()
    {
        Cancel();
        if (worker_.joinable())
        {
            worker_.request_stop();
            worker_.join();
        }
    }

    bool FScadAIController::Submit(FScadAIRequestEnvelope request, FScadAIArtifactValidator validator)
    {
        if (!transport_ || !validator || request.instruction.empty())
        {
            return false;
        }
        if (worker_.joinable())
        {
            if (IsGenerating())
            {
                return false;
            }
            worker_.join();
        }
        if (request.requestId.empty())
        {
            request.requestId = MakeScadAIRequestId();
        }
        {
            std::lock_guard lock(mutex_);
            activeRequestId_ = request.requestId;
            activeRunId_ = request.requestId;
            activeConversationKey_ = request.conversationKey;
            activeInstruction_ = request.instruction;
            streamText_.clear();
            statusMessage_ = "正在生成提案…";
            proposal_.reset();
            state_ = EScadAIProposalState::Generating;
        }
        cancelRequested_ = false;
        worker_ = std::jthread(
            [this, request = std::move(request), validator = std::move(validator)](std::stop_token)
            { Run(request, validator); });
        return true;
    }

    void FScadAIController::Run(FScadAIRequestEnvelope request, FScadAIArtifactValidator validator)
    {
        NextAI::FChatRequest chat;
        chat.messages.push_back(NextAI::FChatMessage::System(request.systemPrompt));
        for (const FScadAIHistoryEntry& entry : historyStore_.Load(request.conversationKey))
        {
            if (entry.role == "user")
            {
                chat.messages.push_back(NextAI::FChatMessage::User(entry.text));
            }
            else if (entry.role == "assistant")
            {
                chat.messages.push_back(NextAI::FChatMessage::Assistant(entry.text));
            }
        }
        chat.messages.push_back(NextAI::FChatMessage::User(BuildUserMessage(request)));
        chat.responseFormat = NextAI::FChatRequest::EResponseFormat::Schema;
        chat.responseSchemaName = request.schemaName;
        chat.jsonSchema = request.jsonSchema;
        chat.strictSchema = request.strictSchema;
        chat.stateless = true;
        chat.enableThinking = false;
        chat.maxTokens = 4096;
        chat.deadlineMs = FScadAIValidationPolicy::advancedModelDeadlineMs;

        for (int attempt = 0; attempt <= FScadAIValidationPolicy::repairBudget; ++attempt)
        {
            chat.runId = attempt == 0 ? request.requestId : fmt::format("{}-repair-{}", request.requestId, attempt);
            {
                std::lock_guard lock(mutex_);
                activeRunId_ = chat.runId;
                state_ = EScadAIProposalState::Generating;
                statusMessage_ = attempt == 0 ? "正在生成提案…" : "正在修复无效提案…";
                streamText_.clear();
            }
            const std::string runId = chat.runId;
            NextAI::FChatResponse response = transport_->Complete(
                chat, [this, runId](const std::string& delta)
                {
                    std::lock_guard lock(mutex_);
                    if (!cancelRequested_ && activeRunId_ == runId)
                    {
                        streamText_ += delta;
                    }
                });
            if (cancelRequested_)
            {
                SetTerminalState(EScadAIProposalState::Cancelled, "请求已取消");
                return;
            }
            if (!response.success)
            {
                SetTerminalState(EScadAIProposalState::Error, response.errorMessage);
                return;
            }
            {
                std::lock_guard lock(mutex_);
                state_ = EScadAIProposalState::Validating;
                statusMessage_ = "正在执行本地校验…";
            }
            FScadAIValidationResult validation = validator(response.content);
            if (validation.success)
            {
                FScadAIProposal proposal;
                proposal.requestId = request.requestId;
                proposal.target = request.target;
                proposal.baseRevision = request.baseRevision;
                proposal.state = EScadAIProposalState::Ready;
                proposal.summary = std::move(validation.summary);
                proposal.rawResponse = std::move(response.content);
                proposal.artifact = std::move(validation.artifact);
                proposal.candidate = std::move(validation.candidate);
                proposal.semanticDiff = std::move(validation.semanticDiff);
                proposal.issues = std::move(validation.issues);
                proposal.repairCount = attempt;
                const std::string proposalSummary = proposal.summary;
                bool accepted = false;
                {
                    std::lock_guard lock(mutex_);
                    if (!cancelRequested_ && activeRequestId_ == request.requestId)
                    {
                        proposal_ = std::move(proposal);
                        state_ = EScadAIProposalState::Ready;
                        statusMessage_ = "提案已通过本地校验";
                        accepted = true;
                    }
                }
                if (accepted)
                {
                    std::string historyError;
                    historyStore_.Append(request.conversationKey, {"user", request.instruction, "submitted"},
                                         historyError);
                    historyStore_.Append(request.conversationKey, {"assistant", proposalSummary, "proposed"},
                                         historyError);
                }
                return;
            }
            if (attempt == FScadAIValidationPolicy::repairBudget)
            {
                FScadAIProposal proposal;
                proposal.requestId = request.requestId;
                proposal.target = request.target;
                proposal.baseRevision = request.baseRevision;
                proposal.state = EScadAIProposalState::Error;
                proposal.rawResponse = response.content;
                proposal.issues = std::move(validation.issues);
                proposal.repairCount = attempt;
                std::lock_guard lock(mutex_);
                proposal_ = std::move(proposal);
                state_ = EScadAIProposalState::Error;
                statusMessage_ = "提案在一次修复后仍未通过校验";
                return;
            }
            chat.messages.push_back(NextAI::FChatMessage::Assistant(response.content));
            chat.messages.push_back(NextAI::FChatMessage::User(RepairMessage(validation)));
        }
    }

    void FScadAIController::Cancel()
    {
        std::string runId;
        {
            std::lock_guard lock(mutex_);
            if (state_ != EScadAIProposalState::Generating && state_ != EScadAIProposalState::Validating)
            {
                return;
            }
            cancelRequested_ = true;
            runId = activeRunId_;
        }
        transport_->Cancel(runId);
    }

    void FScadAIController::Reject()
    {
        std::lock_guard lock(mutex_);
        if (proposal_)
        {
            proposal_->state = EScadAIProposalState::Rejected;
            state_ = EScadAIProposalState::Rejected;
            statusMessage_ = "提案已拒绝";
            std::string historyError;
            historyStore_.Append(activeConversationKey_,
                                 {"assistant", proposal_->summary, "rejected"}, historyError);
        }
    }

    void FScadAIController::Reset()
    {
        Cancel();
        std::lock_guard lock(mutex_);
        proposal_.reset();
        streamText_.clear();
        statusMessage_.clear();
        state_ = EScadAIProposalState::Idle;
    }

    void FScadAIController::MarkApplied()
    {
        std::lock_guard lock(mutex_);
        if (proposal_ && proposal_->state == EScadAIProposalState::Ready)
        {
            proposal_->state = EScadAIProposalState::Applied;
            state_ = EScadAIProposalState::Applied;
            statusMessage_ = "提案已应用到未保存草稿";
            std::string historyError;
            historyStore_.Append(activeConversationKey_,
                                 {"assistant", proposal_->summary, "accepted"}, historyError);
        }
    }

    void FScadAIController::RefreshIdentity(const FScadAIEditTarget& target,
                                            const FScadDocumentRevision& revision)
    {
        std::lock_guard lock(mutex_);
        if (proposal_ && proposal_->state == EScadAIProposalState::Ready &&
            !IsProposalCurrent(*proposal_, target, revision))
        {
            proposal_->state = EScadAIProposalState::Stale;
            state_ = EScadAIProposalState::Stale;
            statusMessage_ = "文档已变化，请基于当前内容重新生成";
        }
    }

    bool FScadAIController::LoadTransportConfiguration(FScadAITransportConfiguration& outConfiguration,
                                                        std::string& outError)
    {
        if (IsGenerating())
        {
            outError = "生成期间不能刷新 Provider 配置";
            return false;
        }
        if (!transport_)
        {
            outError = "AI transport 不可用";
            return false;
        }
        return transport_->LoadConfiguration(outConfiguration, outError);
    }

    bool FScadAIController::SelectProvider(const std::string& providerId,
                                            FScadAITransportConfiguration& outConfiguration,
                                            std::string& outError)
    {
        if (IsGenerating())
        {
            outError = "生成期间不能切换 Provider";
            return false;
        }
        if (!transport_)
        {
            outError = "AI transport 不可用";
            return false;
        }
        return transport_->SelectProvider(providerId, outConfiguration, outError);
    }

    bool FScadAIController::SelectModel(const std::string& modelId,
                                         FScadAITransportConfiguration& outConfiguration,
                                         std::string& outError)
    {
        if (IsGenerating())
        {
            outError = "生成期间不能切换模型";
            return false;
        }
        if (!transport_)
        {
            outError = "AI transport 不可用";
            return false;
        }
        return transport_->SelectModel(modelId, outConfiguration, outError);
    }

    bool FScadAIController::IsGenerating() const
    {
        std::lock_guard lock(mutex_);
        return state_ == EScadAIProposalState::Generating || state_ == EScadAIProposalState::Validating;
    }

    FScadAIControllerSnapshot FScadAIController::Snapshot() const
    {
        std::lock_guard lock(mutex_);
        return {state_, streamText_, statusMessage_, proposal_};
    }

    void FScadAIController::SetTerminalState(EScadAIProposalState state, std::string message)
    {
        std::lock_guard lock(mutex_);
        state_ = state;
        statusMessage_ = std::move(message);
    }
} // namespace ScadLibrary::AI
