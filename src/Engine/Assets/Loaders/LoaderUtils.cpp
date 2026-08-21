#include "Engine/Assets/Loaders/LoaderUtils.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "ThirdParty/mikktspace/mikktspace.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

#include <cfloat>
#include <glm/glm.hpp>

namespace Assets
{
    namespace
    {
        /* Functions to allow mikktspace library to interface with our mesh representation */
        int MikktspaceGetNumFaces(const SMikkTSpaceContext* pContext)
        {
            Assets::Model* m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
            return m->NumberOfIndices() / 3;
        }

        int MikktspaceGetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
        {
            return 3;
        }

        void MikktspaceGetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[],
                                   const int iFace, const int iVert)
        {
            Assets::Model* m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
            auto v1 = m->CPUIndices()[iFace * 3 + iVert];

            fvPosOut[0] = m->CPUVertices()[v1].Position.x;
            fvPosOut[1] = m->CPUVertices()[v1].Position.y;
            fvPosOut[2] = m->CPUVertices()[v1].Position.z;
        }

        void MikktspaceGetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[],
                                 const int iFace, const int iVert)
        {
            Assets::Model* m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
            auto v1 = m->CPUIndices()[iFace * 3 + iVert];

            fvNormOut[0] = m->CPUVertices()[v1].Normal.x;
            fvNormOut[1] = m->CPUVertices()[v1].Normal.y;
            fvNormOut[2] = m->CPUVertices()[v1].Normal.z;
        }

        void MikktspaceGetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[],
                                   const int iFace, const int iVert)
        {
            Assets::Model* m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
            auto v1 = m->CPUIndices()[iFace * 3 + iVert];

            fvTexcOut[0] = m->CPUVertices()[v1].TexCoord.x;
            fvTexcOut[1] = m->CPUVertices()[v1].TexCoord.y;
        }

        void MikktspaceSetTSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[],
                                      const float fSign, const int iFace, const int iVert)
        {
            Assets::Model* m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
            auto v1 = m->CPUIndices()[iFace * 3 + iVert];

            m->CPUVertices()[v1].Tangent = glm::vec4(fvTangent[0], fvTangent[1], fvTangent[2], fSign);
        }

        SMikkTSpaceInterface MikktspaceInterface = {
            .m_getNumFaces = MikktspaceGetNumFaces,
            .m_getNumVerticesOfFace = MikktspaceGetNumVerticesOfFace,
            .m_getPosition = MikktspaceGetPosition,
            .m_getNormal = MikktspaceGetNormal,
            .m_getTexCoord = MikktspaceGetTexCoord,
            .m_setTSpaceBasic = MikktspaceSetTSpaceBasic,
            .m_setTSpace = NULL,
        };
    }

    void GenerateMikkTSpace(Model* m)
    {
        SMikkTSpaceContext mikktspaceContext;

        mikktspaceContext.m_pInterface = &MikktspaceInterface;
        mikktspaceContext.m_pUserData = m;
        genTangSpaceDefault(&mikktspaceContext);
    }

    namespace
    {
        // The 8 world-space corners of a node's local AABB.
        void AppendWorldCorners(const glm::mat4& worldTransform, const glm::vec3& aabbMin,
                                const glm::vec3& aabbMax, glm::vec3& boundsMin, glm::vec3& boundsMax)
        {
            const glm::vec3 corners[8] = {
                {aabbMin.x, aabbMin.y, aabbMin.z},
                {aabbMax.x, aabbMin.y, aabbMin.z},
                {aabbMin.x, aabbMax.y, aabbMin.z},
                {aabbMax.x, aabbMax.y, aabbMin.z},
                {aabbMin.x, aabbMin.y, aabbMax.z},
                {aabbMax.x, aabbMin.y, aabbMax.z},
                {aabbMin.x, aabbMax.y, aabbMax.z},
                {aabbMax.x, aabbMax.y, aabbMax.z}
            };
            for (const glm::vec3& corner : corners)
            {
                const glm::vec4 worldPos = worldTransform * glm::vec4(corner, 1.0f);
                boundsMin = glm::min(boundsMin, glm::vec3(worldPos));
                boundsMax = glm::max(boundsMax, glm::vec3(worldPos));
            }
        }
    } // namespace

    bool SceneWorldBounds(const std::vector<std::shared_ptr<Node>>& nodes,
                          const std::vector<Model>& models,
                          glm::vec3& outMin,
                          glm::vec3& outMax)
    {
        glm::vec3 boundsMin(FLT_MAX);
        glm::vec3 boundsMax(-FLT_MAX);
        bool hasModel = false;
        for (const auto& node : nodes)
        {
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (!render || !render->IsDrawable())
            {
                continue;
            }
            const uint32_t modelIdx = render->GetModelId();
            if (modelIdx >= models.size())
            {
                continue;
            }
            const Model& model = models[modelIdx];
            AppendWorldCorners(node->WorldTransform(), model.GetLocalAABBMin(), model.GetLocalAABBMax(),
                               boundsMin, boundsMax);
            hasModel = true;
        }
        if (!hasModel)
        {
            return false;
        }
        outMin = boundsMin;
        outMax = boundsMax;
        return true;
    }

    void ExtendCameraFarPlane(Camera& camera, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        // ModelView is a view matrix, so the eye is the translation of its
        // inverse.
        const glm::vec3 eye = glm::vec3(glm::inverse(camera.ModelView)[3]);
        float farthest = 0.0f;
        for (int k = 0; k < 8; ++k)
        {
            const glm::vec3 corner(
                (k & 1) ? boundsMax.x : boundsMin.x,
                (k & 2) ? boundsMax.y : boundsMin.y,
                (k & 4) ? boundsMax.z : boundsMin.z);
            farthest = glm::max(farthest, glm::length(corner - eye));
        }
        // Only ever extends. An authored camera that already reaches past the
        // scene keeps what it asked for; one that does not would otherwise
        // clip the scene away entirely, which is what the 2 km struct default
        // did to a 3 km generated area — the overview camera sits a kilometre
        // outside a 4.2 km diagonal and rendered nothing at all.
        camera.FarPlane = glm::max(camera.FarPlane, farthest * 1.05f + 1.0f);
    }

    Camera AutoFocusCamera(EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Node>>& nodes,
                           std::vector<Model>& models, const bool obliqueView)
    {
        //auto center camera by scene bounds
        glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
        const bool hasModel = SceneWorldBounds(nodes, models, boundsMin, boundsMax);

        if (!hasModel)
        {
            boundsMin = glm::vec3(-10, -10, -10);
            boundsMax = glm::vec3(10, 10, 10);
        }

        glm::vec3 boundsCenter = (boundsMax - boundsMin) * 0.5f + boundsMin;
        const glm::vec3 extent = glm::max(boundsMax - boundsMin, glm::vec3(0.01f));
        const float sphereRadius = glm::max(glm::length(extent) * 0.5f, 0.01f);

        Camera newCamera;
        newCamera.FieldOfView = 40;
        const float halfFov = glm::radians(newCamera.FieldOfView) * 0.5f;
        const float viewDistance = sphereRadius / glm::max(std::sin(halfFov), 0.01f) * 1.15f;
        const float cameraDistance = obliqueView ? viewDistance * 0.75f : viewDistance;
        const glm::vec3 viewDirection =
            obliqueView ? glm::normalize(glm::vec3(1.0f, 0.45f, 1.0f)) : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 eye = boundsCenter + viewDirection * cameraDistance;
        newCamera.ModelView = lookAt(eye, boundsCenter, glm::vec3(0, 1, 0));
        newCamera.Aperture = 0.0f;
        newCamera.FocalDistance = cameraDistance;
        newCamera.name = "AutoCamera";
        newCamera.NearPlane = sphereRadius < 25.0f ? 0.01f : glm::max(0.05f, sphereRadius * 0.001f);
        newCamera.FarPlane = glm::max(cameraDistance + sphereRadius * 1.5f, newCamera.NearPlane * 10.0f);
        cameraInit.ControlSpeed = glm::max(cameraInit.ControlSpeed, sphereRadius * 0.04f);

        return newCamera;
    }
}
