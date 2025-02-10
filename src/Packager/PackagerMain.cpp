#include <iostream>
//#include <boost/program_options.hpp>
#include <cxxopts.hpp>
#include "../Utilities/FileHelper.hpp"
#include "../Utilities/Console.hpp"

//using namespace boost::program_options;

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        std::string PakPath;
        std::string SrcPath;
        std::string RootPath;
        std::string Regex;
                
        const int lineLength = 120;
        cxxopts::Options options("options", "");
        options.add_options()
            ("out", "abs path", cxxopts::value<std::string>(PakPath)->default_value("out.pak"))
            ("src", "based project root path, like assets/textures", cxxopts::value<std::string>(SrcPath)->default_value("assets"))
            ("regex", "if not empty, only pak files match the regex will be packed.", cxxopts::value<std::string>(Regex)->default_value(""))
            
            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        Utilities::Package::FPackageFileSystem PackageSystem(Utilities::Package::EPM_OsFile);
        PackageSystem.PakAll(PakPath, SrcPath, "", Regex);

        return EXIT_SUCCESS;
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
