#include "Assets/Loaders/FLDrawParser.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Assets
{
    static std::string ToLowerStr(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    LDrawParser::LDrawParser(LDrawColorTable& colors, LDrawFileResolver& resolver)
        : colors_(colors), resolver_(resolver)
    {
    }

    int LDrawParser::ResolveColor(int faceColor, int parentColor) const
    {
        if (faceColor == 16)
            return (parentColor == 16) ? kLDrawColorInherit : parentColor;
        if (faceColor == 24)
            return kLDrawColorInherit;
        return faceColor;
    }

    void LDrawParser::SetMPDSubfiles(std::unordered_map<std::string, std::string> subfiles)
    {
        mpdSubfiles_ = std::move(subfiles);
    }

    void LDrawParser::ClearMPDSubfiles()
    {
        mpdSubfiles_.clear();
    }

    bool LDrawParser::HasMPDSubfile(const std::string& name) const
    {
        return mpdSubfiles_.count(ToLowerStr(name)) > 0;
    }

    LDrawPartTemplate LDrawParser::ParseFile(const std::string& filepath)
    {
        std::string key = ToLowerStr(std::filesystem::path(filepath).filename().string());

        auto cacheIt = templateCache_.find(key);
        if (cacheIt != templateCache_.end())
            return cacheIt->second;

        LDrawPartTemplate tmpl;
        tmpl.filename = key;

        BFCState bfc;

        // Check MPD sub-files first
        auto mpdIt = mpdSubfiles_.find(key);
        if (mpdIt != mpdSubfiles_.end())
        {
            std::istringstream stream(mpdIt->second);
            ParseStream(stream, 16, glm::mat4(1.0f), bfc, tmpl.faces);
        }
        else
        {
            std::ifstream file(filepath);
            if (!file.is_open())
            {
                SPDLOG_WARN("LDraw: cannot open file '{}'", filepath);
                return tmpl;
            }
            ParseStream(file, 16, glm::mat4(1.0f), bfc, tmpl.faces);
        }

        templateCache_[key] = tmpl;
        SPDLOG_INFO("LDraw: parsed part {} -> {} faces", key, tmpl.faces.size());
        return tmpl;
    }

    void LDrawParser::ParseStream(std::istream& input, int parentColor,
                                  const glm::mat4& transform, BFCState bfc,
                                  std::vector<LDrawFace>& outFaces)
    {
        std::string line;
        while (std::getline(input, line))
        {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;
            line = line.substr(start);

            if (line.empty())
                continue;

            int lineType = line[0] - '0';
            if (lineType < 0 || lineType > 5)
                continue;

            std::istringstream iss(line);
            int type;
            iss >> type;

            switch (type)
            {
            case 0: // Meta command
            {
                std::string rest;
                std::getline(iss, rest);
                size_t p = rest.find_first_not_of(" \t");
                if (p != std::string::npos)
                    rest = rest.substr(p);
                else
                    break;

                if (rest.find("BFC") == 0)
                {
                    std::string bfcCmd = rest.substr(3);
                    p = bfcCmd.find_first_not_of(" \t");
                    if (p != std::string::npos)
                        bfcCmd = bfcCmd.substr(p);

                    if (bfcCmd.find("CERTIFY") == 0)
                    {
                        bfc.certified = true;
                        bfc.localCull = true;
                        if (bfcCmd.find("CW") != std::string::npos && bfcCmd.find("CCW") == std::string::npos)
                            bfc.fileWindingCCW = false;
                        else
                            bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "NOCERTIFY")
                    {
                        bfc.certified = false;
                    }
                    else if (bfcCmd == "CW")
                    {
                        bfc.fileWindingCCW = false;
                    }
                    else if (bfcCmd == "CCW")
                    {
                        bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "INVERTNEXT")
                    {
                        bfc.invertNext = true;
                    }
                    else if (bfcCmd.find("CLIP") == 0)
                    {
                        bfc.localCull = true;
                        if (bfcCmd.find("CW") != std::string::npos && bfcCmd.find("CCW") == std::string::npos)
                            bfc.fileWindingCCW = false;
                        else if (bfcCmd.find("CCW") != std::string::npos)
                            bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "NOCLIP")
                    {
                        bfc.localCull = false;
                    }
                }
                break;
            }

            case 1: // Sub-file reference
            {
                int color;
                float x, y, z, a, b, c, d, e, f, g, h, i;
                std::string subfile;

                iss >> color >> x >> y >> z >> a >> b >> c >> d >> e >> f >> g >> h >> i;
                std::getline(iss, subfile);
                {
                    size_t sp = subfile.find_first_not_of(" \t");
                    if (sp != std::string::npos)
                        subfile = subfile.substr(sp);
                    size_t ep = subfile.find_last_not_of(" \t\r\n");
                    if (ep != std::string::npos)
                        subfile = subfile.substr(0, ep + 1);
                }

                if (subfile.empty())
                {
                    bfc.invertNext = false;
                    break;
                }

                // Build 4x4 sub-transform (column-major for glm)
                glm::mat4 subTransform(
                    a, d, g, 0.0f,
                    b, e, h, 0.0f,
                    c, f, i, 0.0f,
                    x, y, z, 1.0f
                );

                glm::mat4 combinedTransform = transform * subTransform;

                // Color inheritance
                int subColor = (color == 16) ? parentColor : color;

                // BFC: determine winding for child
                BFCState childBfc = bfc;
                childBfc.invertNext = false;

                bool invert = bfc.orientationInverted;
                if (bfc.invertNext)
                    invert = !invert;

                glm::mat3 rotPart(subTransform);
                if (glm::determinant(rotPart) < 0.0f)
                    invert = !invert;

                childBfc.orientationInverted = invert;

                // Try to resolve the sub-file: MPD sub-files first, then library
                std::string subKey = ToLowerStr(subfile);
                auto mpdIt = mpdSubfiles_.find(subKey);
                if (mpdIt != mpdSubfiles_.end())
                {
                    std::istringstream subStream(mpdIt->second);
                    ParseStream(subStream, subColor, combinedTransform,
                                childBfc, outFaces);
                }
                else
                {
                    std::string resolvedPath = resolver_.Resolve(subfile);
                    if (!resolvedPath.empty())
                    {
                        std::ifstream subFileStream(resolvedPath);
                        if (subFileStream.is_open())
                        {
                            ParseStream(subFileStream, subColor, combinedTransform,
                                        childBfc, outFaces);
                        }
                    }
                }

                bfc.invertNext = false;
                break;
            }

            case 2: // Line - skip
            {
                bfc.invertNext = false;
                break;
            }

            case 3: // Triangle
            {
                int color;
                glm::vec3 v0, v1, v2;
                iss >> color >> v0.x >> v0.y >> v0.z >> v1.x >> v1.y >> v1.z >> v2.x >> v2.y >> v2.z;

                v0 = glm::vec3(transform * glm::vec4(v0, 1.0f));
                v1 = glm::vec3(transform * glm::vec4(v1, 1.0f));
                v2 = glm::vec3(transform * glm::vec4(v2, 1.0f));

                bool faceWindingCCW = (bfc.fileWindingCCW != bfc.orientationInverted);

                LDrawFace face;
                face.vertexCount = 3;
                if (faceWindingCCW)
                {
                    face.vertices[0] = v0;
                    face.vertices[1] = v1;
                    face.vertices[2] = v2;
                }
                else
                {
                    face.vertices[0] = v0;
                    face.vertices[1] = v2;
                    face.vertices[2] = v1;
                }
                face.colorCode = ResolveColor(color, parentColor);

                outFaces.push_back(face);
                bfc.invertNext = false;
                break;
            }

            case 4: // Quadrilateral
            {
                int color;
                glm::vec3 v0, v1, v2, v3;
                iss >> color >> v0.x >> v0.y >> v0.z >> v1.x >> v1.y >> v1.z
                    >> v2.x >> v2.y >> v2.z >> v3.x >> v3.y >> v3.z;

                v0 = glm::vec3(transform * glm::vec4(v0, 1.0f));
                v1 = glm::vec3(transform * glm::vec4(v1, 1.0f));
                v2 = glm::vec3(transform * glm::vec4(v2, 1.0f));
                v3 = glm::vec3(transform * glm::vec4(v3, 1.0f));

                // Bowtie detection
                glm::vec3 c0 = glm::cross(v1 - v0, v2 - v0);
                glm::vec3 c1 = glm::cross(v2 - v0, v3 - v0);
                if (glm::dot(c0, c1) < 0.0f)
                    std::swap(v1, v3);

                // Apply BFC winding
                bool faceWindingCCW = (bfc.fileWindingCCW != bfc.orientationInverted);
                if (!faceWindingCCW)
                    std::swap(v1, v3);

                int resolved = ResolveColor(color, parentColor);

                // Split quad into two triangles
                LDrawFace face1;
                face1.vertexCount = 3;
                face1.vertices[0] = v0;
                face1.vertices[1] = v1;
                face1.vertices[2] = v2;
                face1.colorCode = resolved;
                outFaces.push_back(face1);

                LDrawFace face2;
                face2.vertexCount = 3;
                face2.vertices[0] = v0;
                face2.vertices[1] = v2;
                face2.vertices[2] = v3;
                face2.colorCode = resolved;
                outFaces.push_back(face2);

                bfc.invertNext = false;
                break;
            }

            case 5: // Conditional line - skip
            {
                bfc.invertNext = false;
                break;
            }

            default:
                break;
            }
        }
    }

    void LDrawParser::ClearCache()
    {
        templateCache_.clear();
    }
}
