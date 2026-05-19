#pragma once

#include <string>
#include <vector>

namespace tinygltf
{
    class Model;
    struct Buffer;
}

namespace Assets
{
    class Scene;

    /**
     * @brief 场景保存器 - 将运行时Scene导出为GLTF/GLB文件
     *
     * 支持保存：
     * - Node树的层级关系和Transform（位置、旋转、缩放）
     * - Mesh几何数据（顶点、法线、纹理坐标、切线、索引）
     * - Material PBR参数和纹理引用
     * - 原始纹理数据（从sourceModel复制）
     */
    class FSceneSaver
    {
    public:
        /**
         * @brief 保存场景为GLB格式（二进制，推荐）
         * @param filename 输出文件路径（.glb扩展名）
         * @param scene 要保存的场景对象
         * @return 成功返回true，失败返回false
         */
        static bool SaveGLBScene(
            const std::string& filename,
            const Scene& scene);

        /**
         * @brief 保存场景为GLTF格式（JSON + 外部文件）
         * @param filename 输出文件路径（.gltf扩展名）
         * @param scene 要保存的场景对象
         * @return 成功返回true，失败返回false
         */
        static bool SaveGLTFScene(
            const std::string& filename,
            const Scene& scene);

    private:
        /**
         * @brief 核心序列化方法 - 将Scene数据填充到tinygltf::Model
         * @param outModel 输出的GLTF模型对象
         * @param scene 输入的场景对象
         * @return 成功返回true，失败返回false
         */
        static bool SerializeScene(
            tinygltf::Model& outModel,
            const Scene& scene);

        /**
         * @brief 序列化Node树和Transform
         * @param model 输出的GLTF模型对象
         * @param scene 输入的场景对象
         */
        static void SerializeNodes(
            tinygltf::Model& model,
            const Scene& scene);

        /**
         * @brief 序列化Mesh几何数据
         * @param model 输出的GLTF模型对象
         * @param scene 输入的场景对象
         */
        static void SerializeMeshes(
            tinygltf::Model& model,
            const Scene& scene);

        /**
         * @brief 序列化Material参数
         * @param model 输出的GLTF模型对象
         * @param scene 输入的场景对象
         */
        static void SerializeMaterials(
            tinygltf::Model& model,
            const Scene& scene);

        /**
         * @brief 序列化Camera数据
         * @param model 输出的GLTF模型对象
         * @param scene 输入的场景对象
         */
        static void SerializeCameras(
            tinygltf::Model& model,
            const Scene& scene);

        /**
         * @brief 复制纹理数据（从sourceModel）
         * @param outModel 输出的GLTF模型对象
         * @param sourceModel 原始的GLTF模型对象（包含纹理数据）
         */
        static void CopyTextures(
            tinygltf::Model& outModel,
            const tinygltf::Model& sourceModel);

        /**
         * @brief 将数据追加到Buffer
         * @param buffer 目标Buffer
         * @param data 数据指针
         * @param size 数据大小（字节）
         * @return 数据在Buffer中的起始偏移量
         */
        static int AppendToBuffer(
            tinygltf::Buffer& buffer,
            const void* data,
            size_t size);

        /**
         * @brief 创建BufferView
         * @param model GLTF模型对象
         * @param bufferIdx Buffer索引
         * @param offset 在Buffer中的偏移量
         * @param length 数据长度
         * @param target 目标类型（TINYGLTF_TARGET_ARRAY_BUFFER或TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER）
         * @return 创建的BufferView索引
         */
        static int CreateBufferView(
            tinygltf::Model& model,
            int bufferIdx,
            size_t offset,
            size_t length,
            int target);

        /**
         * @brief 创建Accessor
         * @param model GLTF模型对象
         * @param bufferViewIdx BufferView索引
         * @param componentType 组件类型（TINYGLTF_COMPONENT_TYPE_FLOAT等）
         * @param count 元素数量
         * @param type 类型字符串（"VEC3"、"SCALAR"等）
         * @param min 最小值（可选，用于优化）
         * @param max 最大值（可选，用于优化）
         * @return 创建的Accessor索引
         */
        static int CreateAccessor(
            tinygltf::Model& model,
            int bufferViewIdx,
            int componentType,
            int count,
            const std::string& type,
            const std::vector<double>& min = {},
            const std::vector<double>& max = {});
    };

}
