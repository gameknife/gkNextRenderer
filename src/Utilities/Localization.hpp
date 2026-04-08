#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>

namespace Utilities
{
    namespace Localization
    {
        static std::map<std::string, std::string> locTexts;
        
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
            std::vector<uint8_t> fileData;
            Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(localeFilePath, fileData);

            if(!fileData.empty())
            {
                std::string content(fileData.begin(), fileData.end());
                if (content.size() >= 3 &&
                    static_cast<unsigned char>(content[0]) == 0xEF &&
                    static_cast<unsigned char>(content[1]) == 0xBB &&
                    static_cast<unsigned char>(content[2]) == 0xBF)
                {
                    content.erase(0, 3);
                }
                std::istringstream stream(content);
                std::string line;
                
                while(std::getline(stream, line))
                {
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }

                    size_t separatorPos = line.find(';');
                    if(separatorPos != std::string::npos)
                    {
                        AddLocText(line.substr(0, separatorPos).c_str(), 
                                   line.substr(separatorPos + 1).c_str());
                    }
                }
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
