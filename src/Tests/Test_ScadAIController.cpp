#include <catch2/catch_test_macros.hpp>

#include "AI/ScadAIController.hpp"
#include "AI/ScadAIValidationPolicy.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using namespace ScadLibrary::AI;

namespace
{
    class FFakeScadAITransport final : public IScadAITransport
    {
    public:
        bool LoadConfiguration(FScadAITransportConfiguration& outConfiguration,
                               std::string& outError) override
        {
            std::lock_guard lock(mutex);
            outConfiguration = configuration;
            outError.clear();
            return true;
        }

        bool SelectProvider(const std::string& providerId,
                            FScadAITransportConfiguration& outConfiguration,
                            std::string& outError) override
        {
            std::lock_guard lock(mutex);
            const auto found = std::find_if(
                configuration.providers.begin(), configuration.providers.end(),
                [&providerId](const FScadAIProviderOption& provider) { return provider.id == providerId; });
            if (found == configuration.providers.end() || !found->configured || !found->available)
            {
                outError = "provider unavailable";
                outConfiguration = configuration;
                return false;
            }
            configuration.currentProviderId = providerId;
            configuration.currentModelId = found->models.empty() ? "" : found->models.front();
            outConfiguration = configuration;
            outError.clear();
            return true;
        }

        bool SelectModel(const std::string& modelId,
                         FScadAITransportConfiguration& outConfiguration,
                         std::string& outError) override
        {
            std::lock_guard lock(mutex);
            const auto provider = std::find_if(
                configuration.providers.begin(), configuration.providers.end(),
                [this](const FScadAIProviderOption& item)
                { return item.id == configuration.currentProviderId; });
            if (provider == configuration.providers.end() ||
                std::find(provider->models.begin(), provider->models.end(), modelId) == provider->models.end())
            {
                outError = "model unavailable";
                outConfiguration = configuration;
                return false;
            }
            configuration.currentModelId = modelId;
            outConfiguration = configuration;
            outError.clear();
            return true;
        }

        NextAI::FChatResponse Complete(const NextAI::FChatRequest& request,
                                       NextAI::FChatStreamCallback onDelta) override
        {
            std::unique_lock lock(mutex);
            ++calls;
            runIds.push_back(request.runId);
            strictSchemas.push_back(request.strictSchema);
            deadlines.push_back(request.deadlineMs);
            condition.notify_all();
            if (block)
            {
                condition.wait(lock, [&] { return cancelled; });
                return NextAI::FChatResponse::Failure("cancelled");
            }
            REQUIRE_FALSE(responses.empty());
            NextAI::FChatResponse response = std::move(responses.front());
            responses.pop_front();
            lock.unlock();
            if (onDelta && response.success)
            {
                onDelta(response.content.substr(0, std::min<size_t>(4, response.content.size())));
            }
            return response;
        }

        bool Cancel(const std::string& runId) override
        {
            std::lock_guard lock(mutex);
            cancelled = true;
            cancelledRunId = runId;
            condition.notify_all();
            return true;
        }

        bool WaitForCalls(int expected)
        {
            std::unique_lock lock(mutex);
            return condition.wait_for(lock, std::chrono::seconds(2), [&] { return calls >= expected; });
        }

        std::mutex mutex;
        std::condition_variable condition;
        std::deque<NextAI::FChatResponse> responses;
        std::vector<std::string> runIds;
        std::vector<bool> strictSchemas;
        std::vector<int> deadlines;
        std::string cancelledRunId;
        int calls = 0;
        bool block = false;
        bool cancelled = false;
        FScadAITransportConfiguration configuration{
            {{"first", "First", {"first-default"}, true, true},
             {"second", "Second", {"second-default", "second-large"}, true, true},
             {"offline", "Offline", {"offline-default"}, true, false}},
            "first", "first-default", "ready"};
    };

    FScadAIRequestEnvelope MakeRequest()
    {
        FScadAIRequestEnvelope request;
        request.requestId = "test-run";
        request.target = {EScadAIEditKind::SceneSource, "draft:test", "test", "", {}};
        request.baseRevision = {1, 2};
        request.instruction = "make it taller";
        request.systemPrompt = "return json";
        request.snapshot = {{"source", "cube(1);"}};
        request.schemaName = "test";
        request.jsonSchema = R"({"type":"object"})";
        return request;
    }

    FScadAIValidationResult ValidateOk(std::string_view response)
    {
        FScadAIValidationResult result;
        try
        {
            result.artifact = nlohmann::json::parse(response);
            result.candidate = result.artifact;
            result.summary = "ok";
            result.success = true;
        }
        catch (...)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "json", "bad json"});
        }
        return result;
    }

    FScadAIControllerSnapshot WaitForTerminal(FScadAIController& controller)
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            FScadAIControllerSnapshot snapshot = controller.Snapshot();
            if (snapshot.state != EScadAIProposalState::Generating &&
                snapshot.state != EScadAIProposalState::Validating)
            {
                return snapshot;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        FAIL("controller did not reach a terminal state");
        return {};
    }

    std::filesystem::path TestHistoryRoot()
    {
        return std::filesystem::temp_directory_path() / "gknext_scadai_controller_tests";
    }
}

TEST_CASE("Scad AI canonical revision detects stale proposals", "[AI][ScadLibrary][Controller]")
{
    const nlohmann::json first = {{"b", 2}, {"a", 1}};
    const nlohmann::json same = {{"a", 1}, {"b", 2}};
    REQUIRE(HashCanonicalSnapshot(first) == HashCanonicalSnapshot(same));

    FScadAIProposal proposal;
    proposal.target = {EScadAIEditKind::SceneSource, "draft:test", "test", "", {}};
    proposal.baseRevision = {4, HashCanonicalSnapshot(first)};
    REQUIRE(IsProposalCurrent(proposal, proposal.target, proposal.baseRevision));
    REQUIRE_FALSE(IsProposalCurrent(proposal, proposal.target, {5, proposal.baseRevision.contentHash}));
    REQUIRE_FALSE(IsProposalCurrent(proposal, proposal.target,
                                    {4, HashCanonicalSnapshot(nlohmann::json{{"a", 2}})}));
}

TEST_CASE("Scad AI controller repairs exactly once", "[AI][ScadLibrary][Controller]")
{
    auto fake = std::make_unique<FFakeScadAITransport>();
    FFakeScadAITransport* transport = fake.get();
    transport->responses.push_back(NextAI::FChatResponse::Success("not json"));
    transport->responses.push_back(NextAI::FChatResponse::Success(R"({"value":2})"));
    FScadAIController controller(std::move(fake), TestHistoryRoot());
    REQUIRE(controller.Submit(MakeRequest(), ValidateOk));
    const FScadAIControllerSnapshot snapshot = WaitForTerminal(controller);
    REQUIRE(snapshot.state == EScadAIProposalState::Ready);
    REQUIRE(snapshot.proposal);
    REQUIRE(snapshot.proposal->repairCount == 1);
    REQUIRE(transport->calls == 2);
    REQUIRE(transport->runIds[0] == "test-run");
    REQUIRE(transport->runIds[1] == "test-run-repair-1");
    REQUIRE(transport->strictSchemas[0]);
    REQUIRE(transport->deadlines[0] == FScadAIValidationPolicy::advancedModelDeadlineMs);
}

TEST_CASE("Scad AI controller stops after one failed repair", "[AI][ScadLibrary][Controller]")
{
    auto fake = std::make_unique<FFakeScadAITransport>();
    FFakeScadAITransport* transport = fake.get();
    transport->responses.push_back(NextAI::FChatResponse::Success("bad one"));
    transport->responses.push_back(NextAI::FChatResponse::Success("bad two"));
    FScadAIController controller(std::move(fake), TestHistoryRoot());
    REQUIRE(controller.Submit(MakeRequest(), ValidateOk));
    const FScadAIControllerSnapshot snapshot = WaitForTerminal(controller);
    REQUIRE(snapshot.state == EScadAIProposalState::Error);
    REQUIRE(snapshot.proposal);
    REQUIRE(snapshot.proposal->repairCount == 1);
    REQUIRE(transport->calls == 2);
}

TEST_CASE("Scad AI controller cancels the active bridge run", "[AI][ScadLibrary][Controller]")
{
    auto fake = std::make_unique<FFakeScadAITransport>();
    FFakeScadAITransport* transport = fake.get();
    transport->block = true;
    FScadAIController controller(std::move(fake), TestHistoryRoot());
    REQUIRE(controller.Submit(MakeRequest(), ValidateOk));
    REQUIRE(transport->WaitForCalls(1));
    controller.Cancel();
    const FScadAIControllerSnapshot snapshot = WaitForTerminal(controller);
    REQUIRE(snapshot.state == EScadAIProposalState::Cancelled);
    REQUIRE(transport->cancelledRunId == "test-run");
    REQUIRE_FALSE(snapshot.proposal);
}

TEST_CASE("Scad AI controller selects provider and model only while idle",
          "[AI][ScadLibrary][Controller]")
{
    auto fake = std::make_unique<FFakeScadAITransport>();
    FFakeScadAITransport* transport = fake.get();
    FScadAIController controller(std::move(fake), TestHistoryRoot());
    FScadAITransportConfiguration configuration;
    std::string error;

    REQUIRE(controller.LoadTransportConfiguration(configuration, error));
    REQUIRE(configuration.currentProviderId == "first");
    REQUIRE(controller.SelectProvider("second", configuration, error));
    REQUIRE(configuration.currentProviderId == "second");
    REQUIRE(configuration.currentModelId == "second-default");
    REQUIRE(controller.SelectModel("second-large", configuration, error));
    REQUIRE(configuration.currentModelId == "second-large");
    REQUIRE_FALSE(controller.SelectProvider("offline", configuration, error));

    transport->block = true;
    REQUIRE(controller.Submit(MakeRequest(), ValidateOk));
    REQUIRE(transport->WaitForCalls(1));
    REQUIRE_FALSE(controller.SelectProvider("first", configuration, error));
    REQUIRE(error == "生成期间不能切换 Provider");
    REQUIRE_FALSE(controller.SelectModel("second-default", configuration, error));
    REQUIRE(error == "生成期间不能切换模型");
    controller.Cancel();
    REQUIRE(WaitForTerminal(controller).state == EScadAIProposalState::Cancelled);
}

TEST_CASE("Scad AI ready proposal becomes stale without mutation", "[AI][ScadLibrary][Controller]")
{
    auto fake = std::make_unique<FFakeScadAITransport>();
    fake->responses.push_back(NextAI::FChatResponse::Success(R"({"value":2})"));
    FScadAIController controller(std::move(fake), TestHistoryRoot());
    const FScadAIRequestEnvelope request = MakeRequest();
    REQUIRE(controller.Submit(request, ValidateOk));
    REQUIRE(WaitForTerminal(controller).state == EScadAIProposalState::Ready);
    controller.RefreshIdentity(request.target, {2, request.baseRevision.contentHash});
    const auto snapshot = controller.Snapshot();
    REQUIRE(snapshot.state == EScadAIProposalState::Stale);
    REQUIRE(snapshot.proposal->candidate == nlohmann::json{{"value", 2}});
}
