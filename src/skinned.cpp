#include "skinned.h"

static const aiTextureType Skinned_TEXTURE_TYPES[] ={
	aiTextureType_DIFFUSE
   // aiTextureType_SPECULAR,
   // aiTextureType_HEIGHT,
	//aiTextureType_AMBIENT
};

const aiTextureType* Skinned::TEXTURE_TYPES = Skinned_TEXTURE_TYPES;

const uint32_t Skinned::NUM_TEXTURE_TYPES = ARRAY_SIZE(Skinned_TEXTURE_TYPES);

Skinned::Skinned(VulkanState& vulkanState):
	numVertices(0),
	numIndices(0),
	numBones(0),
	uniformBufferOffset(0),
	vertexBufferOffset(0),
	indexBufferOffset(0),
	mNumSamplerDescriptors(0),
	mState(vulkanState),
	mCommonBufferInfo(mState.device),
	mCommonStagingBufferInfo(mState.device),
	mPath(""),
	mFolder(""),
	mAnimNodeRoot(NULL)
{


}


Skinned::~Skinned() 
{	
}

void Skinned::init(std::string modelPath, unsigned int pFlags) 
{
	init(modelPath.c_str(), pFlags);
}

void Skinned::init(const char* modelPath, unsigned int pFlags)
{
	mPath = modelPath;
	mFolder = FileManager::getFilePath(std::string(modelPath));
	LOG("FOLDER:" << mFolder);

	mScene = importer.ReadFile(modelPath, pFlags);
	  
	  // If the import failed, report it
	if (!mScene || !mScene->mRootNode || mScene->mFlags == AI_SCENE_FLAGS_INCOMPLETE)
		throwError(importer.GetErrorString());
	if (!mScene->HasMeshes())
		throwError("No meshes found");
	if (!mScene->HasAnimations())
		throwError("No amations found");

	processModel(*mScene);
}


void Skinned::processMeshVertices(std::vector<Vertex>& vertices, aiMesh& mesh, Mesh& meshInfo)
{
	bool hasPositions = mesh.HasPositions();
	bool hasNormals = mesh.HasNormals();
	bool hasTangentsAndBitangents = mesh.HasTangentsAndBitangents();
	bool hasTexCoords = mesh.HasTextureCoords(0);

	// Vertices
	for (size_t j = 0; j < mesh.mNumVertices; ++j) {
		Vertex& vertex = vertices[meshInfo.baseVertex + j];
		if (hasPositions) 
			convertVector(mesh.mVertices[j], vertex.pos);
		if (hasNormals) 
			convertVector(mesh.mNormals[j], vertex.normal);
		if (hasTangentsAndBitangents) {
			convertVector(mesh.mTangents[j], vertex.tangent);
			convertVector(mesh.mBitangents[j], vertex.bitangent);
		}
		if (hasTexCoords) 
			convertVector(mesh.mTextureCoords[0][j], vertex.texCoord);
	}
}

void Skinned::processMeshIndices(std::vector<uint32_t>& indices, aiMesh& mesh, Mesh& meshInfo)
{
	for (size_t j = 0; j < mesh.mNumFaces; ++j) 
		for (size_t k = 0; k < 3; ++k)
			indices[meshInfo.baseIndex + 3 * j + k] = mesh.mFaces[j].mIndices[k] + meshInfo.baseIndex;
}

void Skinned::processMeshMaterials(aiMesh& mesh, Mesh& meshInfo) 
{
	// Textures
	meshInfo.materialIndex = mesh.mMaterialIndex;
	aiMaterial& material = *(mScene->mMaterials[mesh.mMaterialIndex]);
	auto it = mMaterialIndexToMaterial.find(mesh.mMaterialIndex);
	if (it == mMaterialIndexToMaterial.end()) {
		Material materialInfo;
		for (size_t j = 0; j < NUM_TEXTURE_TYPES; ++j) {
			aiTextureType textureType = TEXTURE_TYPES[j];
			size_t numMaterials = material.GetTextureCount(textureType); 
			for (size_t k = 0; k < numMaterials; ++k) {
				aiString texturePath;
			
				material.GetTexture(textureType, k, &texturePath);
				std::string fullTexturePath = mFolder + "/";
				fullTexturePath += texturePath.C_Str();

				TextureDesc textureDesc(fullTexturePath);
				ImageInfo* imageInfo = TextureManager::load(
						mState, 
						mState.commandPool, 
						mState.graphicsQueue, 
						textureDesc);

				LOG("AFTER LOAD");
				switch(textureType) {
					case aiTextureType_DIFFUSE:
						materialInfo.diffuseImages.push_back(imageInfo);
						++materialInfo.numImages;
						break;
					case aiTextureType_SPECULAR:
						materialInfo.specularImages.push_back(imageInfo);
						++materialInfo.numImages;
						break;
					case aiTextureType_HEIGHT:
						materialInfo.heightImages.push_back(imageInfo);
						++materialInfo.numImages;
						break;
					case aiTextureType_AMBIENT:
						materialInfo.ambientImages.push_back(imageInfo);
						++materialInfo.numImages;
						break;
					default:
						break;
				}
			}
			mNumSamplerDescriptors += NUM_TEXTURE_TYPES;
		}	
		mMaterialIndexToMaterial[mesh.mMaterialIndex] = materialInfo;
	} else {LOG("MATERIAL EXISTS");}

}


void Skinned::processMeshBones(
		aiNode* node, 
		std::unordered_map<std::string, uint32_t>& boneNameToIndexMap, 
		std::vector<Vertex>& vertices, 
		std::vector<uint32_t>& vertexWeightIndices,
		aiMesh& mesh, 
		Mesh& meshInfo)
{
	if (!mesh.HasBones())
		return;

	for (size_t i = 0; i < mesh.mNumBones; ++i) {
		aiBone* bone = mesh.mBones[i];
		std::string name(bone->mName.C_Str());
		uint32_t boneIndex;
		auto it = boneNameToIndexMap.find(name);
		if (it == boneNameToIndexMap.end()) {
			boneIndex = numBones++;
			boneNameToIndexMap[name] = boneIndex; 
			mBoneTransforms[boneIndex] = bone->mOffsetMatrix;
		} else {
			boneIndex = it->second;
		}

		aiNode* key = mScene->mRootNode->FindNode(bone->mName); 
		if (key) { 
			//node where name = bone name found
			mNodeToBoneIndexMap[key] = boneIndex;
			//set tree branch as animatable
			if (key != node)
				for (key = key->mParent; key && (key != node || key != node->mParent); key = key->mParent)
					mNodeToBoneIndexMap.insert(std::make_pair(key, BONE_INDEX_UNSET));
		}

		for (size_t j = 0; j < bone->mNumWeights; ++j) {
			aiVertexWeight& vertexWeight = bone->mWeights[j];
			uint32_t index = meshInfo.baseVertex + vertexWeight.mVertexId;
			uint32_t& weightIndex = vertexWeightIndices[index];
			Vertex& vertex = vertices[index];

			if (weightIndex < MAX_BONES_PER_VERTEX && vertex.weights[weightIndex] == 0.0f) {
				vertex.boneIndices[weightIndex] = boneIndex;
				vertex.weights[weightIndex] = vertexWeight.mWeight;
				LOG("WEIGHT INDEX:" << weightIndex);
				++weightIndex;
			}
				
			/*for (size_t k = 0; k < MAX_BONES_PER_VERTEX; ++k) {
			    if (vertex.weights[k] == 0.0f) {
					vertex.boneIndices[k] = boneIndex;
					vertex.weights[k] = vertexWeight.mWeight;
					//LOG("INSERT WEIGHTS AT: " << k << " FOR " << meshInfo.baseVertex + vertexWeight.mVertexId);
					break;
                }
			}*/
		}
	}
}


void Skinned::createAnimNode(aiNode* node, AnimNode* parent)
{
    auto it = mNodeToBoneIndexMap.find(node);
    if (it == mNodeToBoneIndexMap.end()) 
		return;
	LOG("CREATE NODE");
	AnimNode* animNode = new AnimNode(*node);

	if (parent) 
		parent->mChildren.push_back(animNode);
	else 
		mAnimNodeRoot = animNode;
	
	if (it->second != BONE_INDEX_UNSET)
		animNode->boneIndex = it->second;

	animNode->mAnimTypes.reserve(mScene->mNumAnimations);

	for (size_t i = 0; i < mScene->mNumAnimations; ++i) {
		aiAnimation* anim = mScene->mAnimations[i];
		bool hasAnim = false;
		if (anim->mTicksPerSecond <= 0.0)
			anim->mTicksPerSecond = AnimNode::DEFAULT_TICKS_PER_SECOND; 
	   
		if (anim->mDuration <= 0.0)
			anim->mDuration = AnimNode::DEFAULT_TICKS_DURATION;

		for (size_t j = 0; j < anim->mNumChannels; ++j) {
			aiNodeAnim* channel = anim->mChannels[j];
			if (std::string(channel->mNodeName.C_Str()) == std::string(node->mName.C_Str())) {
				hasAnim = true;
				animNode->mAnimTypes.push_back(channel);
				break;
			}
		}
   
		if (!hasAnim)
			animNode->mAnimTypes.push_back(NULL);
	} 
	
	for (size_t i = 0; i < node->mNumChildren; ++i)
		createAnimNode(node->mChildren[i], animNode);
}



void Skinned::processModel(const aiScene& scene) 
{
    mModelSpaceTransform = mScene->mRootNode->mTransformation;
    mModelSpaceTransform.Inverse();

	mMeshes.resize(scene.mNumMeshes);

	uint32_t baseVertex = 0;
	uint32_t baseIndex = 0;
	uint32_t baseBone = 0;
	
	for (size_t i = 0; i < scene.mNumMeshes; ++i) {
		aiMesh* mesh = scene.mMeshes[i];
		Mesh& meshInfo = mMeshes[i];
		
		meshInfo.baseVertex = baseVertex;
		baseVertex += mesh->mNumVertices;
		meshInfo.numVertices = baseVertex - meshInfo.baseVertex;
		
		meshInfo.baseIndex = baseIndex;
		baseIndex += 3 * mesh->mNumFaces;
		meshInfo.numIndices = baseIndex - meshInfo.baseIndex;
		
		baseBone += mesh->mNumBones;
	}

	numVertices = baseVertex;
	numIndices = baseIndex;

	std::vector<Vertex> vertices(numVertices);
	std::vector<uint32_t> vertexBoneIndices(numVertices);
	std::vector<uint32_t> indices(numIndices);
	mBoneTransforms.resize(baseBone);
	std::unordered_map<std::string, uint32_t> boneNameToIndexMap;

	std::stack<aiNode*> s;
    s.push(mScene->mRootNode);
	// depth walk nodes tree to process each child
	while (!s.empty()) {
		aiNode* n = s.top();
        s.pop();

		for (size_t i = 0; i < n->mNumMeshes; ++i) {
            aiMesh& mesh = *(mScene->mMeshes[n->mMeshes[i]]);
			Mesh& meshInfo = mMeshes[i];
			processMeshVertices(vertices, mesh, meshInfo);
			processMeshIndices(indices, mesh, meshInfo);
			processMeshMaterials(mesh, meshInfo);
			processMeshBones(n, boneNameToIndexMap, vertices, vertexBoneIndices, mesh, meshInfo);
        }
            
        for(size_t i = 0; i < n->mNumChildren; ++i)
            s.push(n->mChildren[i]);
	}

	// create animated nodes tree
	createAnimNode(mScene->mRootNode, NULL);
	createCommonBuffer(vertices, indices);
	createDescriptorPool();
	createDescriptorSet();
}

void Skinned::processAnimNode(float progress, aiMatrix4x4& parentTransform, AnimNode* animNode, uint32_t animationIndex)
{
  //  LOG("INDEX:" << animNode->boneIndex << "ANIM PROCESS:" << progress << " RELATED " << animNode->hasRelatedBone()); 
    aiMatrix4x4 animTransform = animNode->getAnimatedTransform(progress, animationIndex);
    aiMatrix4x4 currTransform = parentTransform * animTransform;

    if (animNode->boneIndex < MAX_BONES) {
        // update bone
        aiMatrix4x4 animatedTransform = mModelSpaceTransform * currTransform * mBoneTransforms[animNode->boneIndex];
        // change row-order, since assimp is uses directx matrices row order
		ubo.bones[animNode->boneIndex] = glm::transpose(glm::make_mat4(&animatedTransform.a1));
    }

    for (size_t i = 0; i < animNode->mChildren.size(); ++i)         
        processAnimNode(progress, currTransform, animNode->mChildren[i] , animationIndex);
}

void Skinned::createCommonBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
	numVertices = vertices.size();
	numIndices = indices.size();

	VkDeviceSize uniformBufferSize = sizeof(UBO);
	VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * numVertices;
	VkDeviceSize indexBufferSize = sizeof(indices[0]) * numIndices;
	
	uniformBufferOffset = 0;
	vertexBufferOffset = uniformBufferSize;
	indexBufferOffset = vertexBufferOffset + vertexBufferSize;

	mCommonBufferInfo.size = vertexBufferSize + indexBufferSize + uniformBufferSize;
	mCommonStagingBufferInfo.size = mCommonBufferInfo.size;
	BufferHelper::createStagingBuffer(mState, mCommonStagingBufferInfo);
	
	char* data;
	vkMapMemory(mState.device, mCommonStagingBufferInfo.memory, 0, mCommonStagingBufferInfo.size, 0, (void**) &data);
	memcpy(data + uniformBufferOffset, &ubo, uniformBufferSize);
	memcpy(data + vertexBufferOffset, vertices.data(), vertexBufferSize);
	memcpy(data + indexBufferOffset, indices.data(), indexBufferSize);
	vkUnmapMemory(mState.device, mCommonStagingBufferInfo.memory);

	BufferHelper::createCommonBuffer(mState, mCommonBufferInfo);

	BufferHelper::copyBuffer(
			mState,
			mCommonStagingBufferInfo.buffer, 
			mCommonBufferInfo.buffer, 
			mCommonBufferInfo.size);
}

void Skinned::createPipeline(VulkanState& state) 
{
	VkPipelineShaderStageCreateInfo stages[] = {
		state.shaders.skinned.vertex,
		state.shaders.skinned.fragment
	};

	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(Vertex);
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//location, binding, format, offset
	VkVertexInputAttributeDescription attrDesc[] = { 
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) },
		{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
		{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent) },
		{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, bitangent) },
		{ 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) },
		{ 5, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, boneIndices) },
		{ 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights) }
	};

	auto vertexInputInfo = PipelineCreator::vertexInputState(&bindingDesc, 1, attrDesc, ARRAY_SIZE(attrDesc)); 

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo = PipelineCreator::inputAssemblyNoRestart(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	VkPipelineViewportStateCreateInfo viewportState = PipelineCreator::viewportStateDynamic();

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicInfo = PipelineCreator::dynamicState(dynamicStates, ARRAY_SIZE(dynamicStates));
	VkPipelineRasterizationStateCreateInfo rasterizationState = PipelineCreator::rasterizationStateCullBackCCW();
	VkPipelineDepthStencilStateCreateInfo depthStencil = PipelineCreator::depthStencilStateDepthLessNoStencil();
	VkPipelineMultisampleStateCreateInfo multisampleState = PipelineCreator::multisampleStateNoMultisampleNoSampleShading();
	VkPipelineColorBlendAttachmentState blendAttachmentState = PipelineCreator::blendAttachmentStateDisabled();

	VkPipelineColorBlendStateCreateInfo blendState = PipelineCreator::blendStateDisabled(&blendAttachmentState, 1); 
	
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(state.physicalDevice, &physicalDeviceProperties);

	VkDescriptorSetLayout layouts[] = {
		state.descriptorSetLayouts.uniform,
		state.descriptorSetLayouts.sampler
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = PipelineCreator::layout(layouts, ARRAY_SIZE(layouts), NULL, 0);
	VK_CHECK_RESULT(vkCreatePipelineLayout(state.device, &pipelineLayoutInfo, nullptr, &state.pipelines.skinned.layout));

	PipelineCacheInfo cacheInfo("skinned", state.pipelines.skinned.cache);
	cacheInfo.getCache(state.device);

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = ARRAY_SIZE(stages);
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &assemblyInfo;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizationState;
	pipelineInfo.pMultisampleState = &multisampleState;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &blendState;
	pipelineInfo.pDynamicState = &dynamicInfo;
	pipelineInfo.layout = state.pipelines.skinned.layout;
	pipelineInfo.renderPass = state.renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &state.pipelines.skinned.pipeline));
	
	cacheInfo.saveCache(state.device);

	LOG("SKINNED MODEL PIPELINE CREATED");

}

void Skinned::createDescriptorPool() 
{
	VkDescriptorPoolSize uboSize = {};
	uboSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboSize.descriptorCount = mNumSamplerDescriptors + 1;
	
	VkDescriptorPoolSize samplerSize = {};
	samplerSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerSize.descriptorCount = mNumSamplerDescriptors + 1;

	VkDescriptorPoolSize poolSizes[] = {
		uboSize,
		samplerSize
	};
	
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = ARRAY_SIZE(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = mNumSamplerDescriptors + 1;
	
	LOG("NUM SAMPLERS:" << mNumSamplerDescriptors << " materials: " << mMaterialIndexToMaterial.size());
	LOG("MAX SETS:" << poolInfo.maxSets);

	VK_CHECK_RESULT(vkCreateDescriptorPool(mState.device, &poolInfo, nullptr, &mDescriptorPool));
}

void Skinned::createDescriptorSet()
{
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &mState.descriptorSetLayouts.uniform;

	VK_CHECK_RESULT(vkAllocateDescriptorSets(mState.device, &allocInfo, &mUniformDescriptorSet));
	
	VkDescriptorBufferInfo buffInfo = {};
	buffInfo.buffer = mCommonBufferInfo.buffer;
	buffInfo.offset = uniformBufferOffset;
	buffInfo.range = sizeof(UBO);

	VkWriteDescriptorSet uniformWriteSet = {};
	uniformWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uniformWriteSet.dstSet = mUniformDescriptorSet;
	uniformWriteSet.dstBinding = 0;
	uniformWriteSet.dstArrayElement = 0;
	uniformWriteSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformWriteSet.descriptorCount = 1;
	uniformWriteSet.pBufferInfo = &buffInfo;

	vkUpdateDescriptorSets(mState.device, 1, &uniformWriteSet, 0, nullptr);
	LOG("UNIFORM SET CREATED");
	
	std::vector<VkDescriptorSetLayout> layouts(NUM_TEXTURE_TYPES, mState.descriptorSetLayouts.sampler);
	
	for (auto& materialPair : mMaterialIndexToMaterial) {
		Material& material = materialPair.second;
		material.descriptors.resize(material.maxImages);
		for (size_t i = 0; i < material.maxImages; ++i) {
			VkDescriptorSet& descriptorSet = material.descriptors[i];
			std::vector<VkWriteDescriptorSet> writeSets(NUM_TEXTURE_TYPES);

			VkDescriptorSetAllocateInfo samplerAllocInfo = {};
			samplerAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			samplerAllocInfo.descriptorPool = mDescriptorPool;
			samplerAllocInfo.descriptorSetCount = NUM_TEXTURE_TYPES;
			samplerAllocInfo.pSetLayouts = layouts.data();

			VK_CHECK_RESULT(vkAllocateDescriptorSets(mState.device, &samplerAllocInfo, &descriptorSet));

			for (size_t j = 0; j < NUM_TEXTURE_TYPES; ++j) {
				std::vector<ImageInfo*>* images = NULL;
				uint32_t binding;

				switch(TEXTURE_TYPES[j]) {
					case aiTextureType_DIFFUSE:
						images = &material.diffuseImages;
						binding = 0;
						break;
					case aiTextureType_SPECULAR:
						images = &material.specularImages;
						binding = 1;
						break;
					case aiTextureType_HEIGHT:
						images = &material.heightImages;
						binding = 2;
						break;
					default: //aiTextureType_AMBIENT
						images = &material.ambientImages;
						binding = 3;
						break;
				}

				bool hasImage = images != NULL && j < images->size();

				if (hasImage) {
					LOG("HAS IMAGE binding: " << binding << " index: " << j << " N:" << NUM_TEXTURE_TYPES);
					ImageInfo* imageInfo = images->at(i);
					VkDescriptorImageInfo descriptorInfo = {};
					descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					descriptorInfo.imageView = imageInfo->imageView;
					descriptorInfo.sampler = imageInfo->sampler;

					writeSets[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeSets[j].dstSet = descriptorSet;
					writeSets[j].dstBinding = binding;
					writeSets[j].dstArrayElement = 0;
					writeSets[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					writeSets[j].descriptorCount = 1;
					writeSets[j].pImageInfo = &descriptorInfo;
				}
			}
			vkUpdateDescriptorSets(mState.device, writeSets.size(), writeSets.data(), 0, nullptr);
		} 
	}
}

void Skinned::draw(VkCommandBuffer& commandBuffer, VkPipeline& pipeline, VkPipelineLayout& pipelineLayout)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	
	VkDeviceSize offset = vertexBufferOffset;
	VkBuffer& commonBuff = mCommonBufferInfo.buffer;
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &commonBuff, &offset);
	vkCmdBindIndexBuffer(commandBuffer, mCommonBufferInfo.buffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
	
	for (const auto& mesh : mMeshes) {
		Material& material = mMaterialIndexToMaterial[mesh.materialIndex];
		VkDescriptorSet sets[] = {
			mUniformDescriptorSet,
			material.descriptors[0]
		};

		vkCmdBindDescriptorSets(
			commandBuffer, 
			VK_PIPELINE_BIND_POINT_GRAPHICS, 
			pipelineLayout, 
			0, 
			ARRAY_SIZE(sets), 
			sets, 
			0, 
			nullptr);
		vkCmdDrawIndexed(commandBuffer, mesh.numIndices, 1, mesh.baseIndex, 0, 0);
	}
}

void Skinned::update(VkCommandBuffer& cmdBuffer, const Timer& timer, Camera& camera, uint32_t animationIndex /* = 0 */)
{
    if (animationIndex >= mScene->mNumAnimations) {
        LOG("ERROR: WRONG ANIMATION INDEX:" << animationIndex);
        return;
    }

    aiMatrix4x4 initialTransform;
    float progress = timer.total() * mScene->mAnimations[0]->mTicksPerSecond;
    progress = fmod(progress, mScene->mAnimations[0]->mDuration);
    processAnimNode(progress, initialTransform, mAnimNodeRoot, animationIndex);

	ubo.view = camera.view();
	ubo.proj = camera.proj();

	vkCmdUpdateBuffer(
			cmdBuffer,
			mCommonBufferInfo.buffer,
			uniformBufferOffset,
			sizeof(UBO),
			&ubo);
}

void Skinned::convertVector(const aiVector3D& src, glm::vec3& dest)
{
    dest.x = src.x;
    dest.y = src.y; 
    dest.z = src.z;
}

void Skinned::convertVector(const aiVector3D& src, glm::vec2& dest)
{
    dest.x = src.x;
    dest.y = src.y; 
}


void Skinned::throwError(const char* error) 
{
	std::string errorStr = error;
	throwError(errorStr);
}

void Skinned::throwError(std::string& error) 
{
	error += " for scene: " + mPath;
	throw std::runtime_error(error);
}
