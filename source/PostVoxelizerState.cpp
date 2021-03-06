#include "PipelineStates.h"

#include <glm/gtc/matrix_transform.hpp>
#include "VCTPipelineDefines.h"
#include "SwapChain.h"
#include "VulkanCore.h"
#include "Shader.h"
#include "DataTypes.h"
#include "AnisotropicVoxelTexture.h"
#include "Camera.h"

struct PushConstantComp
{
	glm::vec3 gridres;			// Resolution
	uint32_t cascadeNum;		// Current cascade
};

struct Parameter
{
	AnisotropicVoxelTexture* avt;
};

void BuildCommandBufferPostVoxelizerState(
	RenderState* renderstate,
	VkCommandPool commandpool,
	VulkanCore* core,
	uint32_t framebufferCount,
	VkFramebuffer* framebuffers,
	BYTE* parameters)
{
	uint32_t width = core->GetSwapChain()->m_width;
	uint32_t height = core->GetSwapChain()->m_height;
	RenderState* renderState = renderstate;

	////////////////////////////////////////////////////////////////////////////////
	// Rebuild the parameters
	////////////////////////////////////////////////////////////////////////////////
	AnisotropicVoxelTexture* avt = ((Parameter*)parameters)->avt;

	////////////////////////////////////////////////////////////////////////////////
	// Rebuild the command buffers
	////////////////////////////////////////////////////////////////////////////////
	renderState->m_commandBufferCount = avt->m_cascadeCount;
	renderState->m_commandBuffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer)*renderState->m_commandBufferCount);
	for (uint32_t i = 0; i < renderState->m_commandBufferCount; i++)
		renderState->m_commandBuffers[i] = VKTools::Initializers::CreateCommandBuffer(commandpool, core->GetViewDevice(), VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

	////////////////////////////////////////////////////////////////////////////////
	// Record command buffer
	////////////////////////////////////////////////////////////////////////////////
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	for (uint32_t i = 0; i < renderState->m_commandBufferCount; i++)
	{
		VK_CHECK_RESULT(vkBeginCommandBuffer(renderState->m_commandBuffers[i], &cmdBufInfo));

		vkCmdResetQueryPool(renderState->m_commandBuffers[i], renderState->m_queryPool, 0, 4);
		vkCmdWriteTimestamp(renderState->m_commandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderState->m_queryPool, 0);

		// Submit push constant
		PushConstantComp pc;
		pc.gridres = glm::vec3(avt->m_width, avt->m_height, avt->m_depth);
		pc.cascadeNum = i;
		vkCmdPushConstants(renderState->m_commandBuffers[i], renderState->m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantComp), &pc);

		vkCmdBindPipeline(renderState->m_commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, renderState->m_pipelines[0]);
		vkCmdBindDescriptorSets(renderState->m_commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, renderState->m_pipelineLayout, 0, 1, &renderState->m_descriptorSets[0], 0, 0);

		uint32_t numdis = ((avt->m_width) / 8);
		vkCmdDispatch(renderState->m_commandBuffers[i], numdis * NUM_DIRECTIONS, numdis, numdis);

		vkCmdWriteTimestamp(renderState->m_commandBuffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, renderState->m_queryPool, 1);

		vkEndCommandBuffer(renderState->m_commandBuffers[i]);
	}
}

void CreatePostVoxelizerState(
	RenderState& renderState,
	VulkanCore* core,
	VkCommandPool commandPool,
	VkDevice device,
	SwapChain* swapchain,
	AnisotropicVoxelTexture* avt)
{
	uint32_t width = swapchain->m_width;
	uint32_t height = swapchain->m_height;

	////////////////////////////////////////////////////////////////////////////////
	// Create queries
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_queryCount = 4;
	renderState.m_queryResults = (uint64_t*)malloc(sizeof(uint64_t)*renderState.m_queryCount);
	memset(renderState.m_queryResults, 0, sizeof(uint64_t)*renderState.m_queryCount);
	// Create query pool
	VkQueryPoolCreateInfo queryPoolInfo = {};
	queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = renderState.m_queryCount;
	VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, NULL, &renderState.m_queryPool));

	////////////////////////////////////////////////////////////////////////////////
	// Create the pipelineCache
	////////////////////////////////////////////////////////////////////////////////
	// create a default pipelinecache
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, NULL, &renderState.m_pipelineCache));

	////////////////////////////////////////////////////////////////////////////////
	// set framebuffers
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_framebufferCount = 0;
	renderState.m_framebuffers = NULL;

	////////////////////////////////////////////////////////////////////////////////
	// Create semaphores
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_semaphores)
	{
		renderState.m_semaphoreCount = avt->m_cascadeCount;
		renderState.m_semaphores = (VkSemaphore*)malloc(sizeof(VkSemaphore)*renderState.m_semaphoreCount);
		VkSemaphoreCreateInfo semInfo = VKTools::Initializers::SemaphoreCreateInfo();
		for (uint32_t i = 0; i < renderState.m_semaphoreCount; i++)
			vkCreateSemaphore(core->GetViewDevice(), &semInfo, NULL, &renderState.m_semaphores[i]);
	}
	
	////////////////////////////////////////////////////////////////////////////////
	// Create the Uniform Data
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_uniformData = NULL;
	renderState.m_uniformDataCount = 0;

	////////////////////////////////////////////////////////////////////////////////
	// Set the descriptorset layout
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_descriptorLayouts)
	{
		// Descriptorset
		renderState.m_descriptorLayoutCount = 1;
		renderState.m_descriptorLayouts = (VkDescriptorSetLayout*)malloc(renderState.m_descriptorLayoutCount * sizeof(VkDescriptorSetLayout));
		VkDescriptorSetLayoutBinding layoutBinding[PostVoxelizerDescriptorLayout::POSTVOXELIZERDESCRIPTOR_COUNT];
		// Binding 0 : Diffuse texture sampled image
		layoutBinding[POSTVOXELIZER_DESCRIPTOR_VOXELGRID_DIFFUSE] =
		{ POSTVOXELIZER_DESCRIPTOR_VOXELGRID_DIFFUSE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
		layoutBinding[POSTVOXELIZER_DESCRIPTOR_VOXELGRID_ALPHA] =
		{ POSTVOXELIZER_DESCRIPTOR_VOXELGRID_ALPHA, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };

		// Create the descriptorlayout
		VkDescriptorSetLayoutCreateInfo descriptorLayout = VKTools::Initializers::DescriptorSetLayoutCreateInfo(0, PostVoxelizerDescriptorLayout::POSTVOXELIZERDESCRIPTOR_COUNT, layoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, NULL, &renderState.m_descriptorLayouts[0]));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Create pipeline layout
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_pipelineLayout)
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = VKTools::Initializers::PipelineLayoutCreateInfo(0, 1, &renderState.m_descriptorLayouts[0]);
		VkPushConstantRange pushConstantRange = VKTools::Initializers::PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantComp));
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &renderState.m_pipelineLayout));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Create descriptor pool
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_descriptorPool)
	{
		VkDescriptorPoolSize poolSize[PostVoxelizerDescriptorLayout::POSTVOXELIZERDESCRIPTOR_COUNT];
		poolSize[POSTVOXELIZER_DESCRIPTOR_VOXELGRID_DIFFUSE] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
		poolSize[POSTVOXELIZER_DESCRIPTOR_VOXELGRID_ALPHA] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = VKTools::Initializers::DescriptorPoolCreateInfo(0, 1, POSTVOXELIZERDESCRIPTOR_COUNT, poolSize);
		//create the descriptorPool
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, NULL, &renderState.m_descriptorPool));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Create the descriptor set
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_descriptorSets)
	{
		renderState.m_descriptorSetCount = 1;
		renderState.m_descriptorSets = (VkDescriptorSet*)malloc(renderState.m_descriptorSetCount * sizeof(VkDescriptorSet));
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = VKTools::Initializers::DescriptorSetAllocateInfo(renderState.m_descriptorPool, 1, &renderState.m_descriptorLayouts[0]);
		//allocate the descriptorset with the pool
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &renderState.m_descriptorSets[0]));

		///////////////////////////////////////////////////////
		///// Set/Update the image and uniform buffer descriptorsets
		/////////////////////////////////////////////////////// 
		VkWriteDescriptorSet wds = {};
		// Bind the 3D voxel textures
		{
			wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wds.pNext = NULL;
			wds.dstSet = renderState.m_descriptorSets[0];
			wds.dstBinding = POSTVOXELIZER_DESCRIPTOR_VOXELGRID_DIFFUSE;
			wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			wds.descriptorCount = 1;
			wds.dstArrayElement = 0;
			wds.pImageInfo = &avt->m_descriptor[0];
			//update the descriptorset
			vkUpdateDescriptorSets(device, 1, &wds, 0, NULL);
		}
		{
			wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wds.pNext = NULL;
			wds.dstSet = renderState.m_descriptorSets[0];
			wds.dstBinding = POSTVOXELIZER_DESCRIPTOR_VOXELGRID_ALPHA;
			wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			wds.descriptorCount = 1;
			wds.dstArrayElement = 0;
			wds.pImageInfo = &avt->m_alphaDescriptor;
			//update the descriptorset
			vkUpdateDescriptorSets(device, 1, &wds, 0, NULL);
		}
	}

	///////////////////////////////////////////////////////
	///// Create the compute pipeline
	/////////////////////////////////////////////////////// 
	if (!renderState.m_pipelines)
	{
		renderState.m_pipelineCount = 1;
		renderState.m_pipelines = (VkPipeline*)malloc(renderState.m_pipelineCount * sizeof(VkPipeline));

		// Create pipeline		
		VkComputePipelineCreateInfo computePipelineCreateInfo = VKTools::Initializers::ComputePipelineCreateInfo(renderState.m_pipelineLayout, VK_FLAGS_NONE);
		// Shaders are loaded from the SPIR-V format, which can be generated from glsl
		Shader shaderStage;
		shaderStage = VKTools::LoadShader("shaders/voxelizerpost.comp.spv", "main", device, VK_SHADER_STAGE_COMPUTE_BIT);
		computePipelineCreateInfo.stage = shaderStage.m_shaderStage;
		VK_CHECK_RESULT(vkCreateComputePipelines(device, renderState.m_pipelineCache, 1, &computePipelineCreateInfo, nullptr, &renderState.m_pipelines[0]));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Build command buffers
	////////////////////////////////////////////////////////////////////////////////
	Parameter* parameter;
	parameter = (Parameter*)malloc(sizeof(Parameter));
	parameter->avt = avt;
	renderState.m_cmdBufferParameters = (BYTE*)parameter;

	renderState.m_CreateCommandBufferFunc = &BuildCommandBufferPostVoxelizerState;
	renderState.m_CreateCommandBufferFunc(&renderState, commandPool, core, 0, NULL, renderState.m_cmdBufferParameters);
}