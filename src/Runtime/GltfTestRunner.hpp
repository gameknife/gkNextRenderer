#pragma once

#include "Common/CoreMinimal.hpp"
#include <vector>
#include <string>

class NextEngine;

class GltfTestRunner
{
public:
    GltfTestRunner(NextEngine* engine);
    ~GltfTestRunner();

    bool Update(double deltaSeconds);

private:
    void FetchModelList();
    bool DownloadModel(const std::string& modelName, std::string& outPath);

    NextEngine* engine_;
    
    enum class State
    {
        Init,
        Downloading,
        Loading,
        Rendering,
        Finished
    };

    State currentState_ = State::Init;
    std::vector<std::string> modelList_;
    size_t currentModelIndex_ = 0;
    
    float renderTimer_ = 0.0f;
    const float renderTimePerModel_ = 2.0f; // Render each model for 2 seconds

    std::string cacheDir_;
};
