// renderer.hpp - the Vulkan hair renderer: shadow maps, sky, head, hair, ground
// and the post-processing chain.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets.hpp"
#include "scene.hpp"
#include "vk_util.hpp"

namespace hgen {

struct RenderStats {
  double cpu_ms = 0.0;
  double gpu_ms = 0.0;
  uint32_t triangles = 0;
  uint32_t strands = 0;
};

class Renderer {
 public:
  Renderer(Gpu& gpu, const Assets& assets, const Scene& scene, int width, int height,
           int shadow_res, int samples);
  ~Renderer();

  void set_scene(const Scene& scene);
  const Scene& scene() const { return scene_; }

  // Renders one frame into the internal colour target.
  RenderStats render(double time_seconds, uint32_t frame_index);

  // Copies the rendered frame back and returns tightly packed RGBA8 rows.
  std::vector<uint8_t> read_pixels();

  int width() const { return width_; }
  int height() const { return height_; }
  int samples() const { return static_cast<int>(samples_); }
  bool rebuild_hair(int hair_count, int segments);

 private:
  struct HairVertex {
    float pos[4];
    float pos2[4];
    float texW[3];
  };

  void create_targets();
  void create_hair_geometry();
  void create_hairmesh_view();
  void create_pipelines();
  void create_descriptors();
  void update_frame_ubo(double time, uint32_t frame_index);
  void write_png_internal(const std::string& path);

  uint32_t strands_for_bundle(uint32_t bundle) const;
  uint32_t root_base_for_bundle(uint32_t bundle) const;
  uint32_t roots_per_bundle(uint32_t bundle) const;

  void pass_shadows(VkCommandBuffer cmd);
  void pass_sky_lut(VkCommandBuffer cmd);
  void pass_scene(VkCommandBuffer cmd);
  void pass_bloom(VkCommandBuffer cmd);
  void pass_post(VkCommandBuffer cmd);

  Gpu& gpu_;
  const Assets& assets_;
  Scene scene_;
  int width_, height_;
  int shadow_res_;
  VkSampleCountFlagBits samples_;

  // textures
  Image sky_lut_, bloom1_, bloom2_, hdr_msaa_, hdr_resolve_, depth_msaa_, final_color_;
  Image obj_shadow_, hair_shadow_, dummy_tex_, dummy_depth_;
  Image tex_hairmesh_, tex_uv_, tex_uw_, tex_v_, tex_noise_, tex_clump_;
  int dbg_id_ = 0;
  Image tex_skin_, tex_skin_flat_, tex_skin_nrm_, tex_skin_ao_, tex_skin_spec_;

  // One set of Marschner lookup tables per hair material (std_blond, rosa, ...).
  struct MaterialSet {
    std::string name;
    Image nttr, ntrt, nttr_gi, ntrt_gi, gi, mgi;
    VkDescriptorSet set = VK_NULL_HANDLE;
  };
  std::vector<MaterialSet> materials_;
  VkSampler samp_shadow_ = VK_NULL_HANDLE;   // depth compare (object)
  VkSampler samp_hairdepth_ = VK_NULL_HANDLE;  // raw nearest depth (hair)
  VkSampler samp_linear_ = VK_NULL_HANDLE;
  VkSampler samp_repeat_ = VK_NULL_HANDLE;
  VkSampler samp_nearest_ = VK_NULL_HANDLE;

  // passes / framebuffers
  VkRenderPass pass_depth_ = VK_NULL_HANDLE;
  VkRenderPass sky_lut_pass_ = VK_NULL_HANDLE;   // single sample HDR colour pass
  VkRenderPass scene_pass_ = VK_NULL_HANDLE;
  VkRenderPass post_pass_ = VK_NULL_HANDLE;
  VkFramebuffer fb_obj_shadow_ = VK_NULL_HANDLE, fb_hair_shadow_ = VK_NULL_HANDLE;
  VkFramebuffer fb_sky_lut_ = VK_NULL_HANDLE, fb_bloom1_ = VK_NULL_HANDLE, fb_bloom2_ = VK_NULL_HANDLE;
  VkFramebuffer fb_scene_ = VK_NULL_HANDLE, fb_post_ = VK_NULL_HANDLE;

  // layouts
  VkDescriptorSetLayout dsl_frame_ = VK_NULL_HANDLE, dsl_hair_ = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_main_ = VK_NULL_HANDLE, set_sky_ = VK_NULL_HANDLE, set_blur_ = VK_NULL_HANDLE,
                  set_shadow_ = VK_NULL_HANDLE;
  VkDescriptorSet hair_set() const;
  VkPipelineLayout layout_scene_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_full_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_post_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_shadow_ = VK_NULL_HANDLE;

  // pipelines
  VkPipeline p_sky_lut_ = VK_NULL_HANDLE, p_background_ = VK_NULL_HANDLE, p_ground_ = VK_NULL_HANDLE;
  VkPipeline p_head_ = VK_NULL_HANDLE, p_bust_ = VK_NULL_HANDLE, p_hair_ = VK_NULL_HANDLE,
             p_hair_lines_ = VK_NULL_HANDLE;
  VkPipeline p_hairmesh_curves_ = VK_NULL_HANDLE, p_hairmesh_rects_ = VK_NULL_HANDLE;
  VkPipeline p_obj_shadow_ = VK_NULL_HANDLE, p_hair_shadow_ = VK_NULL_HANDLE;
  VkPipeline p_prefilter_ = VK_NULL_HANDLE, p_blur_ = VK_NULL_HANDLE, p_post_ = VK_NULL_HANDLE;
  VkPipeline p_rain_ = VK_NULL_HANDLE;   // instanced procedural rain streaks

  // geometry
  Buffer ubo_, hair_vb_, head_vb_, head_ib_, bust_vb_, bust_ib_, readback_;
  Buffer hair_line_vb_;        // one vertex per segment: strand centre lines
  Buffer root_buf_;            // explicit root samples (s,t) per bundle
  Buffer collider_buf_;        // july_colliders.js spheres, 2 per entry
  Buffer hairmesh_curve_vb_;   // hair-mesh cage curves (world space positions)
  Buffer hairmesh_rect_vb_;    // bundle quads on the cage, as line pairs
  uint32_t hair_line_vert_count_ = 0;
  uint32_t collider_count_ = 0;
  std::vector<uint32_t> root_base_, root_count_;  // per bundle root ranges
  uint32_t hairmesh_curve_roots_ = 0;   // number of cage curves
  uint32_t hairmesh_curve_verts_ = 0;   // samples per curve
  uint32_t hairmesh_rect_verts_ = 0;
  std::vector<VkVertexInputBindingDescription> hair_bindings_;
  std::vector<VkVertexInputAttributeDescription> hair_attrs_;
  uint32_t head_index_count_ = 0;
  uint32_t bust_index_count_ = 0;
  uint32_t hair_strand_count_ = 0;
  uint32_t hair_segments_ = 0;
  uint32_t hair_vert_count_ = 0;
  uint32_t bundle_count_ = 0;
  // The settings the current strand geometry was built from (see set_scene).
  int built_hair_count_ = -1;
  int built_segments_ = -1;
  std::vector<float> bundle_st_;  // xy per bundle

  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  double last_gpu_ms_ = 0.0;
};

}  // namespace hgen
