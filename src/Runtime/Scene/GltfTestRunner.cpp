#include "Runtime/Scene/GltfTestRunner.hpp"
#include "Runtime/Engine.hpp"
#include "Options.hpp"
#include "Utilities/FileHelper.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

GltfTestRunner::GltfTestRunner(NextEngine* engine) : engine_(engine)
{
    cacheDir_ = "../cache/gltf_samples/";
    if (!std::filesystem::exists(cacheDir_))
    {
        std::filesystem::create_directories(cacheDir_);
    }
}

GltfTestRunner::~GltfTestRunner()
{
}

void GltfTestRunner::FetchModelList()
{
    std::string url = "https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models";
    std::string readBuffer;

    CURL* curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "gkNextRenderer-TestRunner");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        
        // GitHub API requires User-Agent
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, "Accept: application/vnd.github.v3+json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            SPDLOG_ERROR("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        }
        else
        {
            try
            {
                auto j = json::parse(readBuffer);
                if (j.is_array())
                {
                    for (const auto& item : j)
                    {
                        if (item["type"] == "dir")
                        {
                            std::string name = item["name"];
                            if (GOption->TestGltfFilter.empty() || name.find(GOption->TestGltfFilter) != std::string::npos)
                            {
                                modelList_.push_back(name);
                            }
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                SPDLOG_ERROR("JSON parse error: {}", e.what());
            }
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(chunk);
    }
    
    SPDLOG_INFO("Found {} models for robustness test.", modelList_.size());
}

bool GltfTestRunner::DownloadModel(const std::string& modelName, std::string& outPath)
{
    // Only support .glb from glTF-Binary for robustness test
    std::string glbFilename = modelName + ".glb";
    std::string localGlbPath = cacheDir_ + modelName + "/" + glbFilename;
    std::string url = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/" + modelName + "/glTF-Binary/" + glbFilename;

    // Check cache
    if (std::filesystem::exists(localGlbPath))
    {
        outPath = std::filesystem::absolute(localGlbPath).string();
        return true;
    }
    
    // Ensure dir
    std::filesystem::create_directories(cacheDir_ + modelName);

    // Try GLB Download
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(localGlbPath.c_str(), "wb");
    if (!fp)
    {
        curl_easy_cleanup(curl);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // Fail on 404

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK)
    {
        outPath = std::filesystem::absolute(localGlbPath).string();
        return true;
    }
    else
    {
        std::filesystem::remove(localGlbPath);
        SPDLOG_WARN("Model '{}' does not have a glTF-Binary (.glb) version. Skipping.", modelName);
        return false;
    }
}

bool GltfTestRunner::Update(double deltaSeconds)
{
    switch (currentState_)
    {
    case State::Init:
        FetchModelList();
        currentState_ = State::Downloading;
        break;

    case State::Downloading:
        if (currentModelIndex_ >= modelList_.size())
        {
            currentState_ = State::Finished;
            return true; // Remove task
        }
        {
            std::string path;
            std::string name = modelList_[currentModelIndex_];
            SPDLOG_INFO("Preparing model {}/{} : {}", currentModelIndex_ + 1, modelList_.size(), name);
            
            if (DownloadModel(name, path))
            {
                engine_->RequestLoadScene(path);
                currentState_ = State::Loading;
            }
            else
            {
                SPDLOG_ERROR("Skipping {} (Download failed or no GLB)", name);
                currentModelIndex_++;
                // Stay in Downloading to try next
            }
        }
        break;

    case State::Loading:
        if (engine_->GetEngineStatus() == NextRenderer::EApplicationStatus::Running)
        {
            renderTimer_ = 0.0f;
            currentState_ = State::Rendering;
        }
        break;

    case State::Rendering:
        renderTimer_ += (float)deltaSeconds;
        if (renderTimer_ > renderTimePerModel_)
        {
            currentModelIndex_++;
            currentState_ = State::Downloading;
        }
        break;
        
    case State::Finished:
        return true;
    }

    return false;
}
