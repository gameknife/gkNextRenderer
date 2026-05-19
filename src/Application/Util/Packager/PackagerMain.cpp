#include <iostream>
#include <cxxopts.hpp>
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Engine.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        std::string pakPath;
        std::string srcPath;
        std::string rootPath;
        std::string regex;
        std::string manifestPath;
        bool disableCompression = false;

        cxxopts::Options options("options", "");
        options.add_options()
            ("out", "abs path", cxxopts::value<std::string>(pakPath)->default_value("out.pak"))
            ("src", "based project root path, like assets/textures", cxxopts::value<std::string>(srcPath)->default_value("assets"))
            ("regex", "if not empty, only pak files match the regex will be packed.", cxxopts::value<std::string>(regex)->default_value(""))
            ("root", "root path used to compute relative entries, defaults to project root", cxxopts::value<std::string>(rootPath)->default_value(""))
            ("manifest", "optional manifest json path to record packed entries", cxxopts::value<std::string>(manifestPath)->default_value(""))
            ("no-compress", "store files without compression", cxxopts::value<bool>(disableCompression)->default_value("false")->implicit_value("true"))

            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        Utilities::Package::FPackageFileSystem packageSystem(Utilities::Package::EPM_OsFile);
        packageSystem.PakAll(pakPath, srcPath, rootPath, regex, !disableCompression, manifestPath);

        return EXIT_SUCCESS;
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
