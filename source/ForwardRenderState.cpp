#include "PipelineStates.h"

#include <array>

#include "VCTPipelineDefines.h"
#include "SwapChain.h"
#include "VulkanCore.h"
#include "Shader.h"
#include "DataTypes.h"

struct Parameter
{
	VkRenderPass renderpass;
	vk_mesh_s* meshes;
	uint32_t meshCount;
	VkDescriptorSet staticDescriptorSet;
};

void BuildCommandBufferForwardRenderState(
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
	VkDevice device = core->GetViewDevice();

	////////////////////////////////////////////////////////////////////////////////
	// Rebuild the parameters
	////////////////////////////////////////////////////////////////////////////////
	VkRenderPass renderpass = ((Parameter*)parameters)->renderpass;
	vk_mesh_s* meshes = ((Parameter*)parameters)->meshes;
	uint32_t meshCount = ((Parameter*)parameters)->meshCount;
	VkDescriptorSet staticDescriptorSet = ((Parameter*)parameters)->staticDescriptorSet;

	////////////////////////////////////////////////////////////////////////////////
	// Rebuild the command buffers
	////////////////////////////////////////////////////////////////////////////////
	renderState->m_commandBufferCount = framebufferCount;
	renderState->m_commandBuffers = (VkCommandBuffer*)malloc(sizeof(VkCommandBuffer)*renderState->m_commandBufferCount);
	for (uint32_t i = 0; i < renderState->m_commandBufferCount; i++)
		renderState->m_commandBuffers[i] = VKTools::Initializers::CreateCommandBuffer(commandpool, device, VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

	////////////////////////////////////////////////////////////////////////////////
	// Record command buffer
	////////////////////////////////////////////////////////////////////////////////
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	VkClearValue clearValues[2];
	VkClearColorValue defaultClearColor = { { 0.025f, 0.025f, 0.025f, 1.0f } };
	clearValues[0].color = defaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = renderpass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	for (uint32_t i = 0; i < framebufferCount; i++)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = framebuffers[i];

		//begin
		VK_CHECK_RESULT(vkBeginCommandBuffer(renderState->m_commandBuffers[i], &cmdBufInfo));

		vkCmdResetQueryPool(renderState->m_commandBuffers[i], renderState->m_queryPool, 0, 4);
		vkCmdWriteTimestamp(renderState->m_commandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderState->m_queryPool, 0);

		// Start the first sub pass specified in our default render pass setup by the base class
		// This will clear the color and depth attachment
		vkCmdBeginRenderPass(renderState->m_commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Update dynamic viewport state
		VkViewport viewport = {};
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.minDepth = (float) 0.0f;
		viewport.maxDepth = (float) 1.0f;
		vkCmdSetViewport(renderState->m_commandBuffers[i], 0, 1, &viewport);

		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent.width = width;
		scissor.extent.height = height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(renderState->m_commandBuffers[i], 0, 1, &scissor);

		// Bind the rendering pipeline (including the shaders)
		vkCmdBindPipeline(renderState->m_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderState->m_pipelines[0]);

		// Bind descriptor sets describing shader binding points
		uint32_t doffset = 0;
		vkCmdBindDescriptorSets(renderState->m_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderState->m_pipelineLayout, 0, 1, &staticDescriptorSet, 1, &doffset);
		uint32_t d = 0;
		for (uint32_t m = 0; m < meshCount; m++)
		{
			//select the current mesh
			vk_mesh_s* mesh = &meshes[m];
			//bind vertexbuffer per mesh
			vkCmdBindVertexBuffers(renderState->m_commandBuffers[i], 0, mesh->vbvCount, mesh->vertexResources, mesh->vertexOffsets);
			for (uint32_t j = 0; j < mesh->submeshCount; j++)
			{
				//TODO: BIND new descriptorset
				VkDescriptorSet descriptorset = renderState->m_descriptorSets[(i*DYNAMIC_DESCRIPTOR_SET_COUNT) + d++];

				//bind the textures to the correct format
				//format: stype,pnext,scSet,srcBinding,srcArrayelement,dstSet,dstbinding,dstarrayelement,descriptorcount
				VkCopyDescriptorSet textureDescriptorSets[TEXTURE_NUM];
				//diffuse texture
				VkCopyDescriptorSet diffuse;
				diffuse.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
				diffuse.pNext = NULL;
				diffuse.srcSet = staticDescriptorSet;
				diffuse.srcBinding = STATIC_DESCRIPTOR_IMAGE;
				diffuse.srcArrayElement = mesh->submeshes[j].textureIndex[DIFFUSE_TEXTURE];
				diffuse.dstSet = descriptorset;
				diffuse.dstBinding = FORWARDRENDER_DESCRIPTOR_IMAGE_DIFFUSE;
				diffuse.dstArrayElement = 0;
				diffuse.descriptorCount = 1;
				textureDescriptorSets[DIFFUSE_TEXTURE] = diffuse;
				//normal texture
				VkCopyDescriptorSet normal;
				normal.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
				normal.pNext = NULL;
				normal.srcSet = staticDescriptorSet;
				normal.srcBinding = STATIC_DESCRIPTOR_IMAGE;
				normal.srcArrayElement = mesh->submeshes[j].textureIndex[NORMAL_TEXTURE];
				normal.dstSet = descriptorset;
				normal.dstBinding = FORWARDRENDER_DESCRIPTOR_IMAGE_NORMAL;
				normal.dstArrayElement = 0;
				normal.descriptorCount = 1;
				textureDescriptorSets[NORMAL_TEXTURE] = normal;
				//opacity teture
				VkCopyDescriptorSet opacity;
				opacity.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
				opacity.pNext = NULL;
				opacity.srcSet = staticDescriptorSet;
				opacity.srcBinding = STATIC_DESCRIPTOR_IMAGE;
				opacity.srcArrayElement = (uint32_t)mesh->submeshes[j].textureIndex[OPACITY_TEXTURE];
				opacity.dstSet = descriptorset;
				opacity.dstBinding = FORWARDRENDER_DESCRIPTOR_IMAGE_OPACITY;
				opacity.dstArrayElement = 0;
				opacity.descriptorCount = 1;
				textureDescriptorSets[OPACITY_TEXTURE] = opacity;
				// Update the descriptors
				vkUpdateDescriptorSets(device, 0, NULL, (uint32_t)TEXTURE_NUM, textureDescriptorSets);
				// Bind descriptor sets describing shader binding points
				vkCmdBindDescriptorSets(renderState->m_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderState->m_pipelineLayout, 1, 1, &descriptorset, 0, NULL);
				// Bind triangle indices
				vkCmdBindIndexBuffer(renderState->m_commandBuffers[i], mesh->submeshes[j].ibv.buffer, mesh->submeshes[j].ibv.offset, mesh->submeshes[j].ibv.format);
				// Draw indexed triangle
				vkCmdDrawIndexed(renderState->m_commandBuffers[i], (uint32_t)mesh->submeshes[j].ibv.count, 1, 0, 0, 0);
			}
		}

		vkCmdWriteTimestamp(renderState->m_commandBuffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, renderState->m_queryPool, 1);

		// End renderpass
		vkCmdEndRenderPass(renderState->m_commandBuffers[i]);
		// End commandbuffer
		VK_CHECK_RESULT(vkEndCommandBuffer(renderState->m_commandBuffers[i]));
	}
}

extern void CreateForwardRenderState(
	RenderState& renderState,
	VkFramebuffer* framebuffers,
	uint32_t framebufferCount,
	VulkanCore* core,
	VkCommandPool commandPool,
	SwapChain* swapchain,
	VkDescriptorSet staticDescriptorSet,
	Vertices* vertices,
	vk_mesh_s* meshes,
	uint32_t meshCount,
	VkRenderPass renderpass,
	VkDescriptorSetLayout staticDescLayout)		// Forward renderer pipeline state
{
	uint32_t width = swapchain->m_width;
	uint32_t height = swapchain->m_height;
	VkDevice device = core->GetViewDevice();

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
	if (!renderState.m_pipelineCache)
	{
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, NULL, &renderState.m_pipelineCache));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Create semaphores
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_semaphores)
	{
		renderState.m_semaphoreCount = 1;
		renderState.m_semaphores = (VkSemaphore*)malloc(sizeof(VkSemaphore)*renderState.m_semaphoreCount);
		VkSemaphoreCreateInfo semInfo = VKTools::Initializers::SemaphoreCreateInfo();
		for (uint32_t i = 0; i < renderState.m_semaphoreCount; i++)
			vkCreateSemaphore(device, &semInfo, NULL, &renderState.m_semaphores[i]);
	}
	
	////////////////////////////////////////////////////////////////////////////////
	// set framebuffers
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_framebufferCount = 0;
	renderState.m_framebuffers = NULL;

	////////////////////////////////////////////////////////////////////////////////
	// Create the Uniform Data
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_uniformDataCount = 0;
	renderState.m_uniformData = NULL;

	////////////////////////////////////////////////////////////////////////////////
	// Create renderpass
	////////////////////////////////////////////////////////////////////////////////
	renderState.m_renderpass = NULL;

	////////////////////////////////////////////////////////////////////////////////
	// Create the descriptorlayout
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_descriptorLayouts)
	{
		renderState.m_descriptorLayoutCount = 1;
		renderState.m_descriptorLayouts = (VkDescriptorSetLayout*)malloc(sizeof(VkDescriptorSetLayout)*renderState.m_descriptorLayoutCount);
		//dynamic descriptorset
		VkDescriptorSetLayoutBinding layoutBinding[FORWARDRENDER_DESCRIPTOR_COUNT];
		// Binding 0 : diffuse texture sampled image
		layoutBinding[FORWARDRENDER_DESCRIPTOR_IMAGE_DIFFUSE] =
		{ FORWARDRENDER_DESCRIPTOR_IMAGE_DIFFUSE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
		// Binding 1 : normal texture sampled image
		layoutBinding[FORWARDRENDER_DESCRIPTOR_IMAGE_NORMAL] =
		{ FORWARDRENDER_DESCRIPTOR_IMAGE_NORMAL, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
		// Binding 2: opacity texture sampled image
		layoutBinding[FORWARDRENDER_DESCRIPTOR_IMAGE_OPACITY] =
		{ FORWARDRENDER_DESCRIPTOR_IMAGE_OPACITY, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
		// set the createinfo
		//create the descriptorlayout
		VkDescriptorSetLayoutCreateInfo descriptorLayout = VKTools::Initializers::DescriptorSetLayoutCreateInfo(0, FORWARDRENDER_DESCRIPTOR_COUNT, layoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, NULL, &renderState.m_descriptorLayouts[0]));
	}
	
	////////////////////////////////////////////////////////////////////////////////
	// Create layout
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_pipelineLayout)
	{
		VkDescriptorSetLayout dLayouts[] = { staticDescLayout, renderState.m_descriptorLayouts[0] };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = VKTools::Initializers::PipelineLayoutCreateInfo(0, 2, dLayouts);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &renderState.m_pipelineLayout));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Create descriptor pool
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_descriptorPool)
	{
		VkDescriptorPoolSize poolSize[FORWARDRENDER_DESCRIPTOR_COUNT];
		poolSize[FORWARDRENDER_DESCRIPTOR_IMAGE_DIFFUSE] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE , framebufferCount * DYNAMIC_DESCRIPTOR_SET_COUNT };
		poolSize[FORWARDRENDER_DESCRIPTOR_IMAGE_NORMAL] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE , framebufferCount * DYNAMIC_DESCRIPTOR_SET_COUNT };
		poolSize[FORWARDRENDER_DESCRIPTOR_IMAGE_OPACITY] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE , framebufferCount * DYNAMIC_DESCRIPTOR_SET_COUNT };
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = VKTools::Initializers::DescriptorPoolCreateInfo(0, framebufferCount * DYNAMIC_DESCRIPTOR_SET_COUNT, FORWARDRENDER_DESCRIPTOR_COUNT, poolSize);
		//create the descriptorPool
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, NULL, &renderState.m_descriptorPool));
	}
	
	////////////////////////////////////////////////////////////////////////////////
	// Create the descriptor set
	////////////////////////////////////////////////////////////////////////////////
	//allocate the requirered descriptorsets
	if (!renderState.m_descriptorSets)
	{
		renderState.m_descriptorSetCount = DYNAMIC_DESCRIPTOR_SET_COUNT * framebufferCount;
		renderState.m_descriptorSets = (VkDescriptorSet*)malloc(renderState.m_descriptorSetCount * sizeof(VkDescriptorSet));
		for (uint32_t i = 0; i < framebufferCount; i++)
		{
			for (uint32_t j = 0; j < DYNAMIC_DESCRIPTOR_SET_COUNT; j++)
			{
				VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = VKTools::Initializers::DescriptorSetAllocateInfo(renderState.m_descriptorPool, 1, &renderState.m_descriptorLayouts[0]);
				//allocate the descriptorset with the pool
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &renderState.m_descriptorSets[(i*DYNAMIC_DESCRIPTOR_SET_COUNT) + j]));
			}
		}
	}
	
	////////////////////////////////////////////////////////////////////////////////
	// Create Pipeline
	////////////////////////////////////////////////////////////////////////////////
	if (!renderState.m_pipelines)
	{
		renderState.m_pipelineCount = 1;
		renderState.m_pipelines = (VkPipeline*)malloc(renderState.m_pipelineCount * sizeof(VkPipeline));

		// Create the pipeline input assembly state info
		VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyCreateInfo = {};
		pipelineInputAssemblyCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		//This pipeline renders vertex data as triangles
		pipelineInputAssemblyCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// RASTERIZATION STATE
		VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo = {};
		pipelineRasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		// Solid polygon mode
		pipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
		// Enable culling
		pipelineRasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		// Set vert read to counter clockwise
		pipelineRasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		pipelineRasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.lineWidth = 1.0f;

		// Color blend state
		VkPipelineColorBlendStateCreateInfo colorBlendState = {};
		colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendState.attachmentCount = 1;
		// Don't enable blending for now
		//blending is not used in this example
		VkPipelineColorBlendAttachmentState blendAttatchmentState[1] = {};
		blendAttatchmentState[0].colorWriteMask = 0xf;	//white
		blendAttatchmentState[0].blendEnable = VK_FALSE;
		colorBlendState.pAttachments = blendAttatchmentState;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		//one viewport for now
		viewportState.viewportCount = 1;
		//scissor rectable
		viewportState.scissorCount = 1;

		//enable dynamic state
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		// The dynamic state properties themselves are stored in the command buffer
		std::vector<VkDynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = (uint32_t)dynamicStateEnables.size();

		// Depth and stencil state
		// Describes depth and stenctil test and compare ops
		VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
		// Basic depth compare setup with depth writes and depth test enabled
		// No stencil used 
		depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = depthStencilState.back;

		// Multi sampling state
		VkPipelineMultisampleStateCreateInfo multisampleState = {};
		multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleState.pSampleMask = NULL;
		// No multi sampling used in this example
		multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Load shaders
		// Shaders are loaded from the SPIR-V format, which can be generated from glsl
		std::array<Shader, 2> shaderStages;
		shaderStages[0] = VKTools::LoadShader("shaders/diffuse.vert.spv", "main", device, VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = VKTools::LoadShader("shaders/diffuse.frag.spv", "main", device, VK_SHADER_STAGE_FRAGMENT_BIT);

		// Assign states
		// Assign pipeline state create information
		std::vector<VkPipelineShaderStageCreateInfo> shaderStagesData;
		for (int i = 0; i < shaderStages.size(); i++)
			shaderStagesData.push_back(shaderStages[i].m_shaderStage);

		// pipelineinfo for creating the pipeline
		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.layout = renderState.m_pipelineLayout;
		pipelineCreateInfo.stageCount = (uint32_t)shaderStagesData.size();
		pipelineCreateInfo.pStages = shaderStagesData.data();
		pipelineCreateInfo.pVertexInputState = &vertices->inputState;
		pipelineCreateInfo.pInputAssemblyState = &pipelineInputAssemblyCreateInfo;
		pipelineCreateInfo.pRasterizationState = &pipelineRasterizationStateCreateInfo;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.renderPass = renderpass;
		pipelineCreateInfo.pDynamicState = &dynamicState;

		// Create rendering pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, renderState.m_pipelineCache, 1, &pipelineCreateInfo, NULL, &renderState.m_pipelines[0]));
	}

	////////////////////////////////////////////////////////////////////////////////
	// Build command buffers
	////////////////////////////////////////////////////////////////////////////////
	Parameter* parameter;
	parameter = (Parameter*)malloc(sizeof(Parameter));
	parameter->renderpass = renderpass;
	parameter->meshes = meshes;
	parameter->meshCount = meshCount;
	parameter->staticDescriptorSet = staticDescriptorSet;
	renderState.m_cmdBufferParameters = (BYTE*)parameter;

	renderState.m_CreateCommandBufferFunc = &BuildCommandBufferForwardRenderState;
	renderState.m_CreateCommandBufferFunc(&renderState, commandPool, core, framebufferCount, framebuffers, renderState.m_cmdBufferParameters);
}
