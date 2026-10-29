#include "renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "png.hpp"

namespace hgen {
namespace {

// ---------------------------------------------------------------- UBO layout

struct FrameUbo {
  float mvp[16];
  float mv[16];
  float lightMVP[16];
  float lightView[16];
  float invView[16];
  float camPos[4];
  float lightDir[4];
  float resolution[4];
  float lightDirView[4];
  float hair0[4];
  float hair1[4];
  float hair2[4];
  float hair3[4];
  float hair4[4];
  float hair5[4];
  float fiber[4];
  float fiber2[4];
  float skyRef[4];
  float misc[4];
  float dye[4];
  float dye2[4];
};
static_assert(sizeof(FrameUbo) == (16 * 5 + 4 * 16) * 4, "unexpected UBO packing");

void copy_mat(float* dst, const mat4& m) { std::memcpy(dst, m.m, sizeof(float) * 16); }

// ------------------------------------------------------------ half float util

uint16_t float_to_half(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000;
  int32_t exp = int32_t((x >> 23) & 0xff) - 127 + 15;
  uint32_t mant = x & 0x7fffff;
  if (exp <= 0) return uint16_t(sign);            // underflow -> zero
  if (exp >= 31) return uint16_t(sign | 0x7c00);  // overflow -> inf
  return uint16_t(sign | uint32_t(exp << 10) | (mant >> 13));
}

bool skip_pass(const char* name) {
  const char* e = std::getenv("HGEN_SKIP");
  return e && std::string(e).find(name) != std::string::npos;
}

bool supports_linear_filter(Gpu& gpu, VkFormat f) {
  VkFormatProperties props{};
  gpu.api.vkGetPhysicalDeviceFormatProperties(gpu.physical, f, &props);
  return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

uint32_t find_sample_count(Gpu& gpu, VkFormat color, VkFormat depth, int wanted) {
  auto ok = [&](VkSampleCountFlagBits s) {
    VkImageFormatProperties p{};
    if (gpu.api.vkGetPhysicalDeviceImageFormatProperties(
            gpu.physical, color, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &p) != VK_SUCCESS)
      return false;
    if (!(p.sampleCounts & s)) return false;
    VkImageFormatProperties dp{};
    if (gpu.api.vkGetPhysicalDeviceImageFormatProperties(
            gpu.physical, depth, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &dp) != VK_SUCCESS)
      return false;
    return (dp.sampleCounts & s) != 0;
  };
  if (wanted >= 8 && ok(VK_SAMPLE_COUNT_8_BIT)) return 8;
  if (wanted >= 4 && ok(VK_SAMPLE_COUNT_4_BIT)) return 4;
  if (wanted >= 2 && ok(VK_SAMPLE_COUNT_2_BIT)) return 2;
  return 1;
}

mat4 light_matrix(vec3 sun_dir, float extent) {
  vec3 z = normalize(sun_dir);
  vec3 up = std::fabs(z.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
  vec3 x = normalize(cross(up, z));
  vec3 y = cross(z, x);
  mat4 m;
  std::memset(m.m, 0, sizeof(m.m));
  float s = 1.0f / extent;
  m.at(0, 0) = x.x * s; m.at(1, 0) = x.y * s; m.at(2, 0) = x.z * s;
  m.at(0, 1) = y.x * s; m.at(1, 1) = y.y * s; m.at(2, 1) = y.z * s;
  m.at(0, 2) = z.x * s; m.at(1, 2) = z.y * s; m.at(2, 2) = z.z * s;
  m.at(3, 3) = 1.0f;
  return m;
}

struct DescriptorWrite {
  VkDescriptorSet set;
  uint32_t binding;
  VkDescriptorType type;
  VkImageView view;
  VkSampler sampler;
  VkBuffer buffer;
  VkDeviceSize offset, range;
};

void write_descriptors(Gpu& gpu, const std::vector<DescriptorWrite>& writes) {
  std::vector<VkWriteDescriptorSet> ws;
  std::vector<VkDescriptorImageInfo> images;
  std::vector<VkDescriptorBufferInfo> buffers;
  images.reserve(writes.size());
  buffers.reserve(writes.size());
  ws.reserve(writes.size());
  for (const auto& w : writes) {
    VkWriteDescriptorSet wsx{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wsx.dstSet = w.set;
    wsx.dstBinding = w.binding;
    wsx.descriptorCount = 1;
    wsx.descriptorType = w.type;
    if (w.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || w.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      VkDescriptorBufferInfo bi{};
      bi.buffer = w.buffer;
      bi.offset = w.offset;
      bi.range = w.range;
      buffers.push_back(bi);
      wsx.pBufferInfo = &buffers.back();
    } else {
      VkDescriptorImageInfo ii{};
      ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      ii.imageView = w.view;
      ii.sampler = w.sampler;
      images.push_back(ii);
      wsx.pImageInfo = &images.back();
    }
    ws.push_back(wsx);
  }
  gpu.api.vkUpdateDescriptorSets(gpu.device, uint32_t(ws.size()), ws.data(), 0, nullptr);
}

VkRenderPass make_depth_pass(Gpu& gpu, VkFormat depth_format) {
  VkAttachmentDescription att{};
  att.format = depth_format;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.pDepthStencilAttachment = &ref;
  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = 1;
  ci.pAttachments = &att;
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = 2;
  ci.pDependencies = deps;
  VkRenderPass pass = VK_NULL_HANDLE;
  check(gpu.api.vkCreateRenderPass(gpu.device, &ci, nullptr, &pass), "vkCreateRenderPass(depth)");
  return pass;
}

VkRenderPass make_color_pass(Gpu& gpu, VkFormat color_format, VkImageLayout final_layout,
                             VkSampleCountFlagBits samples, VkFormat depth_format,
                             VkFormat resolve_format) {
  std::vector<VkAttachmentDescription> atts;
  VkAttachmentDescription color{};
  color.format = color_format;
  color.samples = samples;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = resolve_format == VK_FORMAT_UNDEFINED ? final_layout
                                                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  atts.push_back(color);

  VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth_ref{};
  VkAttachmentReference resolve_ref{};
  bool has_depth = depth_format != VK_FORMAT_UNDEFINED;
  bool has_resolve = resolve_format != VK_FORMAT_UNDEFINED;
  if (has_depth) {
    VkAttachmentDescription d{};
    d.format = depth_format;
    d.samples = samples;
    d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    d.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    d.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    d.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_ref.attachment = uint32_t(atts.size());
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    atts.push_back(d);
  }
  if (has_resolve) {
    VkAttachmentDescription r{};
    r.format = resolve_format;
    r.samples = VK_SAMPLE_COUNT_1_BIT;
    r.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    r.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    r.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    r.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    r.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    r.finalLayout = final_layout;
    resolve_ref.attachment = uint32_t(atts.size());
    resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    atts.push_back(r);
  }

  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &color_ref;
  sub.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;
  sub.pResolveAttachments = has_resolve ? &resolve_ref : nullptr;

  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_TRANSFER_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_SHADER_READ_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = uint32_t(atts.size());
  ci.pAttachments = atts.data();
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = 2;
  ci.pDependencies = deps;
  VkRenderPass pass = VK_NULL_HANDLE;
  check(gpu.api.vkCreateRenderPass(gpu.device, &ci, nullptr, &pass), "vkCreateRenderPass(color)");
  return pass;
}

VkFramebuffer make_framebuffer(Gpu& gpu, VkRenderPass pass, const std::vector<VkImageView>& views,
                               uint32_t w, uint32_t h) {
  VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  ci.renderPass = pass;
  ci.attachmentCount = uint32_t(views.size());
  ci.pAttachments = views.data();
  ci.width = w;
  ci.height = h;
  ci.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  check(gpu.api.vkCreateFramebuffer(gpu.device, &ci, nullptr, &fb), "vkCreateFramebuffer");
  return fb;
}

}  // namespace

// ---------------------------------------------------------------------- setup

Renderer::Renderer(Gpu& gpu, const Assets& assets, const Scene& scene, int width, int height,
                   int shadow_res, int samples)
    : gpu_(gpu), assets_(assets), scene_(scene), width_(width), height_(height), shadow_res_(shadow_res) {
  samples_ = static_cast<VkSampleCountFlagBits>(
      find_sample_count(gpu_, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT, samples));
  if (samples_ == VK_SAMPLE_COUNT_1_BIT && samples > 1)
    fprintf(stderr, "[hairgen] MSAA %dx unsupported here, falling back to 1 sample\n", samples);

  // ---- samplers
  samp_shadow_ = gpu_.create_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true);
  samp_hairdepth_ = gpu_.create_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
  if (const char* d = std::getenv("HGEN_DBG")) dbg_id_ = std::atoi(d);
  samp_linear_ = gpu_.create_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, true);
  samp_repeat_ = gpu_.create_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, false);
  samp_nearest_ = gpu_.create_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

  create_targets();

  // ---- textures from the asset bundle
  auto upload_float = [&](const char* name, bool is_3d, Image& out, bool mipmap = false) {
    const AssetChunk& c = assets_.get(name);
    size_t texels = c.texel_count();
    uint32_t comps = c.channels;
    // Pick a filterable format, falling back to 16-bit storage when needed.
    VkFormat f32 = VK_FORMAT_UNDEFINED, f16 = VK_FORMAT_UNDEFINED;
    switch (comps) {
      case 1: f32 = VK_FORMAT_R32_SFLOAT; f16 = VK_FORMAT_R16_SFLOAT; break;
      case 2: f32 = VK_FORMAT_R32G32_SFLOAT; f16 = VK_FORMAT_R16G16_SFLOAT; break;
      case 3: f32 = VK_FORMAT_R32G32B32_SFLOAT; f16 = VK_FORMAT_R16G16B16A16_SFLOAT; break;
      default: f32 = VK_FORMAT_R32G32B32A32_SFLOAT; f16 = VK_FORMAT_R16G16B16A16_SFLOAT; break;
    }
    bool use32 = supports_linear_filter(gpu_, f32);
    VkFormat format = use32 ? f32 : f16;
    uint32_t out_comps = use32 ? comps : (comps == 3 ? 4 : comps);

    std::vector<float> src(texels * comps);
    for (size_t i = 0; i < src.size(); ++i) src[i] = c.float_at(i);
    std::vector<uint8_t> bytes;
    if (use32) {
      bytes.resize(src.size() * 4);
      std::memcpy(bytes.data(), src.data(), bytes.size());
    } else {
      bytes.resize(texels * out_comps * 2);
      uint16_t* dst = reinterpret_cast<uint16_t*>(bytes.data());
      for (size_t t = 0; t < texels; ++t)
        for (uint32_t k = 0; k < out_comps; ++k)
          dst[t * out_comps + k] = float_to_half(k < comps ? src[t * comps + k] : 0.0f);
    }
    uint32_t mips = 1;
    if (mipmap && !is_3d) {
      for (uint32_t d = std::max(c.width, c.height); d > 1; d >>= 1) ++mips;
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              (mips > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    out = is_3d ? gpu_.create_image_3d(c.width, c.height, c.depth, format, usage)
                : gpu_.create_image_2d(c.width, c.height, format, usage,
                                       VK_SAMPLE_COUNT_1_BIT, mips);
    gpu_.upload_image(out, bytes.data(), bytes.size());
  };

  auto upload_u8 = [&](const char* name, bool is_3d, VkFormat format, uint32_t out_comps, Image& out,
                       bool mipmap = false) {
    const AssetChunk& c = assets_.get(name);
    size_t texels = c.texel_count();
    std::vector<uint8_t> bytes(texels * out_comps);
    for (size_t t = 0; t < texels; ++t)
      for (uint32_t k = 0; k < out_comps; ++k)
        bytes[t * out_comps + k] = k < c.channels ? c.data[t * c.channels + k]
                                                  : (out_comps == 4 ? 255 : 0);
    uint32_t mips = 1;
    if (mipmap && !is_3d) {
      for (uint32_t d = std::max(c.width, c.height); d > 1; d >>= 1) ++mips;
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              (mips > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    out = is_3d ? gpu_.create_image_3d(c.width, c.height, c.depth, format, usage)
                : gpu_.create_image_2d(c.width, c.height, format, usage,
                                       VK_SAMPLE_COUNT_1_BIT, mips);
    gpu_.upload_image(out, bytes.data(), bytes.size());
  };

  upload_float("hairmesh.pos", true, tex_hairmesh_);
  upload_float("hairmesh.uv", false, tex_uv_);
  upload_float("hairmesh.uw", true, tex_uw_);
  upload_float("hairmesh.v", true, tex_v_);
  upload_u8("noise3d", true, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_noise_);
  upload_u8("hair.clump", false, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_clump_);
  // The skin maps are minified hard at these camera distances, so they all get
  // mip chains (a 1024^2 pore-level normal map aliases into gravel otherwise).
  upload_u8("skin.albedo", false, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_skin_, true);
  upload_u8("skin.flat", false, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_skin_flat_, true);
  // Detail maps (optional in the bundle).
  if (assets_.has("skin.normal")) upload_u8("skin.normal", false, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_skin_nrm_, true);
  if (assets_.has("skin.ao")) upload_u8("skin.ao", false, VK_FORMAT_R8_UNORM, 1, tex_skin_ao_, true);
  if (assets_.has("skin.spec")) upload_u8("skin.spec", false, VK_FORMAT_R8_UNORM, 1, tex_skin_spec_, true);
  upload_u8("hair.clump", false, VK_FORMAT_R8G8B8A8_UNORM, 4, tex_clump_, true);

  // ---- hair materials: one LUT set each
  {
    const AssetChunk& names = assets_.get("mtl.names");
    std::string list(reinterpret_cast<const char*>(names.data.data()), names.data.size());
    size_t pos = 0;
    while (pos < list.size()) {
      const size_t end = list.find('\0', pos);
      if (end == std::string::npos || end == pos) break;
      MaterialSet m;
      m.name = list.substr(pos, end - pos);
      const std::string tag = "mtl." + m.name + ".";
      upload_u8((tag + "N_TT_R").c_str(), false, VK_FORMAT_R8G8B8A8_UNORM, 4, m.nttr);
      upload_u8((tag + "N_TRT").c_str(), false, VK_FORMAT_R8G8B8A8_UNORM, 4, m.ntrt);
      upload_u8((tag + "N_TT_R_GI").c_str(), false, VK_FORMAT_R8G8B8A8_UNORM, 4, m.nttr_gi);
      upload_u8((tag + "N_TRT_GI").c_str(), false, VK_FORMAT_R8G8B8A8_UNORM, 4, m.ntrt_gi);
      upload_u8((tag + "GI").c_str(), true, VK_FORMAT_R8G8B8A8_UNORM, 4, m.gi);
      upload_u8((tag + "M_GI").c_str(), false, VK_FORMAT_R8G8B8A8_UNORM, 4, m.mgi);
      materials_.push_back(std::move(m));
      pos = end + 1;
    }
    if (materials_.empty()) throw std::runtime_error("asset bundle has no materials");
  }

  // ---- head geometry
  {
    const AssetChunk& p = assets_.get("head.pos");
    const AssetChunk& n = assets_.get("head.nrm");
    const AssetChunk& uv = assets_.get("head.uv");
    const AssetChunk& idx = assets_.get("head.idx");
    size_t verts = p.texel_count();
    std::vector<float> interleaved(verts * 8);
    for (size_t i = 0; i < verts; ++i) {
      for (int k = 0; k < 3; ++k) {
        interleaved[i * 8 + k] = p.float_at(i * 3 + k);
        interleaved[i * 8 + 3 + k] = n.float_at(i * 3 + k);
      }
      interleaved[i * 8 + 6] = uv.float_at(i * 2 + 0);
      interleaved[i * 8 + 7] = uv.float_at(i * 2 + 1);
    }
    head_vb_ = gpu_.create_buffer(interleaved.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  interleaved.data());
    std::vector<uint32_t> indices(idx.texel_count());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint32_t(idx.float_at(i));
    head_index_count_ = uint32_t(indices.size());
    head_ib_ = gpu_.create_buffer(indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  indices.data());
  }

  // ---- pedestal under the neck (generated by the exporter)
  if (assets_.has("bust.pos")) {
    const AssetChunk& p = assets_.get("bust.pos");
    const AssetChunk& n = assets_.get("bust.nrm");
    const AssetChunk& idx = assets_.get("bust.idx");
    const size_t verts = p.texel_count();
    std::vector<float> interleaved(verts * 8);
    for (size_t i = 0; i < verts; ++i) {
      for (int k = 0; k < 3; ++k) {
        interleaved[i * 8 + k] = p.float_at(i * 3 + k);
        interleaved[i * 8 + 3 + k] = n.float_at(i * 3 + k);
      }
      interleaved[i * 8 + 6] = 0.5f;  // no texture on the pedestal
      interleaved[i * 8 + 7] = 0.5f;
    }
    bust_vb_ = gpu_.create_buffer(interleaved.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  interleaved.data());
    std::vector<uint32_t> indices(idx.texel_count());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint32_t(idx.float_at(i));
    bust_index_count_ = uint32_t(indices.size());
    bust_ib_ = gpu_.create_buffer(indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  indices.data());
  }

  // ---- per-bundle texel coordinates
  {
    const AssetChunk& b = assets_.get("hair.bundles");
    bundle_count_ = b.width;
    bundle_st_.resize(bundle_count_ * 2);
    for (uint32_t i = 0; i < bundle_count_ * 2; ++i) bundle_st_[i] = b.float_at(i);
  }

  // ---- explicit root samples (july_rootsamples.js)
  {
    const AssetChunk& st = assets_.get("roots.st");
    const AssetChunk& base = assets_.get("roots.base");
    const AssetChunk& count = assets_.get("roots.count");
    root_base_.resize(bundle_count_);
    root_count_.resize(bundle_count_);
    for (uint32_t b = 0; b < bundle_count_; ++b) {
      root_base_[b] = uint32_t(base.float_at(b));
      root_count_[b] = uint32_t(count.float_at(b));
    }
    const size_t n = st.width * 2;
    std::vector<float> stv(n);
    for (size_t i = 0; i < n; ++i) stv[i] = st.float_at(i);
    root_buf_ = gpu_.create_buffer(n * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   stv.data());
  }

  // ---- head colliders (july_colliders.js)
  {
    const AssetChunk& c = assets_.get("colliders");
    collider_count_ = c.width;
    std::vector<float> cv(size_t(collider_count_) * c.channels);
    for (size_t i = 0; i < cv.size(); ++i) cv[i] = c.float_at(i);
    collider_buf_ = gpu_.create_buffer(cv.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       cv.data());
  }

  // ---- uniform buffer + readback
  ubo_ = gpu_.create_buffer(sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  readback_ = gpu_.create_buffer(VkDeviceSize(width_) * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  create_hair_geometry();
  create_hairmesh_view();
  // Descriptor set layouts must exist before the pipeline layouts that use them.
  create_descriptors();
  create_pipelines();

  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  check(gpu_.api.vkCreateFence(gpu_.device, &fci, nullptr, &fence_), "vkCreateFence");

  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = gpu_.command_pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  check(gpu_.api.vkAllocateCommandBuffers(gpu_.device, &ai, &cmd_), "vkAllocateCommandBuffers");
}

Renderer::~Renderer() {
  if (gpu_.device == VK_NULL_HANDLE) return;
  gpu_.api.vkDeviceWaitIdle(gpu_.device);
  auto destroy_pipeline = [&](VkPipeline& p) {
    if (p) gpu_.api.vkDestroyPipeline(gpu_.device, p, nullptr);
    p = VK_NULL_HANDLE;
  };
  destroy_pipeline(p_sky_lut_);
  destroy_pipeline(p_background_);
  destroy_pipeline(p_ground_);
  destroy_pipeline(p_head_);
  destroy_pipeline(p_bust_);
  destroy_pipeline(p_hair_);
  destroy_pipeline(p_obj_shadow_);
  destroy_pipeline(p_hair_shadow_);
  destroy_pipeline(p_prefilter_);
  destroy_pipeline(p_blur_);
  destroy_pipeline(p_post_);
  auto destroy_fb = [&](VkFramebuffer& f) {
    if (f) gpu_.api.vkDestroyFramebuffer(gpu_.device, f, nullptr);
    f = VK_NULL_HANDLE;
  };
  destroy_fb(fb_obj_shadow_);
  destroy_fb(fb_hair_shadow_);
  destroy_fb(fb_sky_lut_);
  destroy_fb(fb_bloom1_);
  destroy_fb(fb_bloom2_);
  destroy_fb(fb_scene_);
  if (pass_depth_) gpu_.api.vkDestroyRenderPass(gpu_.device, pass_depth_, nullptr);
  for (auto* img : {&sky_lut_, &bloom1_, &bloom2_, &hdr_msaa_, &hdr_resolve_, &depth_msaa_, &final_color_,
                    &obj_shadow_, &hair_shadow_, &dummy_tex_, &dummy_depth_, &tex_hairmesh_, &tex_uv_, &tex_uw_, &tex_v_,
                    &tex_noise_, &tex_clump_, &tex_skin_, &tex_skin_flat_, &tex_skin_nrm_, &tex_skin_ao_,
                    &tex_skin_spec_})
    gpu_.destroy_image(*img);
  for (MaterialSet& m : materials_)
    for (auto* img : {&m.nttr, &m.ntrt, &m.nttr_gi, &m.ntrt_gi, &m.gi, &m.mgi}) gpu_.destroy_image(*img);
  for (auto* b : {&hair_vb_, &head_vb_, &head_ib_, &bust_vb_, &bust_ib_, &ubo_, &readback_, &root_buf_, &collider_buf_,
                  &hair_line_vb_, &hairmesh_curve_vb_, &hairmesh_rect_vb_})
    gpu_.destroy_buffer(*b);
  if (fence_) gpu_.api.vkDestroyFence(gpu_.device, fence_, nullptr);
  if (layout_scene_) gpu_.api.vkDestroyPipelineLayout(gpu_.device, layout_scene_, nullptr);
  if (layout_full_) gpu_.api.vkDestroyPipelineLayout(gpu_.device, layout_full_, nullptr);
  if (layout_shadow_) gpu_.api.vkDestroyPipelineLayout(gpu_.device, layout_shadow_, nullptr);
  if (desc_pool_) gpu_.api.vkDestroyDescriptorPool(gpu_.device, desc_pool_, nullptr);
  if (dsl_frame_) gpu_.api.vkDestroyDescriptorSetLayout(gpu_.device, dsl_frame_, nullptr);
  if (dsl_hair_) gpu_.api.vkDestroyDescriptorSetLayout(gpu_.device, dsl_hair_, nullptr);
  for (VkSampler* s : {&samp_shadow_, &samp_hairdepth_, &samp_linear_, &samp_repeat_, &samp_nearest_})
    if (*s) gpu_.api.vkDestroySampler(gpu_.device, *s, nullptr);
}

// -------------------------------------------------------------------- targets

void Renderer::create_targets() {
  const VkFormat hdr = VK_FORMAT_R16G16B16A16_SFLOAT;
  sky_lut_ = gpu_.create_image_2d(256, 128, hdr, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT);
  dummy_tex_ = gpu_.create_image_2d(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  {
    uint8_t px[4] = {0, 0, 0, 255};
    gpu_.upload_image(dummy_tex_, px, 4);
  }
  // 1x1 depth image used wherever a depth-map binding must not alias the map
  // that a pass is currently rendering into.
  dummy_depth_ = gpu_.create_image_2d(1, 1, VK_FORMAT_D32_SFLOAT,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT);
  const int bloom_w = (std::max)(1, width_ / 4), bloom_h = (std::max)(1, height_ / 4);
  bloom1_ = gpu_.create_image_2d(bloom_w, bloom_h, hdr,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  bloom2_ = gpu_.create_image_2d(bloom_w, bloom_h, hdr,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  hdr_msaa_ = gpu_.create_image_2d(width_, height_, hdr, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, samples_);
  hdr_resolve_ = gpu_.create_image_2d(width_, height_, hdr,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  depth_msaa_ = gpu_.create_image_2d(width_, height_, VK_FORMAT_D32_SFLOAT,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples_);
  final_color_ = gpu_.create_image_2d(width_, height_, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  hdr_resolve_.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  bloom1_.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  bloom2_.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  sky_lut_.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  obj_shadow_ = gpu_.create_image_2d(shadow_res_, shadow_res_, VK_FORMAT_D32_SFLOAT,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT);
  hair_shadow_ = gpu_.create_image_2d(shadow_res_, shadow_res_, VK_FORMAT_D32_SFLOAT,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT);
  obj_shadow_.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  hair_shadow_.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

  pass_depth_ = make_depth_pass(gpu_, VK_FORMAT_D32_SFLOAT);
  fb_obj_shadow_ = make_framebuffer(gpu_, pass_depth_, {obj_shadow_.view}, shadow_res_, shadow_res_);
  fb_hair_shadow_ = make_framebuffer(gpu_, pass_depth_, {hair_shadow_.view}, shadow_res_, shadow_res_);

  // One colour pass shared by every single-sample HDR stage.
  static VkRenderPass pass_color_hdr = VK_NULL_HANDLE;
  pass_color_hdr = make_color_pass(gpu_, hdr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED);
  fb_sky_lut_ = make_framebuffer(gpu_, pass_color_hdr, {sky_lut_.view}, 256, 128);
  fb_bloom1_ = make_framebuffer(gpu_, pass_color_hdr, {bloom1_.view}, bloom_w, bloom_h);
  fb_bloom2_ = make_framebuffer(gpu_, pass_color_hdr, {bloom2_.view}, bloom_w, bloom_h);
  sky_lut_pass_ = pass_color_hdr;

  scene_pass_ = make_color_pass(gpu_, hdr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, samples_,
                                VK_FORMAT_D32_SFLOAT, hdr);
  fb_scene_ = make_framebuffer(gpu_, scene_pass_, {hdr_msaa_.view, depth_msaa_.view, hdr_resolve_.view},
                               width_, height_);
  post_pass_ = make_color_pass(gpu_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED);
  fb_post_ = make_framebuffer(gpu_, post_pass_, {final_color_.view}, width_, height_);

  // Initial layout transitions for the sampled/attachment images.
  VkCommandBuffer cmd = gpu_.begin_once();
  auto barrier = [&](Image& img, VkImageAspectFlags aspect) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    gpu_.api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  };
  barrier(hdr_resolve_, VK_IMAGE_ASPECT_COLOR_BIT);
  barrier(bloom1_, VK_IMAGE_ASPECT_COLOR_BIT);
  barrier(bloom2_, VK_IMAGE_ASPECT_COLOR_BIT);
  barrier(sky_lut_, VK_IMAGE_ASPECT_COLOR_BIT);
  barrier(obj_shadow_, VK_IMAGE_ASPECT_DEPTH_BIT);
  barrier(hair_shadow_, VK_IMAGE_ASPECT_DEPTH_BIT);
  barrier(dummy_depth_, VK_IMAGE_ASPECT_DEPTH_BIT);
  gpu_.end_once(cmd);
}

// --------------------------------------------------------------- hair geometry

void Renderer::create_hair_geometry() {
  const auto& s = scene_.hair;
  hair_segments_ = uint32_t(std::max(4, s.segments));
  hair_strand_count_ = uint32_t(std::max(int(bundle_count_) * 4, s.hair_count));
  const float max_level = float(assets_.max_hair_levels);
  const uint32_t depth = 3 * assets_.max_hair_levels + 1;
  const float texWscale = max_level / (max_level + 1.0f);
  const float texWshift = 0.5f / (max_level + 1.0f);
  const float bias = 0.5f;
  const float width = s.width_px / float(width_);

  auto w2t = [&](float w, float& t0, float& t1, float& t2) {
    float wl = w * max_level;
    int segment = int(std::min(max_level - 1.0f, std::floor(wl)));
    if (segment < 0) segment = 0;
    float t = wl - float(segment);
    t1 = (3.0f * float(segment) + 0.5f + (1.0f - t) * t) / float(depth);
    t2 = (3.0f * float(segment + 1) + 0.5f - (1.0f - t) * t) / float(depth);
    t0 = t;
  };

  std::vector<HairVertex> verts(size_t(hair_segments_) * 2);
  for (uint32_t i = 0; i < hair_segments_; ++i) {
    float w = std::pow(float(i) / float(hair_segments_ - 1), bias);
    float t0, t1, t2;
    w2t(w, t0, t1, t2);
    float di = (w < 0.5f) ? 1.0f : -1.0f;
    float w2 = std::pow((float(i) + di) / float(hair_segments_ - 1), bias);
    w2 = clampf(w2, 0.0f, 1.0f);
    float u0, u1, u2;
    w2t(w2, u0, u1, u2);
    const float texW0 = w * texWscale + texWshift;
    const float texW1 = w2 * texWscale + texWshift;
    for (int side = 0; side < 2; ++side) {
      HairVertex& v = verts[size_t(i) * 2 + side];
      v.pos[0] = t1; v.pos[1] = t2; v.pos[2] = t0; v.pos[3] = w;
      v.pos2[0] = u1; v.pos2[1] = u2; v.pos2[2] = u0; v.pos2[3] = di / float(hair_segments_ - 1);
      v.texW[0] = texW0;
      v.texW[1] = texW1;
      v.texW[2] = side == 0 ? -width : width;
    }
  }
  hair_vert_count_ = uint32_t(verts.size());
  if (hair_vb_.buffer) gpu_.destroy_buffer(hair_vb_);
  hair_vb_ = gpu_.create_buffer(verts.size() * sizeof(HairVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                verts.data());

  // Line mode ("render mode 0" in the demo): one vertex per curve sample, so a
  // LINE_STRIP through them draws the strand centre line.
  std::vector<HairVertex> lines(static_cast<size_t>(hair_segments_));
  for (uint32_t i = 0; i < hair_segments_; ++i) lines[i] = verts[size_t(i) * 2];
  hair_line_vert_count_ = hair_segments_;
  if (hair_line_vb_.buffer) gpu_.destroy_buffer(hair_line_vb_);
  hair_line_vb_ = gpu_.create_buffer(lines.size() * sizeof(HairVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     lines.data());
}

// ------------------------------------------------------- hair-mesh cage view

// "show hair mesh" in the demo draws the control cage: the bundle curves read
// back out of the hair-mesh 3D texture, plus a rectangle per bundle cell. Both
// come straight from the exported hairmesh.pos chunk, so no extra data is
// needed.
void Renderer::create_hairmesh_view() {
  const AssetChunk& pos = assets_.get("hairmesh.pos");
  const uint32_t w = pos.width, h = pos.height, d = pos.depth;
  const uint32_t levels = assets_.max_hair_levels;  // control points 0..levels
  const uint32_t samples = levels + 1;

  // Which layout cells actually hold a bundle (the rest are padding in the
  // 7x6 layout grid).
  std::vector<uint8_t> occupied(size_t(w) * h, 0);
  for (uint32_t b = 0; b < bundle_count_; ++b) {
    const int x = int(bundle_st_[b * 2] - 0.5f);
    const int y = int(bundle_st_[b * 2 + 1] - 0.5f);
    if (x >= 0 && y >= 0 && x < int(w) && y < int(h)) occupied[size_t(y) * w + x] = 1;
  }
  auto at = [&](uint32_t x, uint32_t y) { return occupied[size_t(y) * w + x] != 0; };

  // Control-point positions of a bundle curve: the z = 3*level slices of the
  // hair-mesh 3D texture are the level vertices, the slices in between are
  // the extra cross-section samples used for tangent estimation.
  auto ctrl = [&](uint32_t x, uint32_t y, uint32_t level, float out[3]) {
    const size_t idx = ((size_t(std::min(3 * level, d - 1)) * h + y) * w + x) * 3;
    out[0] = pos.float_at(idx);
    out[1] = pos.float_at(idx + 1);
    out[2] = pos.float_at(idx + 2);
  };

  // Instanced LINE_STRIP cannot offset into a vertex buffer per instance, so
  // the curves are expanded into an explicit line list instead.
  std::vector<float> curves;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      if (!at(x, y)) continue;
      float prev[3], p[3];
      ctrl(x, y, 0, prev);
      for (uint32_t l = 1; l < samples; ++l) {
        ctrl(x, y, l, p);
        curves.insert(curves.end(), {prev[0], prev[1], prev[2], p[0], p[1], p[2]});
        prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
      }
    }
  hairmesh_curve_roots_ = 1;
  hairmesh_curve_verts_ = uint32_t(curves.size() / 3);
  if (hairmesh_curve_vb_.buffer) gpu_.destroy_buffer(hairmesh_curve_vb_);
  if (!curves.empty())
    hairmesh_curve_vb_ = gpu_.create_buffer(curves.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            curves.data());

  // Rectangle outlines around the fully occupied bundle cells of the layout.
  std::vector<float> rects;
  for (uint32_t y = 0; y + 1 < h; ++y)
    for (uint32_t x = 0; x + 1 < w; ++x) {
      if (!at(x, y) || !at(x + 1, y) || !at(x + 1, y + 1) || !at(x, y + 1)) continue;
      float q[4][3];
      ctrl(x, y, 0, q[0]);
      ctrl(x + 1, y, 0, q[1]);
      ctrl(x + 1, y + 1, 0, q[2]);
      ctrl(x, y + 1, 0, q[3]);
      for (int i = 0; i < 4; ++i) {
        const float* a0 = q[i];
        const float* b0 = q[(i + 1) % 4];
        rects.insert(rects.end(), {a0[0], a0[1], a0[2], b0[0], b0[1], b0[2]});
      }
    }
  hairmesh_rect_verts_ = uint32_t(rects.size() / 3);
  if (hairmesh_rect_vb_.buffer) gpu_.destroy_buffer(hairmesh_rect_vb_);
  if (!rects.empty())
    hairmesh_rect_vb_ = gpu_.create_buffer(rects.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           rects.data());
}

bool Renderer::rebuild_hair(int hair_count, int segments) {
  scene_.hair.hair_count = hair_count;
  scene_.hair.segments = segments;
  create_hair_geometry();
  return true;
}

// ---------------------------------------------------------------- pipelines

void Renderer::create_pipelines() {
  VkPushConstantRange pc_scene{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
  VkPushConstantRange pc_frag{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};

  auto make_layout = [&](const std::vector<VkDescriptorSetLayout>& sets,
                         const std::vector<VkPushConstantRange>& pcs) {
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = uint32_t(sets.size());
    ci.pSetLayouts = sets.data();
    ci.pushConstantRangeCount = uint32_t(pcs.size());
    ci.pPushConstantRanges = pcs.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    check(gpu_.api.vkCreatePipelineLayout(gpu_.device, &ci, nullptr, &layout), "vkCreatePipelineLayout");
    return layout;
  };

  layout_scene_ = make_layout({dsl_frame_, dsl_hair_}, {pc_scene});
  layout_full_ = make_layout({dsl_frame_}, {});
  layout_post_ = make_layout({dsl_frame_}, {pc_frag});
  layout_shadow_ = make_layout({}, {});

  auto shader = [&](const char* name) {
    const EmbeddedShader* s = find_shader(name);
    if (!s) throw std::runtime_error(std::string("missing embedded shader: ") + name);
    return s;
  };

  PipelineDesc d;
  d.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // sky table
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;  // samples tex_noise (set 1)
    p.render_pass = sky_lut_pass_;
    p.vert_spv = reinterpret_cast<const char*>(shader("fullscreen.vert")->data);
    p.vert_size = shader("fullscreen.vert")->size;
    const char* sky_name = skip_pass("simplesky") ? "sky_lut_simple.frag" : "sky_lut.frag";
    p.frag_spv = reinterpret_cast<const char*>(shader(sky_name)->data);
    p.frag_size = shader(sky_name)->size;
    p.depth_test = false;
    p.depth_write = false;
    p_sky_lut_ = create_pipeline(gpu_, p);
  }
  // background
  {
    PipelineDesc p = d;
    p.layout = layout_full_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("fullscreen.vert")->data);
    p.vert_size = shader("fullscreen.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("background.frag")->data);
    p.frag_size = shader("background.frag")->size;
    p.depth_test = false;
    p.depth_write = false;
    p_background_ = create_pipeline(gpu_, p);
  }
  // ground
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("fullscreen.vert")->data);
    p.vert_size = shader("fullscreen.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("ground.frag")->data);
    p.frag_size = shader("ground.frag")->size;
    p.depth_test = true;
    p.depth_write = true;
    p_ground_ = create_pipeline(gpu_, p);
  }
  // head
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("head.vert")->data);
    p.vert_size = shader("head.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("head.frag")->data);
    p.frag_size = shader("head.frag")->size;
    p.vertex_bindings = {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX}};
    p.vertex_attributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
    };
    // The bust is a torn triangle strip: parts of it face away from any given
    // viewpoint, so both sides must be shaded (the shader flips the normal on
    // back faces, as the web demo's fs_obj does).
    p.cull = VK_CULL_MODE_NONE;
    if (std::getenv("HGEN_NODEPTH")) { p.depth_test = false; p.depth_write = false; }
    p_head_ = create_pipeline(gpu_, p);
  }
  // pedestal (same vertex layout as the head, different material)
  if (bust_index_count_) {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("head.vert")->data);
    p.vert_size = shader("head.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("bust.frag")->data);
    p.frag_size = shader("bust.frag")->size;
    p.vertex_bindings = {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX}};
    p.vertex_attributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
    };
    p.cull = VK_CULL_MODE_NONE;
    p_bust_ = create_pipeline(gpu_, p);
  }
  // hair
  hair_bindings_ = {{0, sizeof(HairVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  hair_attrs_ = {
      {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
      {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
      {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 32},
  };
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("hair.vert")->data);
    p.vert_size = shader("hair.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("hair.frag")->data);
    p.frag_size = shader("hair.frag")->size;
    p.vertex_bindings = hair_bindings_;
    p.vertex_attributes = hair_attrs_;
    p.cull = VK_CULL_MODE_NONE;
    // The ribbon vertices are emitted two-per-segment, so they form a strip.
    p.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    p_hair_ = create_pipeline(gpu_, p);
  }
  // object shadow
  {
    PipelineDesc p = d;
    p.layout = layout_full_;  // the vertex shader reads the frame UBO (set 0)
    p.render_pass = pass_depth_;
    p.color_attachments = 0;
    p.vert_spv = reinterpret_cast<const char*>(shader("obj_shadow.vert")->data);
    p.vert_size = shader("obj_shadow.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("depth_only.frag")->data);
    p.frag_size = shader("depth_only.frag")->size;
    p.vertex_bindings = {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX}};
    p.vertex_attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
    p_obj_shadow_ = create_pipeline(gpu_, p);
  }
  // hair shadow (same vertex work, hard depth only)
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = pass_depth_;
    p.color_attachments = 0;
    p.vert_spv = reinterpret_cast<const char*>(shader("hair.vert")->data);
    p.vert_size = shader("hair.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("hair_shadow.frag")->data);
    p.frag_size = shader("hair_shadow.frag")->size;
    p.vertex_bindings = hair_bindings_;
    p.vertex_attributes = hair_attrs_;
    p.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    p_hair_shadow_ = create_pipeline(gpu_, p);
  }
  // hair, line mode (the demo's default render mode)
  {
    PipelineDesc p = d;
    p.layout = layout_scene_;
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("hair.vert")->data);
    p.vert_size = shader("hair.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("hair.frag")->data);
    p.frag_size = shader("hair.frag")->size;
    p.vertex_bindings = hair_bindings_;
    p.vertex_attributes = hair_attrs_;
    p.cull = VK_CULL_MODE_NONE;
    p.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    p_hair_lines_ = create_pipeline(gpu_, p);
  }
  // "show hair mesh": bundle curves and the layout-cell outlines
  {
    PipelineDesc p = d;
    p.layout = layout_full_;  // positions are already in world space
    p.render_pass = scene_pass_;
    p.sample_count = samples_;
    p.vert_spv = reinterpret_cast<const char*>(shader("hairmesh.vert")->data);
    p.vert_size = shader("hairmesh.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("hairmesh.frag")->data);
    p.frag_size = shader("hairmesh.frag")->size;
    p.vertex_bindings = {{0, 12, VK_VERTEX_INPUT_RATE_VERTEX}};
    p.vertex_attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
    p.cull = VK_CULL_MODE_NONE;
    // The cage hugs the scalp, so a depth test would hide it inside the head;
    // the demo draws it as a debug overlay on top of everything.
    p.depth_test = false;
    p.depth_write = false;
    p.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    p_hairmesh_curves_ = create_pipeline(gpu_, p);
    p_hairmesh_rects_ = create_pipeline(gpu_, p);
  }
  // bloom / post
  {
    PipelineDesc p = d;
    p.layout = layout_post_;
    p.render_pass = sky_lut_pass_;
    p.vert_spv = reinterpret_cast<const char*>(shader("fullscreen.vert")->data);
    p.vert_size = shader("fullscreen.vert")->size;
    p.frag_spv = reinterpret_cast<const char*>(shader("bloom_prefilter.frag")->data);
    p.frag_size = shader("bloom_prefilter.frag")->size;
    p.depth_test = false;
    p.depth_write = false;
    p_prefilter_ = create_pipeline(gpu_, p);

    p.frag_spv = reinterpret_cast<const char*>(shader("bloom_blur.frag")->data);
    p.frag_size = shader("bloom_blur.frag")->size;
    p_blur_ = create_pipeline(gpu_, p);

    PipelineDesc pp = d;
    pp.layout = layout_post_;
    pp.render_pass = post_pass_;
    pp.vert_spv = reinterpret_cast<const char*>(shader("fullscreen.vert")->data);
    pp.vert_size = shader("fullscreen.vert")->size;
    pp.frag_spv = reinterpret_cast<const char*>(shader("post.frag")->data);
    pp.frag_size = shader("post.frag")->size;
    pp.depth_test = false;
    pp.depth_write = false;
    p_post_ = create_pipeline(gpu_, pp);
  }
}

// -------------------------------------------------------------- descriptors

void Renderer::create_descriptors() {
  std::vector<DescriptorBinding> frame_bindings = {
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
      {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
  };
  dsl_frame_ = create_descriptor_set_layout(gpu_, frame_bindings);

  std::vector<DescriptorBinding> hair_bindings;
  for (uint32_t i = 0; i < 13; ++i)
    hair_bindings.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
  // 13: explicit root samples (july_rootsamples.js), 14: colliders
  // (july_colliders.js). Both feed ComputeHairVert() in hair.vert.
  hair_bindings.push_back({13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT});
  hair_bindings.push_back({14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT});
  dsl_hair_ = create_descriptor_set_layout(gpu_, hair_bindings);

  VkDescriptorPoolSize sizes[3]{};
  sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  sizes[0].descriptorCount = 4;
  sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  sizes[1].descriptorCount = uint32_t(4 * 10 + materials_.size() * 13);
  sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  sizes[2].descriptorCount = uint32_t(materials_.size() * 2);
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = uint32_t(5 + materials_.size());
  pci.poolSizeCount = 3;
  pci.pPoolSizes = sizes;
  check(gpu_.api.vkCreateDescriptorPool(gpu_.device, &pci, nullptr, &desc_pool_), "vkCreateDescriptorPool");

  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = desc_pool_;
  ai.descriptorSetCount = 4;
  std::vector<VkDescriptorSetLayout> frame_layouts(4, dsl_frame_);
  ai.pSetLayouts = frame_layouts.data();
  VkDescriptorSet sets[4]{};
  check(gpu_.api.vkAllocateDescriptorSets(gpu_.device, &ai, sets), "vkAllocateDescriptorSets");
  set_main_ = sets[0];
  set_sky_ = sets[1];
  set_blur_ = sets[2];
  set_shadow_ = sets[3];

  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &dsl_hair_;
  for (MaterialSet& m : materials_)
    check(gpu_.api.vkAllocateDescriptorSets(gpu_.device, &ai, &m.set), "vkAllocateDescriptorSets");

  auto frame_set = [&](VkDescriptorSet set, const Image& obj_shad, const Image& hair_shad,
                       const Image& sky, const Image& bloom) {
    write_descriptors(gpu_, {
        {set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_NULL_HANDLE, VK_NULL_HANDLE, ubo_.buffer, 0, sizeof(FrameUbo)},
        {set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, obj_shad.view, samp_shadow_, VK_NULL_HANDLE, 0, 0},
        {set, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hair_shad.view, samp_hairdepth_, VK_NULL_HANDLE, 0, 0},
        {set, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sky.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bloom.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hdr_resolve_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_skin_flat_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_skin_nrm_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_skin_ao_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {set, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_skin_spec_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
    });
  };
  frame_set(set_main_, obj_shadow_, hair_shadow_, sky_lut_, bloom1_);
  frame_set(set_sky_, obj_shadow_, hair_shadow_, dummy_tex_, bloom1_);
  frame_set(set_blur_, obj_shadow_, hair_shadow_, sky_lut_, bloom2_);
  frame_set(set_shadow_, dummy_depth_, dummy_depth_, sky_lut_, bloom1_);

  // Each material set is identical except for the six Marschner tables.
  for (MaterialSet& m : materials_) {
    write_descriptors(gpu_, {
        {m.set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_hairmesh_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_uv_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_uw_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_v_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_noise_.view, samp_repeat_, VK_NULL_HANDLE, 0, 0},
        {m.set, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_clump_.view, samp_nearest_, VK_NULL_HANDLE, 0, 0},
        {m.set, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.nttr.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.ntrt.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.nttr_gi.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.ntrt_gi.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.gi.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m.mgi.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, tex_skin_.view, samp_linear_, VK_NULL_HANDLE, 0, 0},
        {m.set, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_NULL_HANDLE, VK_NULL_HANDLE, root_buf_.buffer, 0,
         VK_WHOLE_SIZE},
        {m.set, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_NULL_HANDLE, VK_NULL_HANDLE, collider_buf_.buffer, 0,
         VK_WHOLE_SIZE},
    });
  }
}

// --------------------------------------------------------------------- frame

void Renderer::set_scene(const Scene& scene) {
  scene_ = scene;
  if (int(scene_.hair.segments) != int(hair_segments_) || scene_.hair.hair_count != int(hair_strand_count_ * 0 + scene_.hair.hair_count))
    create_hair_geometry();
}

void Renderer::update_frame_ubo(double time, uint32_t frame_index) {
  FrameUbo u{};
  const CameraSettings& cam = scene_.camera;
  float tilt = cam.tilt;
  vec3 dir(std::cos(tilt) * std::sin(cam.rot), -std::sin(tilt), std::cos(tilt) * std::cos(cam.rot));
  vec3 eye = cam.target + dir * cam.dist;
  mat4 view = mat4_look_at(eye, cam.target, vec3(0, 1, 0));
  float aspect = float(width_) / float(height_);
  mat4 proj = mat4_perspective(radians(cam.fov), aspect, 0.05f, 80.0f);
  proj.at(1, 1) = -proj.at(1, 1);  // Vulkan y-down clip space
  mat4 view_proj = proj * view;

  copy_mat(u.mvp, view_proj);
  copy_mat(u.mv, view);
  copy_mat(u.invView, mat4_invert(view_proj));

  float elev = -scene_.sky.sun_tilt;
  vec3 sun(std::cos(elev) * std::sin(scene_.sky.sun_rot), std::sin(elev),
           std::cos(elev) * std::cos(scene_.sky.sun_rot));
  sun = normalize(sun);
  mat4 lview = light_matrix(sun, 1.28f);
  copy_mat(u.lightMVP, lview);
  copy_mat(u.lightView, lview);

  u.camPos[0] = eye.x; u.camPos[1] = eye.y; u.camPos[2] = eye.z; u.camPos[3] = float(time);
  u.lightDir[0] = sun.x; u.lightDir[1] = sun.y; u.lightDir[2] = sun.z; u.lightDir[3] = 0;
  u.resolution[0] = float(width_); u.resolution[1] = float(height_);
  u.resolution[2] = 1.0f / float(width_); u.resolution[3] = 1.0f / float(height_);
  vec3 sun_view = (view * vec4(sun, 0.0f)).xyz();
  u.lightDirView[0] = sun_view.x; u.lightDirView[1] = sun_view.y; u.lightDirView[2] = sun_view.z;

  const HairSettings& h = scene_.hair;
  u.hair0[0] = h.density;    u.hair0[1] = h.length_var; u.hair0[2] = h.fuzz_amount; u.hair0[3] = h.fuzz_distrib;
  u.hair1[0] = h.fuzz_exp;   u.hair1[1] = h.frizz_amount; u.hair1[2] = h.frizz_freq; u.hair1[3] = h.frizz_exp;
  u.hair2[0] = h.kink_amount; u.hair2[1] = h.kink_freq_uv; u.hair2[2] = h.kink_freq_w; u.hair2[3] = h.kink_w_scale;
  u.hair3[0] = h.kink_exp;   u.hair3[1] = h.clump_percent; u.hair3[2] = h.clump_size; u.hair3[3] = h.clump_exp;
  u.hair4[0] = h.clump_var;  u.hair4[1] = h.clump_noise; u.hair4[2] = h.clump_freq; u.hair4[3] = h.curl_amount;
  u.hair5[0] = h.curl_freq;  u.hair5[1] = h.curl_twists; u.hair5[2] = h.curl_twist_exp; u.hair5[3] = h.curl_exp;

  u.fiber[0] = h.a_f[0]; u.fiber[1] = h.a_f[1]; u.fiber[2] = h.a_f[2]; u.fiber[3] = h.r_dev;
  u.fiber2[0] = h.tt_dev; u.fiber2[1] = h.trt_dev; u.fiber2[2] = h.density; u.fiber2[3] = h.ambient;

  u.skyRef[0] = scene_.sky.sun_intensity;
  u.skyRef[1] = scene_.sky.exposure;
  u.skyRef[2] = scene_.sky.bloom;
  u.skyRef[3] = scene_.sky.cloud_coverage;

  u.misc[0] = scene_.sky.ground_y;
  u.misc[1] = scene_.sky.show_ground ? 1.0f : 0.0f;
  u.misc[2] = float(dbg_id_);
  u.dye[0] = scene_.hair.dye[0]; u.dye[1] = scene_.hair.dye[1];
  u.dye[2] = scene_.hair.dye[2]; u.dye[3] = scene_.hair.dye_strength;
  u.dye2[0] = scene_.hair.root_darken; u.dye2[1] = scene_.hair.color_variation;
  u.dye2[2] = scene_.hair.sheen; u.dye2[3] = 0.0f;
  u.misc[3] = float(frame_index);

  std::memcpy(ubo_.mapped, &u, sizeof(u));
}

// -------------------------------------------------------------------- passes

// The demo's explicit root table holds ~1000 roots spread unevenly over the
// bundles; keep that distribution and scale it to the requested strand count.
// The descriptor set of the active hair material.
VkDescriptorSet Renderer::hair_set() const {
  if (materials_.empty()) return VK_NULL_HANDLE;
  const size_t i = std::min(materials_.size() - 1, size_t(std::max(0, scene_.hair.material)));
  return materials_[i].set;
}

uint32_t Renderer::strands_for_bundle(uint32_t bundle) const {
  if (!scene_.hair.use_root_samples || root_count_.empty() || bundle >= root_count_.size())
    return bundle_count_ ? (hair_strand_count_ / bundle_count_) : 0;
  const uint32_t total = std::max(1u, uint32_t(scene_.hair.hair_count));
  const uint32_t n = root_count_[bundle];
  return std::max(1u, uint32_t(std::lround(double(n) * double(total) / 1000.0)));
}

uint32_t Renderer::roots_per_bundle(uint32_t bundle) const {
  if (root_count_.empty() || bundle >= root_count_.size()) return 1;
  return std::max(1u, root_count_[bundle]);
}

uint32_t Renderer::root_base_for_bundle(uint32_t bundle) const {
  if (!scene_.hair.use_root_samples || root_base_.empty() || bundle >= root_base_.size()) return 0;
  return root_base_[bundle];
}

void Renderer::pass_shadows(VkCommandBuffer cmd) {
  VkClearValue clear{};
  clear.depthStencil = {1.0f, 0};

  auto begin = [&](VkFramebuffer fb, uint32_t size) {
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = pass_depth_;
    bi.framebuffer = fb;
    bi.renderArea = {{0, 0}, {size, size}};
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    gpu_.api.vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, float(size), float(size), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {size, size}};
    gpu_.api.vkCmdSetViewport(cmd, 0, 1, &vp);
    gpu_.api.vkCmdSetScissor(cmd, 0, 1, &sc);
  };
  auto end = [&]() { gpu_.api.vkCmdEndRenderPass(cmd); };

  // Object shadow map
  begin(fb_obj_shadow_, uint32_t(shadow_res_));
  if (scene_.hair.show_head && head_index_count_ && scene_.hair.head_casts_shadow) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_obj_shadow_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_full_, 0, 1, &set_shadow_, 0,
                                     nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &head_vb_.buffer, &offset);
    gpu_.api.vkCmdBindIndexBuffer(cmd, head_ib_.buffer, 0, VK_INDEX_TYPE_UINT32);
    gpu_.api.vkCmdDrawIndexed(cmd, head_index_count_, 1, 0, 0, 0);
  }
  if (bust_index_count_) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_obj_shadow_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_full_, 0, 1, &set_shadow_, 0,
                                     nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &bust_vb_.buffer, &offset);
    gpu_.api.vkCmdBindIndexBuffer(cmd, bust_ib_.buffer, 0, VK_INDEX_TYPE_UINT32);
    gpu_.api.vkCmdDrawIndexed(cmd, bust_index_count_, 1, 0, 0, 0);
  }
  end();

  // Hair shadow map
  begin(fb_hair_shadow_, uint32_t(shadow_res_));
  if (scene_.hair.show_hair && hair_vert_count_) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_hair_shadow_);
    VkDescriptorSet sets[2] = {set_shadow_, hair_set()};
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sets, 0, nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &hair_vb_.buffer, &offset);
    for (uint32_t b = 0; b < bundle_count_; ++b) {
      float pc[8] = {bundle_st_[b * 2],
                     bundle_st_[b * 2 + 1],
                     0.0f,
                     1.0f,
                     float(root_base_for_bundle(b)),
                     scene_.hair.use_root_samples ? 1.0f : 0.0f,
                     scene_.hair.collide ? 1.0f : 0.0f,
                     float(roots_per_bundle(b))};
      gpu_.api.vkCmdPushConstants(cmd, layout_scene_,
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc);
      gpu_.api.vkCmdDraw(cmd, hair_vert_count_, strands_for_bundle(b), 0, 0);
    }
  }
  end();
}

void Renderer::pass_sky_lut(VkCommandBuffer cmd) {
  VkClearValue clear{};
  clear.color = {{0, 0, 0, 1}};
  VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  bi.renderPass = sky_lut_pass_;
  bi.framebuffer = fb_sky_lut_;
  bi.renderArea = {{0, 0}, {256, 128}};
  bi.clearValueCount = 1;
  bi.pClearValues = &clear;
  gpu_.api.vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, 256.0f, 128.0f, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {256, 128}};
  gpu_.api.vkCmdSetViewport(cmd, 0, 1, &vp);
  gpu_.api.vkCmdSetScissor(cmd, 0, 1, &sc);
  gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_sky_lut_);
  VkDescriptorSet sky_sets[2] = {set_sky_, hair_set()};
  gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sky_sets, 0,
                                    nullptr);
  gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
  gpu_.api.vkCmdEndRenderPass(cmd);
}

void Renderer::pass_scene(VkCommandBuffer cmd) {
  VkClearValue clears[2]{};
  clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  bi.renderPass = scene_pass_;
  bi.framebuffer = fb_scene_;
  bi.renderArea = {{0, 0}, {uint32_t(width_), uint32_t(height_)}};
  bi.clearValueCount = 2;
  bi.pClearValues = clears;
  gpu_.api.vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, float(width_), float(height_), 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {uint32_t(width_), uint32_t(height_)}};
  gpu_.api.vkCmdSetViewport(cmd, 0, 1, &vp);
  gpu_.api.vkCmdSetScissor(cmd, 0, 1, &sc);

  // sky
  if (!skip_pass("background")) {
  gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_background_);
  gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_full_, 0, 1, &set_main_, 0, nullptr);
  gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  VkDescriptorSet sets[2] = {set_main_, hair_set()};

  // ground
  if (scene_.sky.show_ground && !skip_pass("grounddraw")) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_ground_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sets, 0, nullptr);
    gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  // pedestal
  if (bust_index_count_ && !skip_pass("bustdraw")) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_bust_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sets, 0, nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &bust_vb_.buffer, &offset);
    gpu_.api.vkCmdBindIndexBuffer(cmd, bust_ib_.buffer, 0, VK_INDEX_TYPE_UINT32);
    gpu_.api.vkCmdDrawIndexed(cmd, bust_index_count_, 1, 0, 0, 0);
  }

  // head
  if (scene_.hair.show_head && head_index_count_ && !skip_pass("headdraw")) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_head_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sets, 0, nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &head_vb_.buffer, &offset);
    gpu_.api.vkCmdBindIndexBuffer(cmd, head_ib_.buffer, 0, VK_INDEX_TYPE_UINT32);
    gpu_.api.vkCmdDrawIndexed(cmd, head_index_count_, 1, 0, 0, 0);
  }

  // hair
  if (scene_.hair.show_hair && hair_vert_count_ && !skip_pass("hairdraw")) {
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_hair_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_scene_, 0, 2, sets, 0, nullptr);
    VkDeviceSize offset = 0;
    const bool lines = scene_.hair.render_mode == 0;
    if (lines) {
      gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_hair_lines_);
      gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &hair_line_vb_.buffer, &offset);
    } else {
      gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &hair_vb_.buffer, &offset);
    }
    const uint32_t verts = lines ? hair_line_vert_count_ : hair_vert_count_;
    float width = scene_.hair.width_px / float(width_);
    for (uint32_t b = 0; b < bundle_count_; ++b) {
      float pc[8] = {bundle_st_[b * 2],
                     bundle_st_[b * 2 + 1],
                     width,
                     0.0f,
                     float(root_base_for_bundle(b)),
                     scene_.hair.use_root_samples ? 1.0f : 0.0f,
                     scene_.hair.collide ? 1.0f : 0.0f,
                     float(roots_per_bundle(b))};
      gpu_.api.vkCmdPushConstants(cmd, layout_scene_,
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc);
      gpu_.api.vkCmdDraw(cmd, verts, strands_for_bundle(b), 0, 0);
    }
  }

  // hair-mesh cage ("show hair mesh")
  if (scene_.hair.show_hairmesh && hairmesh_curve_roots_) {
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_full_, 0, 1, &set_main_, 0,
                                     nullptr);
    VkDeviceSize offset = 0;
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_hairmesh_curves_);
    gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &hairmesh_curve_vb_.buffer, &offset);
    gpu_.api.vkCmdDraw(cmd, hairmesh_curve_verts_, hairmesh_curve_roots_, 0, 0);
    if (hairmesh_rect_verts_) {
      gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_hairmesh_rects_);
      gpu_.api.vkCmdBindVertexBuffers(cmd, 0, 1, &hairmesh_rect_vb_.buffer, &offset);
      gpu_.api.vkCmdDraw(cmd, hairmesh_rect_verts_, 1, 0, 0);
    }
  }
  gpu_.api.vkCmdEndRenderPass(cmd);
}

void Renderer::pass_bloom(VkCommandBuffer cmd) {
  if (!scene_.sky.bloom) return;
  const int bloom_w = (std::max)(1, width_ / 4), bloom_h = (std::max)(1, height_ / 4);
  VkClearValue clear{};
  clear.color = {{0, 0, 0, 1}};
  auto begin = [&](VkFramebuffer fb) {
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = sky_lut_pass_;
    bi.framebuffer = fb;
    bi.renderArea = {{0, 0}, {uint32_t(bloom_w), uint32_t(bloom_h)}};
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    gpu_.api.vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, float(bloom_w), float(bloom_h), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {uint32_t(bloom_w), uint32_t(bloom_h)}};
    gpu_.api.vkCmdSetViewport(cmd, 0, 1, &vp);
    gpu_.api.vkCmdSetScissor(cmd, 0, 1, &sc);
  };

  // bright pass into bloom1 (bloom2 is bound but unused there)
  begin(fb_bloom1_);
  gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_prefilter_);
  gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_post_, 0, 1, &set_blur_, 0, nullptr);
  gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
  gpu_.api.vkCmdEndRenderPass(cmd);

  auto blur = [&](VkFramebuffer fb, VkDescriptorSet set, float dx, float dy) {
    begin(fb);
    gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_blur_);
    gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_post_, 0, 1, &set, 0, nullptr);
    float pc[4] = {dx / float(bloom_w), dy / float(bloom_h), 1.35f, 0.0f};
    gpu_.api.vkCmdPushConstants(cmd, layout_post_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
    gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
    gpu_.api.vkCmdEndRenderPass(cmd);
  };
  blur(fb_bloom2_, set_main_, 1.0f, 0.0f);   // bloom1 -> bloom2 (horizontal)
  blur(fb_bloom1_, set_blur_, 0.0f, 1.0f);   // bloom2 -> bloom1 (vertical)
}

void Renderer::pass_post(VkCommandBuffer cmd) {
  VkClearValue clear{};
  clear.color = {{0, 0, 0, 1}};
  VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  bi.renderPass = post_pass_;
  bi.framebuffer = fb_post_;
  bi.renderArea = {{0, 0}, {uint32_t(width_), uint32_t(height_)}};
  bi.clearValueCount = 1;
  bi.pClearValues = &clear;
  gpu_.api.vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, float(width_), float(height_), 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {uint32_t(width_), uint32_t(height_)}};
  gpu_.api.vkCmdSetViewport(cmd, 0, 1, &vp);
  gpu_.api.vkCmdSetScissor(cmd, 0, 1, &sc);
  gpu_.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_post_);
  gpu_.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_post_, 0, 1, &set_main_, 0, nullptr);
  float pc[4] = {scene_.sky.exposure, scene_.sky.bloom ? 0.9f : 0.0f, scene_.sky.vignette, 0.0f};
  gpu_.api.vkCmdPushConstants(cmd, layout_post_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
  gpu_.api.vkCmdDraw(cmd, 3, 1, 0, 0);
  gpu_.api.vkCmdEndRenderPass(cmd);

  // Copy the result to host visible memory.
  if (skip_pass("copy")) return;
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {uint32_t(width_), uint32_t(height_), 1};
  gpu_.api.vkCmdCopyImageToBuffer(cmd, final_color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  readback_.buffer, 1, &region);
}

RenderStats Renderer::render(double time_seconds, uint32_t frame_index) {
  auto t0 = std::chrono::steady_clock::now();
  update_frame_ubo(time_seconds, frame_index);

  gpu_.api.vkResetCommandBuffer(cmd_, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(gpu_.api.vkBeginCommandBuffer(cmd_, &bi), "vkBeginCommandBuffer");

  if (!skip_pass("shadowpass")) pass_shadows(cmd_);
  if (!skip_pass("skypass")) pass_sky_lut(cmd_);
  if (!skip_pass("scenepass")) pass_scene(cmd_);
  if (!skip_pass("bloompass")) pass_bloom(cmd_);
  if (!skip_pass("postpass")) pass_post(cmd_);

  check(gpu_.api.vkEndCommandBuffer(cmd_), "vkEndCommandBuffer");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd_;
  gpu_.api.vkResetFences(gpu_.device, 1, &fence_);
  check(gpu_.api.vkQueueSubmit(gpu_.queue, 1, &si, fence_), "vkQueueSubmit");
  check(gpu_.api.vkWaitForFences(gpu_.device, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");

  auto t1 = std::chrono::steady_clock::now();
  RenderStats stats;
  stats.cpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  stats.gpu_ms = stats.cpu_ms;
  stats.strands = hair_strand_count_;
  stats.triangles = hair_strand_count_ * (hair_segments_ - 1) * 2 + head_index_count_ / 3;
  return stats;
}

std::vector<uint8_t> Renderer::read_pixels() {
  std::vector<uint8_t> out(size_t(width_) * height_ * 4);
  std::memcpy(out.data(), readback_.mapped, out.size());
  return out;
}

void Renderer::write_png_internal(const std::string& path) { write_png_rgba(path, width_, height_, read_pixels()); }


}  // namespace hgen
