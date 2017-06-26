#ifndef AMVK_MODEL_H
#define AMVK_MODEL_H

#include "macro.h"

#include <vulkan/vulkan.h>
#include <cstring>
#include <cstddef>
#include <string>
#include <array>
#include <vector>
#include <unordered_map>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

#include "macro.h"
#include "util.h"
#include "pipeline_manager.h"
#include "buffer_helper.h"
#include "vulkan_image_creator.h"
#include "vulkan_image_info.h"
#include "vulkan_render_pass_creator.h"
#include "vulkan_state.h"
#include "texture_manager.h"
#include "pipeline_creator.h"
#include "timer.h"
#include "camera.h"

class Model {
public:
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec3 tangent;
		glm::vec3 bitangent;
		glm::vec2 texCoord;
	};

	struct UBO {
	    glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct Material {
		Material(): 
			numImages(0), 
			minImages(1), 
			maxImages(1) {}

		std::vector<ImageInfo*> 
			diffuseImages, 
			specularImages, 
			heightImages, 
			ambientImages;
		
		std::vector<VkDescriptorSet> descriptors;

		uint32_t numImages;
		uint32_t minImages, maxImages;
	}; 

	struct Mesh {
		Mesh(): baseVertex(0), numVertices(0), baseIndex(0), numIndices(0), materialIndex(0) {}
		uint32_t baseVertex, numVertices;
		uint32_t baseIndex, numIndices;
		uint32_t materialIndex;
	};

	static const aiTextureType* TEXTURE_TYPES;
	static const uint32_t NUM_TEXTURE_TYPES;
	static constexpr uint32_t const DEFAULT_FLAGS = aiProcess_CalcTangentSpace
		| aiProcess_Triangulate 
		| aiProcess_JoinIdenticalVertices  
		| aiProcess_SortByPType;

	static void convertVector(const aiVector3D& src, glm::vec3& dest);
	static void convertVector(const aiVector3D& src, glm::vec2& dest);
	static void createPipeline(VulkanState& state);

	Model(VulkanState& vulkanState);
	virtual ~Model();

	void init(const char* modelPath, 
		unsigned int pFlags = 
		aiProcess_CalcTangentSpace
		| aiProcess_Triangulate 
		| aiProcess_JoinIdenticalVertices  
		| aiProcess_SortByPType);
	
	void init(std::string modelPath, 
		unsigned int pFlags = 
		aiProcess_CalcTangentSpace
		| aiProcess_Triangulate 
		| aiProcess_JoinIdenticalVertices  
		| aiProcess_SortByPType); 

	void processModel(const aiScene& scene);
	void createCommonBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
	void createDescriptorPool();
	void createDescriptorSet();

	void draw(VkCommandBuffer& commandBuffer); 
	void update(VkCommandBuffer& commandBuffer, const Timer& timer, Camera& camera);

	void throwError(const char* error);
	void throwError(std::string& error);
	
	uint32_t numVertices, numIndices;
	VkDeviceSize uniformBufferOffset,  
				 vertexBufferOffset, 
				 indexBufferOffset; 
protected:
	std::vector<Mesh> mMeshes;
	uint32_t mNumSamplerDescriptors;
	VkDescriptorPool mDescriptorPool;
	VkDescriptorSet mUniformDescriptorSet;

	VulkanState& mState;
	BufferInfo mCommonBufferInfo;
	BufferInfo mCommonStagingBufferInfo;

	std::string mPath, mFolder;
	std::unordered_map<uint32_t, Material> mMaterialIndexToMaterial;	
};

#endif
