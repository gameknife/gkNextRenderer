#include "Scene.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"

#define TINYBVH_IMPLEMENTATION
#include <chrono>
#include <atomic>

#include "Runtime/TaskCoordinator.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"

struct VertInfo
{
    glm::vec3 normal;
    uint32_t instanceId;
};

static tinybvh::BVH GCpuBvh;
// represent every triangles verts one by one, triangle count is size / 3
static std::vector<tinybvh::bvhvec4> GTriangles;
static std::vector<VertInfo> GExtInfos;

namespace Assets
{
    Scene::Scene(Vulkan::CommandPool& commandPool,
                 bool supportRayTracing)
    {
        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        RebuildMeshBuffer(commandPool, supportRayTracing);

        // 动态更新的场景结构，每帧更新
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Nodes", flags, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
                                                      indirectDrawBufferMemory_); // support 65535 nodes


        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Materials", flags, sizeof(Material) * 4096, materialBuffer_, materialBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "AmbientCubes", flags, Assets::CUBE_SIZE * Assets::CUBE_SIZE * Assets::CUBE_SIZE * sizeof(Assets::AmbientCube), ambientCubeBuffer_,
                                                    ambientCubeBufferMemory_);
    }

    Scene::~Scene()
    {
        offsetBuffer_.reset();
        offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed

        indexBuffer_.reset();
        indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        vertexBuffer_.reset();
        vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        lightBuffer_.reset();
        lightBufferMemory_.reset();

        indirectDrawBuffer_.reset();
        indirectDrawBufferMemory_.reset();
        nodeMatrixBuffer_.reset();
        nodeMatrixBufferMemory_.reset();
        materialBuffer_.reset();
        materialBufferMemory_.reset();

        ambientCubeBuffer_.reset();
        ambientCubeBufferMemory_.reset();
    }

    void Scene::Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models, std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                       std::vector<AnimationTrack>& tracks)
    {
        nodes_ = std::move(nodes);
        models_ = std::move(models);
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        // Rebuild the cpu bvh

        // tier1: simple flatten whole scene
        RebuildBVH();

        // tier2: top level, the aabb, bottom level, the triangles in local sapace, with 2 ray relay

        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<uint32_t> indices;

        offsets_.clear();
        for (auto& model : models_)
        {
            // Remember the index, vertex offsets.
            const auto indexOffset = static_cast<uint32_t>(indices.size());
            const auto vertexOffset = static_cast<uint32_t>(vertices.size());

            offsets_.emplace_back(indexOffset, vertexOffset);

            // Copy model data one after the other.

            // cpu vertex to gpu vertex
            for (auto& vertex : model.CPUVertices())
            {
                vertices.push_back(MakeVertex(vertex));
            }
            //vertices.insert(vertices.end(), model.Vertices().begin(), model.Vertices().end());
            indices.insert(indices.end(), model.CPUIndices().begin(), model.CPUIndices().end());

            model.FreeMemory();
        }

        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);

        // 材质和灯光也应考虑更新
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        UpdateMaterial();
        MarkDirty();

        GenerateAmbientCubeCPU();
    }

    void Scene::RebuildBVH()
    {
        GTriangles.clear();
        GExtInfos.clear();

        for (auto& node : nodes_)
        {
            node->RecalcTransform(true);

            uint32_t modelId = node->GetModel();
            if (modelId == -1) continue;
            Model& model = models_[modelId];

            for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
            {
                // Get the three vertices of the triangle
                Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
                Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
                Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
                // Transform vertices to world space
                glm::vec4 wpos_v0 = node->WorldTransform() * glm::vec4(v0.Position, 1);
                glm::vec4 wpos_v1 = node->WorldTransform() * glm::vec4(v1.Position, 1);
                glm::vec4 wpos_v2 = node->WorldTransform() * glm::vec4(v2.Position, 1);
            
                // Perspective divide
                wpos_v0 /= wpos_v0.w;
                wpos_v1 /= wpos_v1.w;
                wpos_v2 /= wpos_v2.w;
            
                // Calculate face normal
                glm::vec3 edge1 = glm::vec3(wpos_v1) - glm::vec3(wpos_v0);
                glm::vec3 edge2 = glm::vec3(wpos_v2) - glm::vec3(wpos_v0);
                glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

                
                // Add triangle vertices to BVH
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v0.x, wpos_v0.y, wpos_v0.z, 0));
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v1.x, wpos_v1.y, wpos_v1.z, 0));
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v2.x, wpos_v2.y, wpos_v2.z, 0));
            
                // Store additional triangle information
                GExtInfos.push_back({normal, node->GetInstanceId()});
            }
        }

        if (GTriangles.size() >= 3)
        {
            GCpuBvh.Build(GTriangles.data(), static_cast<int>(GTriangles.size()) / 3);
            //GCpuBvh.Convert(tinybvh::BVH::WALD_32BYTE, tinybvh::BVH::ALT_SOA, true);
        }
    }

    void Scene::PlayAllTracks()
    {
        for (auto& track : tracks_)
        {
            track.Play();
        }
    }

    Assets::RayCastResult Scene::RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir)
    {
        Assets::RayCastResult Result;

        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
        GCpuBvh.Intersect(ray);

        if (ray.hit.t < 100000.f)
        {
            glm::vec3 hitPos = rayOrigin + rayDir * ray.hit.t;

            Result.HitPoint = glm::vec4(hitPos, 0);
            Result.Normal = glm::vec4(GExtInfos[ray.hit.prim].normal, 0);
            Result.Hitted = true;
            Result.InstanceId = GExtInfos[ray.hit.prim].instanceId;
        }

        return Result;
    }

#define CUBE_SIZE 100
#define RAY_PERFACE 32
    
    inline void fastCopyVec3( tinybvh::bvhvec3& target, glm::vec3& source )
    {
        memcpy( &target, &source, sizeof( glm::vec3 ) );
    }
    inline void fastCopyVec3( tinybvh::bvhvec3& target, tinybvh::bvhvec3& source )
    {
        memcpy( &target, &source, sizeof( tinybvh::bvhvec3 ) );
    }

    // Pre-calculated hemisphere samples in the positive Z direction (up)

const glm::vec3 hemisphereVectors16[16] = {
    glm::vec3(0.0898831725494359, -0.23118056318048955, 0.96875),
    glm::vec3(-0.3791079112581037, 0.18705114039085075, 0.90625),
    glm::vec3(0.5153445316614019, 0.15001983597741456, 0.84375),
    glm::vec3(-0.3240808043433482, -0.5334979566560387, 0.78125),
    glm::vec3(-0.1352243320429325, 0.6819918016542008, 0.71875),
    glm::vec3(0.6081648613009205, -0.4466222553554984, 0.65625),
    glm::vec3(-0.7999438572571327, -0.08689512492988453, 0.59375),
    glm::vec3(0.559254806831153, 0.6364019944471022, 0.53125),
    glm::vec3(0.018252552906059358, -0.8831422772194816, 0.46875000000000006),
    glm::vec3(-0.6310280835640938, 0.66088160456577, 0.40624999999999994),
    glm::vec3(0.9369622623684265, -0.06275074818231247, 0.34374999999999994),
    glm::vec3(-0.7493392047328667, -0.5994907787033216, 0.2812499999999999),
    glm::vec3(0.1500724796134238, 0.9641715036043528, 0.21875),
    glm::vec3(0.5472431702110503, -0.8222596002220706, 0.15624999999999997),
    glm::vec3(-0.9665972247046685, 0.2385387655984508, 0.09375000000000008),
    glm::vec3(0.8773063928879596, 0.47891223673854594, 0.03125000000000001)
};

    
const glm::vec3 hemisphereVectors32[32] = {
    glm::vec3(0.06380871270368209, -0.16411674977923174, 0.984375),
    glm::vec3(-0.2713456981395, 0.13388146427413833, 0.953125),
    glm::vec3(0.3720439325965911, 0.1083041854826636, 0.921875),
    glm::vec3(-0.2360905314110366, -0.38864941831045413, 0.890625),
    glm::vec3(-0.0994527932145428, 0.5015812509422829, 0.859375),
    glm::vec3(0.4518001015172772, -0.3317915801282155, 0.828125),
    glm::vec3(-0.6006110862696151, -0.06524229782152816, 0.796875),
    glm::vec3(0.4246400102205648, 0.4832175711777031, 0.765625),
    glm::vec3(0.014025106917771066, -0.6785990390141627, 0.734375),
    glm::vec3(-0.4910499646725058, 0.5142812135107899, 0.703125),
    glm::vec3(0.7390090634100329, -0.04949331846649647, 0.671875),
    glm::vec3(-0.5995855814426192, -0.47968400004702705, 0.640625),
    glm::vec3(0.12194313936463708, 0.7834487731414841, 0.609375),
    glm::vec3(0.4520746755881747, -0.6792642873483389, 0.578125),
    glm::vec3(-0.8128288753565273, 0.2005915096948101, 0.546875),
    glm::vec3(0.7520560261769773, 0.41053939258723227, 0.515625),
    glm::vec3(-0.28306625928882, -0.8278009134008216, 0.48437499999999994),
    glm::vec3(-0.357091373373129, 0.8168007623879232, 0.4531249999999999),
    glm::vec3(0.8289528112710368, -0.36723115480694823, 0.42187500000000006),
    glm::vec3(-0.8724752793478753, -0.29359665580835004, 0.39062500000000006),
    glm::vec3(0.45112649883473616, 0.8169054360353547, 0.35937499999999994),
    glm::vec3(0.22185204734195418, -0.9182132941017481, 0.328125),
    glm::vec3(-0.792362708282336, 0.5329414347735422, 0.296875),
    glm::vec3(0.9533182286148584, 0.1436235160606669, 0.26562499999999994),
    glm::vec3(-0.6110028678351297, -0.756137457657169, 0.23437499999999994),
    glm::vec3(-0.06066310322190489, 0.9772718261990818, 0.20312499999999992),
    glm::vec3(0.7091634641369421, -0.683773475288631, 0.17187500000000008),
    glm::vec3(-0.9897399665274648, 0.025286518803760413, 0.14062500000000008),
    glm::vec3(0.749854715318357, 0.6524990538612495, 0.1093750000000001),
    glm::vec3(-0.11249484107422018, -0.9905762944401031, 0.0781250000000001),
    glm::vec3(-0.587325250956154, 0.8079924405365998, 0.046875),
    glm::vec3(0.9798239737002217, -0.19925069620281746, 0.01562500000000002)
};
    
const glm::vec3 hemisphereVectors64[64] = {
    glm::vec3(0.045208304261847965, -0.11627628334926335, 0.9921875),
    glm::vec3(-0.1930181724004224, 0.09523480832631948, 0.9765625),
    glm::vec3(0.26573486325149154, 0.07735698770286432, 0.9609375),
    glm::vec3(-0.16933844306146292, -0.2787629262388916, 0.9453125),
    glm::vec3(-0.07164105709419565, 0.3613152519368318, 0.9296875),
    glm::vec3(0.32689361646125864, -0.24006313671746757, 0.9140625),
    glm::vec3(-0.43653348768112604, -0.04741911773432952, 0.8984375),
    glm::vec3(0.3100703328917848, 0.3528434192444685, 0.8828125),
    glm::vec3(0.010289959042319866, -0.49787544284354635, 0.8671875),
    glm::vec3(-0.3620400469186113, 0.3791679219302705, 0.8515625),
    glm::vec3(0.5475979471417058, -0.03667402868433644, 0.8359375),
    glm::vec3(-0.44658537440975626, -0.357279870279656, 0.8203125),
    glm::vec3(0.09130924535014122, 0.5866350220173848, 0.8046875),
    glm::vec3(0.3403589991660056, -0.5114060253658194, 0.7890625),
    glm::vec3(-0.6154098724502003, 0.15187205959154645, 0.7734375),
    glm::vec3(0.5726979016083169, 0.31262969842479865, 0.7578125),
    glm::vec3(-0.21684466362347532, -0.634142024078023, 0.7421875),
    glm::vec3(-0.27523523182276277, 0.6295653268384626, 0.7265625),
    glm::vec3(0.6429854927306586, -0.2848464976643773, 0.7109375),
    glm::vec3(-0.681173899031339, -0.22922182841560512, 0.6953125),
    glm::vec3(0.3545911939487206, 0.6420981136226542, 0.6796875),
    glm::vec3(0.17559566258035023, -0.7267648583804239, 0.6640625),
    glm::vec3(-0.6316786718677515, 0.4248657012529294, 0.6484375),
    glm::vec3(0.7656645419256517, 0.11535228251548411, 0.6328125),
    glm::vec3(-0.494521160904667, -0.6119872639698072, 0.6171875),
    glm::vec3(-0.04949099612998147, 0.7972911638139558, 0.6015625),
    glm::vec3(0.5833561011650366, -0.5624703595099725, 0.5859375),
    glm::vec3(-0.8211598576906518, 0.02097952480916784, 0.5703125),
    glm::vec3(0.6276889668978702, 0.5461944142687971, 0.5546875),
    glm::vec3(-0.095041039683511, -0.8368863852815549, 0.5390625),
    glm::vec3(-0.5009894513222313, 0.6892189443548397, 0.5234375),
    glm::vec3(0.8441896860109167, -0.17166898053096186, 0.5078124999999999),
    glm::vec3(-0.7461731306626566, -0.44830472217102535, 0.4921875),
    glm::vec3(0.24983794281660274, 0.8428933419614358, 0.4765625000000001),
    glm::vec3(0.3887762013663958, -0.7977403627401998, 0.4609375000000001),
    glm::vec3(-0.8329259898180801, 0.32852864841490065, 0.4453125),
    glm::vec3(0.843190694306946, 0.3231069565607033, 0.42968750000000006),
    glm::vec3(-0.40673584817665814, -0.8143206959802536, 0.4140624999999999),
    glm::vec3(-0.2520605302580668, 0.8818804044085411, 0.3984375000000001),
    glm::vec3(0.7872133087567416, -0.483476779545836, 0.38281250000000006),
    glm::vec3(-0.913256571451911, -0.17645332114655893, 0.3671875),
    glm::vec3(0.5577998880146381, 0.7518398057595828, 0.3515625),
    glm::vec3(0.09714616234904466, -0.9368610458518398, 0.3359375000000001),
    glm::vec3(-0.7085326619425887, 0.628793582429321, 0.3203124999999999),
    glm::vec3(0.9523336958114551, 0.01503526407633283, 0.30468750000000006),
    glm::vec3(-0.6955948361299543, -0.6577162724481522, 0.2890625),
    glm::vec3(0.06895733046331357, 0.9594148321602723, 0.27343749999999994),
    glm::vec3(0.5999014736024121, -0.7573974761070995, 0.25781250000000006),
    glm::vec3(-0.957946743150088, 0.1538936390234811, 0.24218749999999997),
    glm::vec3(0.8134600252899379, 0.535679214501593, 0.22656250000000006),
    glm::vec3(-0.2388298553807905, -0.9478742908595742, 0.21093749999999992),
    glm::vec3(-0.46571344035152984, 0.8631129814918161, 0.19531250000000006),
    glm::vec3(0.9292445387423254, -0.3228265626637823, 0.17968749999999994),
    glm::vec3(-0.9057654321487023, -0.3907332568623058, 0.16406249999999994),
    glm::vec3(0.4049589269614279, 0.9022053957209516, 0.14843749999999992),
    glm::vec3(0.3115244524221444, -0.9409109178805575, 0.13281249999999994),
    glm::vec3(-0.8670032766127175, 0.48432675766114935, 0.11718749999999999),
    glm::vec3(0.9681324707648337, 0.22892046139331595, 0.10156249999999989),
    glm::vec3(-0.560064325448335, -0.8239797918965318, 0.08593749999999999),
    glm::vec3(-0.14379285760812155, 0.9871067654740497, 0.07031250000000003),
    glm::vec3(0.7735674920975367, -0.6313498337005227, 0.05468749999999993),
    glm::vec3(-0.9976073333593232, -0.05704147194322919, 0.03906249999999996),
    glm::vec3(0.6974144443248256, 0.7162847034809865, 0.023437499999999938),
    glm::vec3(-0.03041576832268194, -0.9995068013180756, 0.007812500000000042)
};

    
const glm::vec3 hemisphereVectors128[128] = {
    glm::vec3(0.03199842345430843, -0.0823003165691133, 0.99609375),
    glm::vec3(-0.136888458864317, 0.06754051175546189, 0.98828125),
    glm::vec3(0.18883637675968198, 0.05497138424410401, 0.98046875),
    glm::vec3(-0.12057897348259781, -0.198495668692847, 0.97265625),
    glm::vec3(-0.051117252825328204, 0.25780528417689386, 0.96484375),
    glm::vec3(0.23372881913868174, -0.17164505710178457, 0.95703125),
    glm::vec3(-0.31277692495057874, -0.03397587183425218, 0.94921875),
    glm::vec3(0.2226383080687183, 0.25335046130061806, 0.94140625),
    glm::vec3(0.007404356137612612, -0.35825673122933077, 0.93359375),
    glm::vec3(-0.2610813823389444, 0.27343296974656045, 0.92578125),
    glm::vec3(0.3957661358808837, -0.02650546572598553, 0.91796875),
    glm::vec3(-0.32348263290830803, -0.25879448756232487, 0.91015625),
    glm::vec3(0.06628927183222261, 0.4258890574737641, 0.90234375),
    glm::vec3(0.24766256720878219, -0.37212510742624305, 0.89453125),
    glm::vec3(-0.44884366516105495, 0.11076652311609447, 0.88671875),
    glm::vec3(0.41867485184363384, 0.22855015236190224, 0.87890625),
    glm::vec3(-0.1589037215203374, -0.4646991349227203, 0.87109375),
    glm::vec3(-0.2021794834057329, 0.46245966298503105, 0.86328125),
    glm::vec3(0.4734737716941631, -0.2097517706507885, 0.85546875),
    glm::vec3(-0.5028387131148749, -0.16921025509381515, 0.84765625),
    glm::vec3(0.2624153127689454, 0.4751848895012488, 0.83984375),
    glm::vec3(0.130280797411004, -0.5392132350465726, 0.83203125),
    glm::vec3(-0.4698761812274485, 0.3160376978519293, 0.82421875),
    glm::vec3(0.5710338759176131, 0.08602992220992335, 0.81640625),
    glm::vec3(-0.36979429535084846, -0.45763339758687516, 0.80859375),
    glm::vec3(-0.037108186588533876, 0.5978062998468218, 0.80078125),
    glm::vec3(0.43859338026983014, -0.4228905393916039, 0.79296875),
    glm::vec3(-0.6190956983731798, 0.015817058568591395, 0.78515625),
    glm::vec3(0.47456276467221914, 0.4129490001472307, 0.77734375),
    glm::vec3(-0.07206048334248653, -0.6345304894281163, 0.76953125),
    glm::vec3(-0.38095138003526663, 0.5240807112913652, 0.76171875),
    glm::vec3(0.6438053215419209, -0.13092010603891746, 0.75390625),
    glm::vec3(-0.5707510097875278, -0.3429101938371838, 0.74609375),
    glm::vec3(0.1916803099475469, 0.6466834269384439, 0.73828125),
    glm::vec3(0.2991925231364297, -0.6139211996442985, 0.73046875),
    glm::vec3(-0.6429983351780211, 0.25361601939591816, 0.72265625),
    glm::vec3(0.6529839767802063, 0.25022058099653693, 0.7148437499999999),
    glm::vec3(-0.3159976745095425, -0.632654946418661, 0.70703125),
    glm::vec3(-0.19646851513857044, 0.6873814532031561, 0.69921875),
    glm::vec3(0.6156299602782279, -0.37809674617584976, 0.69140625),
    glm::vec3(-0.7166092685540933, -0.13845844569149587, 0.68359375),
    glm::vec3(0.4391915963795719, 0.5919716579516292, 0.67578125),
    glm::vec3(0.07675585886218811, -0.7402204314619838, 0.66796875),
    glm::vec3(-0.561799039305643, 0.49857353020511835, 0.66015625),
    glm::vec3(0.7578287956997262, 0.011964457540574625, 0.65234375),
    glm::vec3(-0.5555529133860317, -0.5253003219128368, 0.64453125),
    glm::vec3(0.05527944243577408, 0.7691120962788373, 0.63671875),
    glm::vec3(0.48273081474152413, -0.6094652485662507, 0.62890625),
    glm::vec3(-0.77381448637041, 0.1243128890913411, 0.62109375),
    glm::vec3(0.6596771175842568, 0.43441018511789525, 0.61328125),
    glm::vec3(-0.19445256750814924, -0.7717485288375523, 0.60546875),
    glm::vec3(-0.38071914340383495, 0.705591908033098, 0.59765625),
    glm::vec3(0.7627966071708396, -0.26500129560927305, 0.58984375),
    glm::vec3(-0.746655251301622, -0.3220955754541644, 0.58203125),
    glm::vec3(0.3352546707073363, 0.7469117303385675, 0.57421875),
    glm::vec3(0.2590301582335995, -0.7823601070392163, 0.56640625),
    glm::vec3(-0.7241177148793762, 0.4045077965364918, 0.55859375),
    glm::vec3(0.8122514359963217, 0.19206149892768942, 0.55078125),
    glm::vec3(-0.4720620219218392, -0.6945087357135996, 0.54296875),
    glm::vec3(-0.1217708424159189, 0.835930409796925, 0.5351562499999999),
    glm::vec3(0.6582482447804852, -0.537231623770742, 0.52734375),
    glm::vec3(-0.8530581125935679, -0.048776396051134034, 0.51953125),
    glm::vec3(0.5993503688444018, 0.6155672638018668, 0.5117187500000001),
    glm::vec3(-0.02627267572176251, -0.8633586958624766, 0.5039062500000001),
    glm::vec3(-0.5667620642783076, 0.6577778908612913, 0.4960937500000001),
    glm::vec3(0.8666219538880552, -0.10270253131099981, 0.4882812500000001),
    glm::vec3(-0.7119058219593607, -0.5121912542535303, 0.48046874999999994),
    glm::vec3(0.17982114836267205, 0.8627052937924209, 0.47265625000000006),
    glm::vec3(0.4522722974434363, -0.7611636204201916, 0.46484375),
    glm::vec3(-0.8515350802973595, 0.2569249764939308, 0.4570312500000001),
    glm::vec3(0.8050240398550383, 0.3874774959967497, 0.4492187500000001),
    glm::vec3(-0.3333056848907602, -0.8331073417516129, 0.44140625),
    glm::vec3(-0.3183294734241714, 0.8430081887564481, 0.43359375000000006),
    glm::vec3(0.8074878302952185, -0.4082569424682913, 0.42578125),
    glm::vec3(-0.8746901334206125, -0.24539619907420876, 0.41796874999999994),
    glm::vec3(0.48108116806378826, 0.7748114353314093, 0.41015624999999994),
    glm::vec3(0.169285599920426, -0.8997010017197486, 0.40234375000000006),
    glm::vec3(-0.7352809557006436, 0.5510961884801834, 0.39453124999999994),
    glm::vec3(0.9177325500173831, 0.09063980933910337, 0.38671875000000006),
    glm::vec3(-0.6176417431264318, -0.6891652420564172, 0.37890625000000006),
    glm::vec3(-0.01012910596250874, 0.9285401606410666, 0.3710937499999999),
    glm::vec3(0.6367967268899706, -0.6800857754876642, 0.36328125),
    glm::vec3(-0.9319452427107501, 0.07155440141765136, 0.35546875000000006),
    glm::vec3(0.7378304536993351, 0.5785683653897532, 0.34765625),
    glm::vec3(-0.1537032845797601, -0.9278370147258249, 0.33984375000000006),
    glm::vec3(-0.5149300158131918, 0.7903178650632058, 0.33203125),
    glm::vec3(0.916173653090256, -0.23560144212566447, 0.32421874999999994),
    glm::vec3(-0.837035332410973, -0.44638429324584145, 0.31640624999999994),
    glm::vec3(0.31653081993164417, 0.8969827966546173, 0.30859374999999994),
    glm::vec3(0.37348193548278197, -0.877520303763094, 0.30078125),
    glm::vec3(-0.8703614033239134, 0.3957781438223511, 0.29296875),
    glm::vec3(0.9113647707080784, 0.2968167242561461, 0.28515625000000006),
    glm::vec3(-0.472641602423666, -0.8364749607694941, 0.2773437499999999),
    glm::vec3(-0.2170200091027972, 0.9382191753116429, 0.26953125),
    glm::vec3(0.7955560592375088, -0.5464374186573636, 0.26171875),
    glm::vec3(-0.9577957701070872, -0.1347548847942413, 0.2539062500000001),
    glm::vec3(0.6165062494091783, 0.7479023403161441, 0.24609375000000006),
    glm::vec3(0.05071007640032107, -0.9698714007794595, 0.23828125000000006),
    glm::vec3(-0.6938738412520885, 0.6822193545330627, 0.2304687500000001),
    glm::vec3(0.9742896864883279, -0.03440641129809068, 0.2226562499999999),
    glm::vec3(-0.7429844785181099, -0.6338897599481393, 0.21484375000000008),
    glm::vec3(0.11987382120798666, 0.970962578327524, 0.20703124999999994),
    glm::vec3(0.5684246710733984, -0.7982513908309438, 0.19921875000000008),
    glm::vec3(-0.9598712821467653, 0.2049652877217563, 0.19140624999999994),
    glm::vec3(0.8475170341193319, 0.49800422873556893, 0.18359374999999997),
    glm::vec3(-0.28895453547561184, -0.9410665378051175, 0.17578125000000008),
    glm::vec3(-0.4232003958481486, 0.8903302331030929, 0.16796875),
    glm::vec3(0.9146682528375029, -0.37112257117713443, 0.16015625000000003),
    glm::vec3(-0.9262959210442865, -0.3446262446370771, 0.15234374999999997),
    glm::vec3(0.450764306712072, 0.8808644944415812, 0.14453125),
    glm::vec3(0.2629303766398162, -0.9550788451423693, 0.13671875000000008),
    glm::vec3(-0.8399098495741167, 0.5271950524230308, 0.12890625000000008),
    glm::vec3(0.9764067170111871, 0.17879101399783226, 0.12109374999999992),
    glm::vec3(-0.5997568206769749, -0.7921231687369612, 0.11328125),
    glm::vec3(-0.09290981683750296, 0.9900727795009108, 0.10546875000000006),
    glm::vec3(0.7378847146778971, -0.6678243816158974, 0.09765625000000006),
    glm::vec3(-0.9959377665034227, -0.006005484003086767, 0.08984374999999994),
    glm::vec3(0.7308110152976085, 0.6776327426734309, 0.08203124999999999),
    glm::vec3(-0.08119280363114117, -0.9939312379571097, 0.07421875000000006),
    glm::vec3(-0.6118595442677903, 0.78817390723707, 0.06640625000000003),
    glm::vec3(0.9840522780085224, -0.1679520366270541, 0.05859375000000009),
    glm::vec3(-0.839419137664299, -0.5411069912423624, 0.05078124999999991),
    glm::vec3(0.2535420657309714, 0.9663695501350967, 0.042968749999999924),
    glm::vec3(0.46596162116409895, -0.8841062185552492, 0.03515624999999998),
    glm::vec3(-0.941020708934826, 0.337242264094723, 0.027343750000000045),
    glm::vec3(0.9218521366576169, 0.3870493100539312, 0.01953124999999998),
    glm::vec3(-0.4183481014350392, -0.9082111741903067, 0.011718749999999905),
    glm::vec3(-0.3050295814844872, 0.9523348652812917, 0.00390625000000009)
};

    // Transform pre-calculated samples to align with given normal
    void GetHemisphereSamples(const glm::vec3& normal, std::vector<glm::vec3>& result )
    {
        result.reserve(RAY_PERFACE);

        // Create orthonormal basis
        glm::vec3 up = glm::abs(normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
        glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
    
        // Transform matrix from Z-up to normal-up space
        glm::mat3 transform(tangent, bitangent, normal);

        // Transform each pre-calculated sample
        for (const auto& sample : hemisphereVectors32)
        {
            result.push_back(transform * sample);
        }
    }

    void BlurAmbientCubes(std::vector<AmbientCube>& ambientCubes, const std::vector<AmbientCube>& ambientCubesCopy, int cubeSize)
    {
        const float centerWeight = 1.0f;
        const float neighborWeight = 1.0f;
        const float weightSum = centerWeight + (6.0f * neighborWeight);

        // Create a lambda for processing a single z-slice
        auto processSlice = [&ambientCubes, &ambientCubesCopy, cubeSize, centerWeight, neighborWeight, weightSum](int z)
        {
            // Process only interior cubes (skip border)
            if (z <= 0 || z >= cubeSize - 1) return;

            for (int y = 1; y < cubeSize - 1; y++)
            {
                for (int x = 1; x < cubeSize - 1; x++)
                {
                    int idx = z * cubeSize * cubeSize + y * cubeSize + x;

                    // Get indices of 6 neighboring cubes
                    int idxPosX = z * cubeSize * cubeSize + y * cubeSize + (x + 1);
                    int idxNegX = z * cubeSize * cubeSize + y * cubeSize + (x - 1);
                    int idxPosY = z * cubeSize * cubeSize + (y + 1) * cubeSize + x;
                    int idxNegY = z * cubeSize * cubeSize + (y - 1) * cubeSize + x;
                    int idxPosZ = (z + 1) * cubeSize * cubeSize + y * cubeSize + x;
                    int idxNegZ = (z - 1) * cubeSize * cubeSize + y * cubeSize + x;

                    // Skip if center cube is not active
                    if (ambientCubesCopy[idx].Info.x != 1) continue;

                    uint32_t* TargetAddress = (uint32_t*)&ambientCubes[idx];
                    uint32_t* SourceAddressCenter = (uint32_t*)&ambientCubesCopy[idx];
                    uint32_t* SourceAddressPosX = (uint32_t*)&ambientCubesCopy[idxPosX];
                    uint32_t* SourceAddressNegX = (uint32_t*)&ambientCubesCopy[idxNegX];
                    uint32_t* SourceAddressPosY = (uint32_t*)&ambientCubesCopy[idxPosY];
                    uint32_t* SourceAddressNegY = (uint32_t*)&ambientCubesCopy[idxNegY];
                    uint32_t* SourceAddressPosZ = (uint32_t*)&ambientCubesCopy[idxPosZ];
                    uint32_t* SourceAddressNegZ = (uint32_t*)&ambientCubesCopy[idxNegZ];

                    for (int face = 0; face < 6; face++)
                    {
                        TargetAddress[face] = glm::packUnorm4x8((
                            glm::unpackUnorm4x8(SourceAddressCenter[face]) * centerWeight +
                            glm::unpackUnorm4x8(SourceAddressPosX[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegX[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressPosY[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegY[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressPosZ[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegZ[face]) * neighborWeight
                        ) / weightSum);
                    }
                }
            }
        };

        // Distribute tasks for each z-slice
        for (int z = 0; z < cubeSize; z++)
        {
            TaskCoordinator::GetInstance()->AddParralledTask([processSlice, z](ResTask& task)
            {
                processSlice(z);
            });
        }

        TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    }

    bool IsInShadow(const glm::vec3& point, const glm::vec3& lightPos, const tinybvh::BVH& bvh)
    {
        glm::vec3 direction = glm::normalize(lightPos - point);
        tinybvh::Ray shadowRay(tinybvh::bvhvec3(point.x, point.y, point.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z));
    
        bvh.Intersect(shadowRay);

        // If the ray hits something before reaching the light, the point is in shadow
        float distanceToLight = glm::length(lightPos - point);
        return shadowRay.hit.t < distanceToLight;
    }
    
    void Scene::GenerateAmbientCubeCPU()
    {
        if (GTriangles.size() < 3)
        {
            return;
        }

        const auto timer = std::chrono::high_resolution_clock::now();
        std::atomic<int> rayCount{0};
        constexpr int cubeSize = CUBE_SIZE;
        std::vector<AmbientCube> ambientCubes(cubeSize * cubeSize * cubeSize);
        std::vector<AmbientCube> ambientCubesCopy(cubeSize * cubeSize * cubeSize);
        ambientCubeProxys_.resize(cubeSize * cubeSize * cubeSize);
        
        // Create a lambda for processing a single z-slice
        auto processSlice = [this, &ambientCubes, &ambientCubesCopy, &rayCount](int z)
        {
            constexpr int raysPerFace = 4;
            const glm::vec3 directions[6] = {
                glm::vec3(0, 0, 1),  // PosZ
                glm::vec3(0, 0, -1), // NegZ
                glm::vec3(0, 1, 0),  // PosY
                glm::vec3(0, -1, 0), // NegY
                glm::vec3(1, 0, 0),  // PosX
                glm::vec3(-1, 0, 0)  // NegX
            };
        
            const float CUBE_UNIT = 0.25f;
            const glm::vec3 CUBE_OFFSET = glm::vec3(-50, -49.5, -50) * CUBE_UNIT;
        
            // Process single z-slice
            for (int y = 0; y < cubeSize; y++)
            {
                for (int x = 0; x < cubeSize; x++)
                {
                    glm::vec3 probePos = glm::vec3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;
                    
                    AmbientCube& cube = ambientCubes[z * cubeSize * cubeSize + y * cubeSize + x];
                    cube.Info = glm::vec4(1, 0, 0, 0);
        
                    for (int face = 0; face < 6; face++)
                    {
                        glm::vec3 baseDir = directions[face];
        
                        float occlusion = 0.0f;

                        std::vector<glm::vec3> hemisphereSamples;
                        GetHemisphereSamples(baseDir, hemisphereSamples);

                        bool nextCube = false;
                        for (int i = 0; i < RAY_PERFACE; i++)
                        {
                            tinybvh::Ray ray(tinybvh::bvhvec3(probePos.x, probePos.y, probePos.z),
                                tinybvh::bvhvec3(hemisphereSamples[i].x, hemisphereSamples[i].y, hemisphereSamples[i].z));
                            
                            GCpuBvh.Intersect(ray);
                            rayCount.fetch_add(1, std::memory_order_relaxed);
    
                            if (ray.hit.t < 10.0f)
                            {
                                const glm::vec3& hitNormal = GExtInfos[ray.hit.prim].normal;
                                float dotProduct = glm::dot(hemisphereSamples[i], hitNormal);
                                
                                if (dotProduct < 0.0f)
                                {
                                    occlusion += 1.0f;
                                }
                                else
                                {
                                    cube.Info.x = 0;
                                    nextCube = true;
                                    break;
                                }
                            }
                        }

                        if (nextCube)
                        {
                            ambientCubeProxys_[z * cubeSize * cubeSize + y * cubeSize + x] = {probePos, (cube.Info.x == 1)};
                            continue;
                        }
        
                        occlusion /= RAY_PERFACE;
                        float visibility = 1.0f - occlusion;
                        vec4 indirectColor = vec4(visibility);
                        uint packedColor = glm::packUnorm4x8(indirectColor);
        
                        switch (face)
                        {
                        case 0: cube.PosZ = packedColor; break;
                        case 1: cube.NegZ = packedColor; break;
                        case 2: cube.PosY = packedColor; break;
                        case 3: cube.NegY = packedColor; break;
                        case 4: cube.PosX = packedColor; break;
                        case 5: cube.NegX = packedColor; break;
                        }
                    }

                    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.5f));
                    
                    IsInShadow( probePos, probePos + lightDir * -10000.0f, GCpuBvh) ? cube.Info.y = 1 : cube.Info.y = 0;
                    
                    ambientCubeProxys_[z * cubeSize * cubeSize + y * cubeSize + x] = {probePos, (cube.Info.x == 1)};
                    ambientCubesCopy[z * cubeSize * cubeSize + y * cubeSize + x] = cube;
                }
            }
        };
        
        // Distribute tasks for each z-slice
        for (int z = 0; z < cubeSize; z++)
        {
            TaskCoordinator::GetInstance()->AddParralledTask([processSlice, z](ResTask& task)
            {
                processSlice(z);
            });
        }

        TaskCoordinator::GetInstance()->WaitForAllParralledTask();

        BlurAmbientCubes(ambientCubes, ambientCubesCopy, cubeSize);

        // Upload to GPU
        AmbientCube* data = reinterpret_cast<AmbientCube*>(ambientCubeBufferMemory_->Map(0, sizeof(AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
        ambientCubeBufferMemory_->Unmap();

        double elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("gen gi data takes: {:.0f}s, {}W rays\n", elapsed, rayCount / 10000);
    }

    void Scene::Tick(float DeltaSeconds)
    {
        float DurationMax = 0;

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            DurationMax = glm::max(DurationMax, track.Duration_);
        }

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            track.Time_ += DeltaSeconds;
            if (track.Time_ > DurationMax)
            {
                track.Time_ = 0;
            }
            Node* node = GetNode(track.NodeName_);
            if (node)
            {
                glm::vec3 translation = node->Translation();
                glm::quat rotation = node->Rotation();
                glm::vec3 scaling = node->Scale();

                track.Sample(track.Time_, translation, rotation, scaling);

                node->SetTranslation(translation);
                node->SetRotation(rotation);
                node->SetScale(scaling);
                node->RecalcTransform(true);

                MarkDirty();

                // temporal if camera node, request override
                if (node->GetName() == "Shot.BlueCar")
                {
                    requestOverrideModelView = true;
                    overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1), glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    }

    void Scene::UpdateMaterial()
    {
        if (materials_.empty()) return;

        gpuMaterials_.clear();
        for (auto& material : materials_)
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }

        Material* data = reinterpret_cast<Material*>(materialBufferMemory_->Map(0, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        materialBufferMemory_->Unmap();
    }

    bool Scene::UpdateNodes()
    {
        // this can move to thread task
        if (nodes_.size() > 0)
        {
            if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBufferInstanced.clear();

                    uint32_t indexOffset = 0;
                    uint32_t vertexOffset = 0;
                    uint32_t nodeOffsetBatched = 0;

                    static std::unordered_map<uint32_t, std::vector<NodeProxy>> nodeProxysMapByModel;
                    for (auto& [key, value] : nodeProxysMapByModel)
                    {
                        value.clear();
                    }

                    for (auto& node : nodes_)
                    {
                        if (node->IsVisible())
                        {
                            auto modelId = node->GetModel();
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                MarkDirty();
                            }

                            NodeProxy proxy = node->GetNodeProxy();
                            proxy.combinedPrevTS = combined;
                            nodeProxysMapByModel[modelId].push_back(proxy);
                        }
                    }

                    int modelCount = static_cast<int>(models_.size());
                    for (int i = 0; i < modelCount; i++)
                    {
                        if (nodeProxysMapByModel.find(i) != nodeProxysMapByModel.end())
                        {
                            auto& nodesOfThisModel = nodeProxysMapByModel[i];

                            // draw indirect buffer, instanced, this could be generate in gpu
                            VkDrawIndexedIndirectCommand cmd{};
                            cmd.firstIndex = indexOffset;
                            cmd.indexCount = models_[i].NumberOfIndices();
                            cmd.vertexOffset = static_cast<int32_t>(vertexOffset);
                            cmd.firstInstance = nodeOffsetBatched;
                            cmd.instanceCount = static_cast<uint32_t>(nodesOfThisModel.size());

                            indirectDrawBufferInstanced.push_back(cmd);
                            nodeOffsetBatched += static_cast<uint32_t>(nodesOfThisModel.size());

                            // fill the nodeProxy
                            nodeProxys.insert(nodeProxys.end(), nodesOfThisModel.begin(), nodesOfThisModel.end());
                        }

                        indexOffset += models_[i].NumberOfIndices();
                        vertexOffset += models_[i].NumberOfVertices();
                    }

                    NodeProxy* data = reinterpret_cast<NodeProxy*>(nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
                    std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
                    nodeMatrixBufferMemory_->Unmap();

                    VkDrawIndexedIndirectCommand* diic = reinterpret_cast<VkDrawIndexedIndirectCommand*>(indirectDrawBufferMemory_->Map(
                        0, sizeof(VkDrawIndexedIndirectCommand) * indirectDrawBufferInstanced.size()));
                    std::memcpy(diic, indirectDrawBufferInstanced.data(), indirectDrawBufferInstanced.size() * sizeof(VkDrawIndexedIndirectCommand));
                    indirectDrawBufferMemory_->Unmap();

                    indirectDrawBatchCount_ = static_cast<uint32_t>(indirectDrawBufferInstanced.size());
                }
                return true;
            }
        }
        return false;
    }

    Node* Scene::GetNode(std::string name)
    {
        for (auto& node : nodes_)
        {
            if (node->GetName() == name)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    Node* Scene::GetNodeByInstanceId(uint32_t id)
    {
        for (auto& node : nodes_)
        {
            if (node->GetInstanceId() == id)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    const Model* Scene::GetModel(uint32_t id) const
    {
        if (id < models_.size())
        {
            return &models_[id];
        }
        return nullptr;
    }

    const FMaterial* Scene::GetMaterial(uint32_t id) const
    {
        if (id < materials_.size())
        {
            return &materials_[id];
        }
        return nullptr;
    }

    void Scene::OverrideModelView(glm::mat4& OutMatrix)
    {
        if (requestOverrideModelView)
        {
            requestOverrideModelView = false;
            OutMatrix = overrideModelView;
        }
    }
}
