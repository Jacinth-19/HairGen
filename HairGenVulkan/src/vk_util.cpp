#include "vk_util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "shaders_generated.hpp"

namespace hgen {

void check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s failed (VkResult %d)", what, static_cast<int>(r));
    throw std::runtime_error(buf);
  }
}

namespace {

uint32_t score_device(const VkPhysicalDeviceProperties& p) {
  switch (p.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 400;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 300;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 200;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 100;
    default: return 50;
  }
}

}  // namespace

void Gpu::init(const std::string& icd_override, bool enable_validation, bool prefer_cpu) {
  load_vulkan(api, icd_override);

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "HairGenVulkan";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "HairGen";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_0;
  if (api.vkEnumerateInstanceVersion) {
    uint32_t v = VK_API_VERSION_1_0;
    if (api.vkEnumerateInstanceVersion(&v) == VK_SUCCESS) {
      // Stay at 1.0: every code path here is core Vulkan 1.0.
      api_version = VK_API_VERSION_1_0;
    }
  }

  // Optional validation layer (only if present).
  std::vector<const char*> layers;
  if (enable_validation) {
    uint32_t count = 0;
    auto* enumer = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
        api.instance_gpa ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(api.instance_gpa)(
                               nullptr, "vkEnumerateInstanceLayerProperties")
                         : nullptr);
    if (enumer && enumer(&count, nullptr) == VK_SUCCESS) {
      std::vector<VkLayerProperties> props(count);
      enumer(&count, props.data());
      for (auto& p : props)
        if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
          layers.push_back("VK_LAYER_KHRONOS_validation");
          validation = true;
        }
    }
    if (!validation) fprintf(stderr, "[hairgen] validation layer not available, continuing without\n");
  }

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  check(api.vkCreateInstance(&ci, nullptr, &instance), "vkCreateInstance");

  // Instance level functions.
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(api.instance_gpa);
  auto iget = [&](const char* name) { return gipa(instance, name); };
  api.vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(iget("vkDestroyInstance"));
  api.vkEnumeratePhysicalDevices =
      reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(iget("vkEnumeratePhysicalDevices"));
  api.vkGetPhysicalDeviceProperties =
      reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(iget("vkGetPhysicalDeviceProperties"));
  api.vkGetPhysicalDeviceFeatures =
      reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(iget("vkGetPhysicalDeviceFeatures"));
  api.vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
      iget("vkGetPhysicalDeviceMemoryProperties"));
  api.vkGetPhysicalDeviceQueueFamilyProperties =
      reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
          iget("vkGetPhysicalDeviceQueueFamilyProperties"));
  api.vkGetPhysicalDeviceFormatProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
      iget("vkGetPhysicalDeviceFormatProperties"));
  api.vkGetPhysicalDeviceImageFormatProperties =
      reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
          iget("vkGetPhysicalDeviceImageFormatProperties"));
  api.vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(iget("vkCreateDevice"));

  uint32_t device_count = 0;
  check(api.vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "vkEnumeratePhysicalDevices");
  if (device_count == 0) throw std::runtime_error("no Vulkan device found");
  std::vector<VkPhysicalDevice> devices(device_count);
  api.vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

  int best = -1, best_score = -1;
  for (uint32_t i = 0; i < device_count; ++i) {
    VkPhysicalDeviceProperties p{};
    api.vkGetPhysicalDeviceProperties(devices[i], &p);
    int score = score_device(p);
    if (prefer_cpu && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) score += 1000;
    if (score > best_score) { best_score = score; best = static_cast<int>(i); }
  }
  physical = devices[best];
  api.vkGetPhysicalDeviceProperties(physical, &props);
  api.vkGetPhysicalDeviceFeatures(physical, &features);
  api.vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

  uint32_t fam_count = 0;
  api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &fam_count, nullptr);
  std::vector<VkQueueFamilyProperties> fams(fam_count);
  api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &fam_count, fams.data());
  bool found = false;
  for (uint32_t i = 0; i < fam_count; ++i) {
    if (fams[i].queueCount > 0 && (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      queue_family = i;
      found = true;
      break;
    }
  }
  if (!found) throw std::runtime_error("no graphics queue family");

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.pEnabledFeatures = &features;  // request what the device already has
  check(api.vkCreateDevice(physical, &dci, nullptr, &device), "vkCreateDevice");
  // Device level functions must be resolved through the device.
  auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(iget("vkGetDeviceProcAddr"));
  if (!gdpa) {
    auto get_dev = reinterpret_cast<PFN_vkGetDeviceProcAddr>(iget("vkGetDeviceProcAddr"));
    gdpa = get_dev;
  }
  if (!gdpa) throw std::runtime_error("vkGetDeviceProcAddr unavailable");
  api.vkGetDeviceProcAddr = gdpa;
  auto dget = [&](const char* name) { return gdpa(device, name); };
#define HGEN_LOAD_DEV(name) api.name = reinterpret_cast<PFN_##name>(dget(#name));
  HGEN_LOAD_DEV(vkDestroyDevice)
  HGEN_LOAD_DEV(vkCreateBuffer)
  HGEN_LOAD_DEV(vkDestroyBuffer)
  HGEN_LOAD_DEV(vkGetBufferMemoryRequirements)
  HGEN_LOAD_DEV(vkAllocateMemory)
  HGEN_LOAD_DEV(vkFreeMemory)
  HGEN_LOAD_DEV(vkBindBufferMemory)
  HGEN_LOAD_DEV(vkMapMemory)
  HGEN_LOAD_DEV(vkUnmapMemory)
  HGEN_LOAD_DEV(vkCreateImage)
  HGEN_LOAD_DEV(vkDestroyImage)
  HGEN_LOAD_DEV(vkGetImageMemoryRequirements)
  HGEN_LOAD_DEV(vkBindImageMemory)
  HGEN_LOAD_DEV(vkCreateImageView)
  HGEN_LOAD_DEV(vkDestroyImageView)
  HGEN_LOAD_DEV(vkCreateSampler)
  HGEN_LOAD_DEV(vkDestroySampler)
  HGEN_LOAD_DEV(vkCreateShaderModule)
  HGEN_LOAD_DEV(vkDestroyShaderModule)
  HGEN_LOAD_DEV(vkCreateDescriptorSetLayout)
  HGEN_LOAD_DEV(vkDestroyDescriptorSetLayout)
  HGEN_LOAD_DEV(vkCreateDescriptorPool)
  HGEN_LOAD_DEV(vkDestroyDescriptorPool)
  HGEN_LOAD_DEV(vkAllocateDescriptorSets)
  HGEN_LOAD_DEV(vkUpdateDescriptorSets)
  HGEN_LOAD_DEV(vkCreatePipelineLayout)
  HGEN_LOAD_DEV(vkDestroyPipelineLayout)
  HGEN_LOAD_DEV(vkCreateGraphicsPipelines)
  HGEN_LOAD_DEV(vkDestroyPipeline)
  HGEN_LOAD_DEV(vkCreateRenderPass)
  HGEN_LOAD_DEV(vkDestroyRenderPass)
  HGEN_LOAD_DEV(vkCreateFramebuffer)
  HGEN_LOAD_DEV(vkDestroyFramebuffer)
  HGEN_LOAD_DEV(vkCreateCommandPool)
  HGEN_LOAD_DEV(vkDestroyCommandPool)
  HGEN_LOAD_DEV(vkAllocateCommandBuffers)
  HGEN_LOAD_DEV(vkFreeCommandBuffers)
  HGEN_LOAD_DEV(vkBeginCommandBuffer)
  HGEN_LOAD_DEV(vkEndCommandBuffer)
  HGEN_LOAD_DEV(vkResetCommandBuffer)
  HGEN_LOAD_DEV(vkQueueSubmit)
  HGEN_LOAD_DEV(vkQueueWaitIdle)
  HGEN_LOAD_DEV(vkDeviceWaitIdle)
  HGEN_LOAD_DEV(vkCmdBeginRenderPass)
  HGEN_LOAD_DEV(vkCmdEndRenderPass)
  HGEN_LOAD_DEV(vkCmdBindPipeline)
  HGEN_LOAD_DEV(vkCmdBindDescriptorSets)
  HGEN_LOAD_DEV(vkCmdBindVertexBuffers)
  HGEN_LOAD_DEV(vkCmdBindIndexBuffer)
  HGEN_LOAD_DEV(vkCmdDraw)
  HGEN_LOAD_DEV(vkCmdDrawIndexed)
  HGEN_LOAD_DEV(vkCmdPushConstants)
  HGEN_LOAD_DEV(vkCmdSetViewport)
  HGEN_LOAD_DEV(vkCmdSetScissor)
  HGEN_LOAD_DEV(vkCmdCopyImageToBuffer)
  HGEN_LOAD_DEV(vkCmdCopyBufferToImage)
  HGEN_LOAD_DEV(vkCmdBlitImage)
  HGEN_LOAD_DEV(vkCmdPipelineBarrier)
  HGEN_LOAD_DEV(vkCreateFence)
  HGEN_LOAD_DEV(vkDestroyFence)
  HGEN_LOAD_DEV(vkWaitForFences)
  HGEN_LOAD_DEV(vkResetFences)
  HGEN_LOAD_DEV(vkGetDeviceQueue)
#undef HGEN_LOAD_DEV
  {
    const char* missing = nullptr;
#define HGEN_CHECK(name) if (!api.name && !missing) missing = #name;
    HGEN_VK_FUNCTIONS(HGEN_CHECK)
#undef HGEN_CHECK
    if (missing) fprintf(stderr, "[hairgen] warning: missing entry point %s\n", missing);
  }
  if (!api.vkGetDeviceQueue) throw std::runtime_error("vkGetDeviceQueue unavailable");
  api.vkGetDeviceQueue(device, queue_family, 0, &queue);

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = queue_family;
  check(api.vkCreateCommandPool(device, &pci, nullptr, &command_pool), "vkCreateCommandPool");
}

void Gpu::shutdown() {
  if (device != VK_NULL_HANDLE) {
    api.vkDeviceWaitIdle(device);
    if (command_pool) api.vkDestroyCommandPool(device, command_pool, nullptr);
    api.vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    api.vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

uint32_t Gpu::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const {
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & flags) == flags) return i;
  }
  throw std::runtime_error("no suitable memory type");
}

VkCommandBuffer Gpu::begin_once() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = command_pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  check(api.vkAllocateCommandBuffers(device, &ai, &cmd), "vkAllocateCommandBuffers");
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(api.vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
  return cmd;
}

void Gpu::end_once(VkCommandBuffer cmd) {
  check(api.vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  check(api.vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
  check(api.vkQueueWaitIdle(queue), "vkQueueWaitIdle");
  api.vkFreeCommandBuffers(device, command_pool, 1, &cmd);
}

void Gpu::wait_idle() { api.vkDeviceWaitIdle(device); }

Buffer Gpu::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags,
                          const void* data) {
  Buffer b;
  b.size = size;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(api.vkCreateBuffer(device, &bi, nullptr, &b.buffer), "vkCreateBuffer");
  VkMemoryRequirements req{};
  api.vkGetBufferMemoryRequirements(device, b.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, mem_flags);
  check(api.vkAllocateMemory(device, &ai, nullptr, &b.memory), "vkAllocateMemory");
  check(api.vkBindBufferMemory(device, b.buffer, b.memory, 0), "vkBindBufferMemory");
  if (data && (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
    void* ptr = nullptr;
    check(api.vkMapMemory(device, b.memory, 0, size, 0, &ptr), "vkMapMemory");
    std::memcpy(ptr, data, static_cast<size_t>(size));
    if (!(mem_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      // coherent memory is requested by the callers, nothing to flush here
    }
    api.vkUnmapMemory(device, b.memory);
  }
  if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    check(api.vkMapMemory(device, b.memory, 0, size, 0, &b.mapped), "vkMapMemory");
  }
  return b;
}

void Gpu::destroy_buffer(Buffer& b) {
  if (b.mapped) api.vkUnmapMemory(device, b.memory);
  if (b.buffer) api.vkDestroyBuffer(device, b.buffer, nullptr);
  if (b.memory) api.vkFreeMemory(device, b.memory, nullptr);
  b = Buffer{};
}

Image Gpu::create_image_2d(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                           VkSampleCountFlagBits samples, uint32_t mip_levels) {
  Image img;
  img.width = w; img.height = h; img.depth = 1; img.format = format;
  img.mips = mip_levels < 1 ? 1 : mip_levels;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = format;
  ci.extent = {w, h, 1};
  ci.mipLevels = img.mips;
  ci.arrayLayers = 1;
  ci.samples = samples;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(api.vkCreateImage(device, &ci, nullptr, &img.image), "vkCreateImage");
  VkMemoryRequirements req{};
  api.vkGetImageMemoryRequirements(device, img.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(api.vkAllocateMemory(device, &ai, nullptr, &img.memory), "vkAllocateMemory");
  check(api.vkBindImageMemory(device, img.image, img.memory, 0), "vkBindImageMemory");

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = format;
  bool depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM ||
               format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
  vi.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = img.mips;
  vi.subresourceRange.layerCount = 1;
  check(api.vkCreateImageView(device, &vi, nullptr, &img.view), "vkCreateImageView");
  return img;
}

Image Gpu::create_image_3d(uint32_t w, uint32_t h, uint32_t d, VkFormat format,
                           VkImageUsageFlags usage) {
  Image img;
  img.width = w; img.height = h; img.depth = d; img.format = format;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_3D;
  ci.format = format;
  ci.extent = {w, h, d};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(api.vkCreateImage(device, &ci, nullptr, &img.image), "vkCreateImage(3D)");
  VkMemoryRequirements req{};
  api.vkGetImageMemoryRequirements(device, img.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(api.vkAllocateMemory(device, &ai, nullptr, &img.memory), "vkAllocateMemory");
  check(api.vkBindImageMemory(device, img.image, img.memory, 0), "vkBindImageMemory");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
  vi.format = format;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = 1;
  vi.subresourceRange.layerCount = 1;
  check(api.vkCreateImageView(device, &vi, nullptr, &img.view), "vkCreateImageView(3D)");
  return img;
}

void Gpu::destroy_image(Image& img) {
  if (img.view) api.vkDestroyImageView(device, img.view, nullptr);
  if (img.image) api.vkDestroyImage(device, img.image, nullptr);
  if (img.memory) api.vkFreeMemory(device, img.memory, nullptr);
  img = Image{};
}

void Gpu::upload_image(Image& img, const void* data, VkDeviceSize bytes) {
  Buffer staging = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 data);
  VkCommandBuffer cmd = begin_once();
  const uint32_t mips = img.mips < 1 ? 1 : img.mips;

  auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                     uint32_t base, uint32_t count) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = base;
    b.subresourceRange.levelCount = count;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT |
                                 (dst & VK_ACCESS_SHADER_READ_BIT ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0),
                             0, 0, nullptr, 0, nullptr, 1, &b);
  };

  barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT, 0, mips);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {img.width, img.height, img.depth};
  api.vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                             &region);

  // Box-filtered mip chain. Level 0 becomes the blit source, every later level
  // is written once, so the barriers simply walk down the chain.
  for (uint32_t i = 1; i < mips; ++i) {
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, i - 1, 1);
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {int32_t(std::max(1u, img.width >> (i - 1))),
                          int32_t(std::max(1u, img.height >> (i - 1))), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {int32_t(std::max(1u, img.width >> i)),
                          int32_t(std::max(1u, img.height >> i)), 1};
    api.vkCmdBlitImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
  }

  if (mips > 1)
    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 0, mips - 1);
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, mips - 1, 1);
  end_once(cmd);
  img.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  destroy_buffer(staging);
}

VkSampler Gpu::create_sampler(VkFilter filter, VkSamplerAddressMode wrap, bool compare,
                              bool mip_linear) {
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = filter;
  si.minFilter = filter;
  si.mipmapMode = mip_linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = wrap;
  si.addressModeV = wrap;
  si.addressModeW = wrap;
  si.compareEnable = compare ? VK_TRUE : VK_FALSE;
  si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  si.maxLod = mip_linear ? VK_LOD_CLAMP_NONE : 0.0f;
  VkSampler sampler = VK_NULL_HANDLE;
  check(api.vkCreateSampler(device, &si, nullptr, &sampler), "vkCreateSampler");
  return sampler;
}

VkDescriptorSetLayout create_descriptor_set_layout(Gpu& gpu,
                                                   const std::vector<DescriptorBinding>& bindings) {
  std::vector<VkDescriptorSetLayoutBinding> b;
  b.reserve(bindings.size());
  for (const auto& d : bindings) {
    VkDescriptorSetLayoutBinding entry{};
    entry.binding = d.binding;
    entry.descriptorType = d.type;
    entry.descriptorCount = d.count;
    entry.stageFlags = d.stages;
    b.push_back(entry);
  }
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  ci.bindingCount = static_cast<uint32_t>(b.size());
  ci.pBindings = b.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  check(gpu.api.vkCreateDescriptorSetLayout(gpu.device, &ci, nullptr, &layout),
        "vkCreateDescriptorSetLayout");
  return layout;
}

VkPipeline create_pipeline(Gpu& gpu, const PipelineDesc& desc) {
  VkShaderModuleCreateInfo vs_ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  vs_ci.codeSize = desc.vert_size;
  vs_ci.pCode = reinterpret_cast<const uint32_t*>(desc.vert_spv);
  VkShaderModule vs = VK_NULL_HANDLE;
  check(gpu.api.vkCreateShaderModule(gpu.device, &vs_ci, nullptr, &vs), "vkCreateShaderModule");
  VkShaderModuleCreateInfo fs_ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  fs_ci.codeSize = desc.frag_size;
  fs_ci.pCode = reinterpret_cast<const uint32_t*>(desc.frag_spv);
  VkShaderModule fs = VK_NULL_HANDLE;
  check(gpu.api.vkCreateShaderModule(gpu.device, &fs_ci, nullptr, &fs), "vkCreateShaderModule");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";
  VkSpecializationInfo spec_info{};
  if (desc.spec_entries) {
    spec_info.mapEntryCount = desc.spec_entry_count;
    spec_info.pMapEntries = desc.spec_entries;
    spec_info.dataSize = desc.spec_data_size;
    spec_info.pData = desc.spec_data;
    stages[1].pSpecializationInfo = &spec_info;
  }

  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = static_cast<uint32_t>(desc.vertex_bindings.size());
  vi.pVertexBindingDescriptions = desc.vertex_bindings.data();
  vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.vertex_attributes.size());
  vi.pVertexAttributeDescriptions = desc.vertex_attributes.data();

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = desc.topology;
  ia.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = desc.cull;
  rs.frontFace = desc.front;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = desc.sample_count;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = desc.depth_op;

  ms.alphaToCoverageEnable = desc.alpha_to_coverage ? VK_TRUE : VK_FALSE;
  // One blend state per colour attachment; the multi-target passes (WBOIT) need
  // a different rule per target, so `attachments` carries them explicitly.
  std::vector<VkPipelineColorBlendAttachmentState> atts;
  if (!desc.attachments.empty()) {
    atts.resize(desc.attachments.size());
    for (size_t i = 0; i < desc.attachments.size(); ++i) {
      const AttachmentState& a = desc.attachments[i];
      VkPipelineColorBlendAttachmentState& o = atts[i];
      o = {};
      o.blendEnable = a.blend ? VK_TRUE : VK_FALSE;
      o.srcColorBlendFactor = a.src;
      o.dstColorBlendFactor = a.dst;
      o.srcAlphaBlendFactor = a.src_alpha;
      o.dstAlphaBlendFactor = a.dst_alpha;
      o.colorBlendOp = VK_BLEND_OP_ADD;
      o.alphaBlendOp = VK_BLEND_OP_ADD;
      o.colorWriteMask = a.write ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                                 : 0;
    }
  } else if (desc.color_attachments) {
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = desc.blend ? VK_TRUE : VK_FALSE;
    att.srcColorBlendFactor = desc.src_color;
    att.dstColorBlendFactor = desc.dst_color;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    atts.assign(desc.color_attachments, att);
  }

  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = uint32_t(atts.size());
  cb.pAttachments = atts.empty() ? nullptr : atts.data();

  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dyn_states;

  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.stageCount = 2;
  ci.pStages = stages;
  ci.pVertexInputState = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pDepthStencilState = &ds;
  ci.pColorBlendState = &cb;
  ci.pDynamicState = &dyn;
  ci.layout = desc.layout;
  ci.renderPass = desc.render_pass;
  ci.subpass = desc.subpass;

  VkPipeline pipeline = VK_NULL_HANDLE;
  check(gpu.api.vkCreateGraphicsPipelines(gpu.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
  gpu.api.vkDestroyShaderModule(gpu.device, vs, nullptr);
  gpu.api.vkDestroyShaderModule(gpu.device, fs, nullptr);
  return pipeline;
}

const EmbeddedShader* find_shader(const char* name) {
  for (const auto& s : kEmbeddedShaders)
    if (std::strcmp(s.name, name) == 0) return &s;
  return nullptr;
}

}  // namespace hgen
