#pragma once

#include <string>
#include <map>
#include <fstream>

namespace Utilities
{
    namespace Localization
    {
        inline std::map<std::string, std::string> locTexts;
        
        static void AddLocText( const char* srcText, const char* locText )
        {
            locTexts[srcText] = locText;
        }
        
        static const char* GetLocText( const char* srcText )
        {
            if(locTexts.find(srcText) != locTexts.end())
            {
                auto& locText = locTexts[srcText];
                return locText.empty() ? srcText : locText.c_str();
            }
            AddLocText(srcText, "");
            return srcText;
        }

        static void ReadLocTexts(const char* localeFilePath)
        {
            // Read from file
            std::ifstream file(Utilities::FileHelper::GetNormalizedFilePath(localeFilePath));
            if(file.is_open())
            {
                std::string line;
                while(std::getline(file, line))
                {
                    std::string srcText = line.substr(0, line.find(";"));
                    std::string locText = line.substr(line.find(";") + 1);
                    AddLocText(srcText.c_str(), locText.c_str());
                }
                file.close();
            }
        }

        static void SaveLocTexts(const char* localeFilePath)
        {
            // Write to file
            std::ofstream file(Utilities::FileHelper::GetNormalizedFilePath(localeFilePath));
            if(file.is_open())
            {
                for(auto& locText : locTexts)
                {
                    file << locText.first << ";" << locText.second << std::endl;
                }
                file.close();
            }
        }
    }

}


#define LOCTEXT(A) Utilities::Localization::GetLocText(A)
