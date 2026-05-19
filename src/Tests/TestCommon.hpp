#pragma once

#include <catch2/catch_all.hpp>
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Options.hpp"
#include <memory>

// Common Test Game Instance that does nothing but allows the engine to run
class TestGameInstance : public NextGameInstanceBase
{
public:
    using NextGameInstanceBase::NextGameInstanceBase;
    void OnInit() override {}
    void OnTick(double deltaSeconds) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }
};

// Fixture for integration tests requiring the full engine
class EngineTestFixture
{
protected:
    std::unique_ptr<Runtime::Config::Options> options_;
    std::unique_ptr<NextEngine> engine_;

public:
    EngineTestFixture();
    virtual ~EngineTestFixture();

    void Simulate(int frames);
};
