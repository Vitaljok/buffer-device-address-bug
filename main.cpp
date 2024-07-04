#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

#include <GLFW/glfw3.h>
#include <VkBootstrap.h>
#include <fstream>
#include <glm/glm.hpp>

GLFWwindow *window;
vk::Instance instance;
vk::SurfaceKHR surface;
vk::PhysicalDevice physicalDevice;
vk::Device device;
vk::Queue queue;
uint32_t queueIndex;
vk::SwapchainKHR swapchain;
vk::Extent2D swapchainExtent;
std::vector<vk::Image> swapchainImages;
std::vector<vk::ImageView> swapchainImageViews;

vk::CommandPool cmdPool;
vk::CommandBuffer cmd;
vk::Fence renderFence;
vk::Semaphore swapchainSemaphore;
vk::Semaphore renderSemaphore;

vk::PipelineLayout pipelineLayout;
vk::Pipeline pipeline;

vk::Buffer vertexBuffer;
vk::DeviceMemory vertexMemory;
vk::DeviceAddress vertexAddress;

vk::Buffer indexBuffer;
vk::DeviceMemory indexMemory;

struct Vertex {
  glm::vec4 position;
  glm::vec4 color;
};

const std::vector<Vertex> vertices{{{0.5, 0.5, 0.0, 1.0}, {1.0, 0.0, 0.0, 1.0}},
                                   {{-0.5, 0.5, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}},
                                   {{-0.5, -0.5, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0}},
                                   {{0.5, -0.5, 0.0, 1.0}, {1.0, 1.0, 0.0, 1.0}}};

const std::vector<uint32_t> indices{0, 1, 2, 2, 3, 0};

const size_t verticesBytes = vertices.size() * sizeof(Vertex);
const size_t indicesBytes = indices.size() * sizeof(uint32_t);

void initVulkan() {
  auto vkb_inst = vkb::InstanceBuilder()
                      .set_app_name("Vulkan")
                      .request_validation_layers(true)
                      .use_default_debug_messenger()
                      .require_api_version(1, 3, 0)
                      .build()
                      .value();

  instance = vkb_inst.instance;
  VULKAN_HPP_DEFAULT_DISPATCHER.init();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

  {
    VkSurfaceKHR surf;
    glfwCreateWindowSurface(instance, window, nullptr, &surf);
    surface = surf;
  }

  {
    vk::PhysicalDeviceVulkan13Features features13;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    vk::PhysicalDeviceVulkan12Features features12;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    auto vkb_gpu = vkb::PhysicalDeviceSelector(vkb_inst)
                       .set_minimum_version(1, 3)
                       .set_required_features_12(features12)
                       .set_required_features_13(features13)
                       .set_surface(surface)
                       .select()
                       .value();

    physicalDevice = vkb_gpu.physical_device;

    auto vkb_device = vkb::DeviceBuilder(vkb_gpu).build().value();
    device = vkb_device.device;
    queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    queueIndex = vkb_device.get_queue_index(vkb::QueueType::graphics).value();
  }

  {
    vk::SurfaceFormatKHR format{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
    auto vkb_swapchain =
        vkb::SwapchainBuilder(physicalDevice, device, surface)
            .set_desired_format(format)
            .set_desired_present_mode(static_cast<VkPresentModeKHR>(vk::PresentModeKHR::eFifo))
            .set_desired_extent(1280, 720)
            .set_image_usage_flags(static_cast<VkImageUsageFlags>(
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment))
            .build()
            .value();

    swapchainExtent = vkb_swapchain.extent;
    swapchain = vkb_swapchain.swapchain;

    for (auto &one : vkb_swapchain.get_images().value()) {
      swapchainImages.push_back(one);
    }

    for (auto &one : vkb_swapchain.get_image_views().value()) {
      swapchainImageViews.push_back(one);
    }
  }

  cmdPool =
      device.createCommandPool({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueIndex});
  cmd = device.allocateCommandBuffers({cmdPool, vk::CommandBufferLevel::ePrimary, 1})[0];
  renderFence = device.createFence({vk::FenceCreateFlagBits::eSignaled});
  swapchainSemaphore = device.createSemaphore({});
  renderSemaphore = device.createSemaphore({});
}

std::vector<uint32_t> readSpvFile(const std::string &fileName) {
  std::ifstream file(fileName, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file");
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
  file.seekg(0);
  file.read((char *)buffer.data(), fileSize);
  file.close();
  return buffer;
}

void initPipeline() {
  vk::PushConstantRange range(vk::ShaderStageFlagBits::eVertex, 0, sizeof(vk::DeviceAddress));
  vk::PipelineLayoutCreateInfo layout;
  layout.setPushConstantRanges(range);
  pipelineLayout = device.createPipelineLayout(layout);

  vk::GraphicsPipelineCreateInfo info;

  vk::Format format = vk::Format::eB8G8R8A8Srgb;
  vk::PipelineRenderingCreateInfo renderInfo;
  renderInfo.setColorAttachmentFormats(format);
  info.pNext = &renderInfo; // Dynamic rendering

  auto vertCode = readSpvFile("simple.vert.spv");
  auto vertShader = device.createShaderModule({{}, vertCode});
  auto fragCode = readSpvFile("simple.frag.spv");
  auto fragShader = device.createShaderModule({{}, fragCode});
  std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{
      {{}, vk::ShaderStageFlagBits::eVertex, vertShader, "main"},
      {{}, vk::ShaderStageFlagBits::eFragment, fragShader, "main"}};
  info.setStages(shaderStages);

  vk::PipelineVertexInputStateCreateInfo vertexInput;
  info.pVertexInputState = &vertexInput;

  vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
  info.pInputAssemblyState = &inputAssembly;

  vk::PipelineViewportStateCreateInfo viewportState;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;
  info.pViewportState = &viewportState;

  vk::PipelineRasterizationStateCreateInfo rasterizer;
  rasterizer.polygonMode = vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.f;
  rasterizer.cullMode = vk::CullModeFlagBits::eNone;
  rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
  info.pRasterizationState = &rasterizer;

  vk::PipelineMultisampleStateCreateInfo multisampling;
  info.pMultisampleState = &multisampling;

  vk::PipelineColorBlendAttachmentState colorBlendAttachment;
  colorBlendAttachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  colorBlendAttachment.blendEnable = vk::False;

  vk::PipelineColorBlendStateCreateInfo colorBlend;
  colorBlend.setAttachments(colorBlendAttachment);
  colorBlend.logicOpEnable = vk::False;
  colorBlend.logicOp = vk::LogicOp::eCopy;
  info.pColorBlendState = &colorBlend;

  vk::PipelineDepthStencilStateCreateInfo depthStencil;
  info.pDepthStencilState = &depthStencil;

  std::array states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState({}, states);
  info.pDynamicState = &dynamicState;

  info.layout = pipelineLayout;

  auto [res, pipe] = device.createGraphicsPipeline(nullptr, info);
  assert(res == vk::Result::eSuccess);
  pipeline = pipe;

  device.destroyShaderModule(vertShader);
  device.destroyShaderModule(fragShader);
}

uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
  auto memProps = physicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("Suitable memory type not found");
}

void initBuffers() {
  {
    vk::BufferCreateInfo info({}, verticesBytes,
                              vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eStorageBuffer |
                                  vk::BufferUsageFlagBits::eShaderDeviceAddress);
    vertexBuffer = device.createBuffer(info);
    auto req = device.getBufferMemoryRequirements(vertexBuffer);

    vk::MemoryAllocateFlagsInfo flagsInfo;
    flagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    allocInfo.pNext = &flagsInfo;
    vertexMemory = device.allocateMemory(allocInfo);
    device.bindBufferMemory(vertexBuffer, vertexMemory, 0);
  }

  {
    vk::BufferCreateInfo info({}, indicesBytes,
                              vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eIndexBuffer);
    indexBuffer = device.createBuffer(info);
    auto req = device.getBufferMemoryRequirements(indexBuffer);

    vk::MemoryAllocateFlagsInfo flagsInfo;
    flagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    allocInfo.pNext = &flagsInfo;
    indexMemory = device.allocateMemory(allocInfo);
    device.bindBufferMemory(indexBuffer, indexMemory, 0);
  }
}

void uploadBuffer(vk::Buffer buffer, size_t size, void *data) {
  vk::BufferCreateInfo bufInfo;
  bufInfo.size = size;
  bufInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
  bufInfo.sharingMode = vk::SharingMode::eExclusive;
  auto stagingBuffer = device.createBuffer(bufInfo);

  auto req = device.getBufferMemoryRequirements(stagingBuffer);
  vk::MemoryAllocateInfo memAllocInfo;
  memAllocInfo.allocationSize = req.size;
  memAllocInfo.memoryTypeIndex =
      findMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                             vk::MemoryPropertyFlagBits::eHostCoherent);
  auto stagingMemory = device.allocateMemory(memAllocInfo);
  device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

  void *ptr = device.mapMemory(stagingMemory, 0, size);
  memcpy(ptr, data, size);
  device.unmapMemory(stagingMemory);

  cmd.reset();
  cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  cmd.copyBuffer(stagingBuffer, buffer, {{0, 0, size}});
  cmd.end();

  queue.submit(vk::SubmitInfo().setCommandBuffers(cmd));
  queue.waitIdle();

  device.destroyBuffer(stagingBuffer);
  device.freeMemory(stagingMemory);
}

void transitionImage(const vk::CommandBuffer &cmd, const vk::Image &image,
                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
  vk::ImageMemoryBarrier2 barrier;
  barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
  barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
  barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
  barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = vk::RemainingMipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = vk::RemainingArrayLayers;
  barrier.image = image;
  cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(barrier));
}

void drawFrame() {
  {
    auto res = device.waitForFences(renderFence, true, UINT64_MAX);
    assert(res == vk::Result::eSuccess);
    device.resetFences(renderFence);
  }

  uint32_t imageIndex;
  {
    auto res = device.acquireNextImageKHR(swapchain, UINT64_MAX, swapchainSemaphore, nullptr,
                                          &imageIndex);
    assert(res == vk::Result::eSuccess);
  }

  cmd.reset();
  cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  transitionImage(cmd, swapchainImages[imageIndex], vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eColorAttachmentOptimal);

  vk::RenderingAttachmentInfo colorAttachment(swapchainImageViews[imageIndex],
                                              vk::ImageLayout::eColorAttachmentOptimal);
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.clearValue.color = {0.1f, 0.1f, 0.1f, 1.0f};

  vk::Rect2D renderArea({0, 0}, swapchainExtent);
  cmd.setViewport(0,
                  vk::Viewport(0, 0, swapchainExtent.width, swapchainExtent.height, 0.0f, 1.0f));
  cmd.setScissor(0, {renderArea});

  vk::RenderingInfo renderInfo;
  renderInfo.renderArea = renderArea;
  renderInfo.layerCount = 1;
  renderInfo.setColorAttachments(colorAttachment);
  cmd.beginRendering(renderInfo);

  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
  cmd.pushConstants<vk::DeviceAddress>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                       vertexAddress);
  cmd.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
  cmd.drawIndexed(indices.size(), 1, 0, 0, 0);

  cmd.endRendering();

  transitionImage(cmd, swapchainImages[imageIndex], vk::ImageLayout::eColorAttachmentOptimal,
                  vk::ImageLayout::ePresentSrcKHR);
  cmd.end();

  {
    vk::CommandBufferSubmitInfo cmdInfo(cmd);
    vk::SemaphoreSubmitInfo waitInfo(swapchainSemaphore, 1,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    vk::SemaphoreSubmitInfo signalInfo(renderSemaphore, 1,
                                       vk::PipelineStageFlagBits2::eAllGraphics);
    vk::SubmitInfo2 submitInfo(vk::SubmitFlags(), waitInfo, cmdInfo, signalInfo);
    queue.submit2(submitInfo, renderFence);
  }

  {
    vk::PresentInfoKHR presentInfo(renderSemaphore, swapchain, imageIndex);
    auto res = queue.presentKHR(&presentInfo);
    assert(res == vk::Result::eSuccess);
  }
}

int main() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window = glfwCreateWindow(1280, 720, "Vulkan", nullptr, nullptr);
  initVulkan();
  initPipeline();
  initBuffers();

  uploadBuffer(vertexBuffer, verticesBytes, (void *)vertices.data());
  vertexAddress = device.getBufferAddress({vertexBuffer});

  uploadBuffer(indexBuffer, indicesBytes, (void *)indices.data());

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    drawFrame();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}