#include <iostream>
#include <boost/program_options.hpp>

#include "../Utilities/FileHelper.hpp"
#include "../Utilities/Console.hpp"

using namespace boost::program_options;

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
        options_description desc("Application options", lineLength);
        desc.add_options()
            ("help", "Display help message.")
            ("out", value<std::string>(&PakPath)->default_value("out.pak"), "abs path")
            ("src", value<std::string>(&SrcPath)->default_value("assets"), "based project root path, like assets/textures")
            ("regex", value<std::string>(&Regex)->default_value(""), "if not empty, only pak files match the regex will be packed.")
            ;
        
        const positional_options_description positional;
        variables_map vm;
        store(command_line_parser(argc, argv).options(desc).positional(positional).run(), vm);
        notify(vm);

        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return EXIT_SUCCESS;
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
