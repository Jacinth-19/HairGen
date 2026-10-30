// vk_util.hpp - device setup plus small RAII-ish resource helpers.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "vk_loader.hpp"

namespace hgen {

void check(VkResult r, const char* what);

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0, depth = 1, layers = 1, mips = 1;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Gpu {
  VkApi api;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceMemoryProperties mem_props{};
  VkPhysicalDeviceFeatures features{};
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  uint32_t api_version = VK_API_VERSION_1_0;
  bool validation = false;

  void init(const std::string& icd_override, bool enable_validation, bool prefer_cpu);
  void shutdown();
  bool is_cpu() const { return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU; }

  // memory helpers
  uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const;

  // command helpers
  VkCommandBuffer begin_once();
  void end_once(VkCommandBuffer cmd);
  void wait_idle();

  // resources
  Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags,
                       const void* data = nullptr);
  void destroy_buffer(Buffer& b);
  // `mip_levels > 1` allocates a chain that upload_image() fills by blitting.
  Image create_image_2d(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                        uint32_t mip_levels = 1);
  Image create_image_3d(uint32_t w, uint32_t h, uint32_t d, VkFormat format, VkImageUsageFlags usage);
  void destroy_image(Image& img);
  // Uploads texel data (tightly packed) into a freshly created image and leaves it in
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
  void upload_image(Image& img, const void* data, VkDeviceSize bytes);
  VkSampler create_sampler(VkFilter filter, VkSamplerAddressMode wrap, bool compare = false,
                           bool mip_linear = false);
};

struct DescriptorBinding {
  uint32_t binding;
  VkDescriptorType type;
  uint32_t count = 1;
  VkShaderStageFlags stages;
};

VkDescriptorSetLayout create_descriptor_set_layout(Gpu& gpu,
                                                   const std::vector<DescriptorBinding>& bindings);

// Per-attachment state for the multi-target passes (the scene pass has three
// colour attachments: the HDR scene, the OIT accumulation and the OIT
// revealage). `attachments` overrides the single-target blend fields above when
// it is not empty; its size is the pipeline's colour attachment count.
struct AttachmentState {
  bool write = true;
  bool blend = false;
  VkBlendFactor src = VK_BLEND_FACTOR_SRC_ALPHA;
  VkBlendFactor dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  VkBlendFactor src_alpha = VK_BLEND_FACTOR_ONE;
  VkBlendFactor dst_alpha = VK_BLEND_FACTOR_ZERO;
};

struct PipelineDesc {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  uint32_t subpass = 0;
  const char* vert_spv = nullptr;   // bytes
  size_t vert_size = 0;
  const char* frag_spv = nullptr;
  size_t frag_size = 0;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  bool depth_test = true;
  bool depth_write = true;
  VkCompareOp depth_op = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;
  uint32_t color_attachments = 1;  // 0 for depth-only passes
  bool blend = false;
  bool alpha_to_coverage = false;
  VkBlendFactor src_color = VK_BLEND_FACTOR_SRC_ALPHA;
  VkBlendFactor dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  std::vector<AttachmentState> attachments;   // empty -> one attachment above
  // Fragment-stage specialisation constants (e.g. hair.frag's kAccumulate, which
  // turns the strand colour into a WBOIT contribution without a second shader).
  const VkSpecializationMapEntry* spec_entries = nullptr;
  uint32_t spec_entry_count = 0;
  const void* spec_data = nullptr;
  size_t spec_data_size = 0;
  std::vector<VkVertexInputBindingDescription> vertex_bindings;
  std::vector<VkVertexInputAttributeDescription> vertex_attributes;
};

VkPipeline create_pipeline(Gpu& gpu, const PipelineDesc& desc);

// Shader modules are embedded at build time (see tools/compile_shaders.py which
// emits src/shaders_generated.hpp with `constexpr unsigned char` arrays).
struct EmbeddedShader {
  const char* name;
  const unsigned char* data;
  size_t size;
};
const EmbeddedShader* find_shader(const char* name);

}  // namespace hgen
