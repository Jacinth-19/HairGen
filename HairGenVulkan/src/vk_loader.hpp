// vk_loader.hpp - minimal, loader-independent Vulkan entry point resolution.
//
// The app can run on top of
//   * a system Vulkan loader (libvulkan.so.1) with whatever ICDs are installed
//     (real GPUs, Mesa lavapipe, ...), or
//   * a bare ICD in shared library form (e.g. SwiftShader's libvk_swiftshader.so
//     on machines without any Vulkan setup).
//
// Nothing about the renderer depends on which of the two is used.
#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

#include <string>
#include <vector>

namespace hgen {

#define HGEN_VK_FUNCTIONS(X)                                            \
  X(vkEnumerateInstanceVersion)                                         \
  X(vkCreateInstance)                                                   \
  X(vkEnumerateInstanceExtensionProperties)                             \
  X(vkEnumeratePhysicalDevices)                                         \
  X(vkGetPhysicalDeviceProperties)                                      \
  X(vkGetPhysicalDeviceFeatures)                                        \
  X(vkGetPhysicalDeviceMemoryProperties)                                \
  X(vkGetPhysicalDeviceQueueFamilyProperties)                           \
  X(vkGetPhysicalDeviceFormatProperties)                                \
  X(vkCreateDevice)                                                     \
  X(vkGetDeviceProcAddr)                                                \
  X(vkDestroyInstance)                                                  \
  X(vkDestroyDevice)                                                    \
  X(vkGetDeviceQueue)                                                   \
  X(vkCreateBuffer)                                                     \
  X(vkDestroyBuffer)                                                    \
  X(vkGetBufferMemoryRequirements)                                      \
  X(vkAllocateMemory)                                                   \
  X(vkFreeMemory)                                                       \
  X(vkBindBufferMemory)                                                 \
  X(vkMapMemory)                                                        \
  X(vkUnmapMemory)                                                      \
  X(vkCreateImage)                                                      \
  X(vkDestroyImage)                                                     \
  X(vkGetImageMemoryRequirements)                                       \
  X(vkBindImageMemory)                                                  \
  X(vkCreateImageView)                                                  \
  X(vkDestroyImageView)                                                 \
  X(vkCreateSampler)                                                    \
  X(vkDestroySampler)                                                   \
  X(vkCreateShaderModule)                                               \
  X(vkDestroyShaderModule)                                              \
  X(vkCreateDescriptorSetLayout)                                        \
  X(vkDestroyDescriptorSetLayout)                                       \
  X(vkCreateDescriptorPool)                                             \
  X(vkDestroyDescriptorPool)                                            \
  X(vkAllocateDescriptorSets)                                           \
  X(vkUpdateDescriptorSets)                                             \
  X(vkCreatePipelineLayout)                                             \
  X(vkDestroyPipelineLayout)                                            \
  X(vkCreateGraphicsPipelines)                                          \
  X(vkDestroyPipeline)                                                  \
  X(vkCreateRenderPass)                                                 \
  X(vkDestroyRenderPass)                                                \
  X(vkCreateFramebuffer)                                                \
  X(vkDestroyFramebuffer)                                               \
  X(vkCreateCommandPool)                                                \
  X(vkDestroyCommandPool)                                               \
  X(vkAllocateCommandBuffers)                                           \
  X(vkFreeCommandBuffers)                                               \
  X(vkBeginCommandBuffer)                                               \
  X(vkEndCommandBuffer)                                                 \
  X(vkResetCommandBuffer)                                               \
  X(vkQueueSubmit)                                                      \
  X(vkQueueWaitIdle)                                                    \
  X(vkDeviceWaitIdle)                                                   \
  X(vkCmdBeginRenderPass)                                               \
  X(vkCmdEndRenderPass)                                                 \
  X(vkCmdBindPipeline)                                                  \
  X(vkCmdBindDescriptorSets)                                            \
  X(vkCmdBindVertexBuffers)                                             \
  X(vkCmdBindIndexBuffer)                                               \
  X(vkCmdDraw)                                                          \
  X(vkCmdDrawIndexed)                                                   \
  X(vkCmdPushConstants)                                                 \
  X(vkCmdSetViewport)                                                   \
  X(vkCmdSetScissor)                                                    \
  X(vkCmdCopyImageToBuffer)                                             \
  X(vkCmdCopyBufferToImage)                                             \
  X(vkCmdBlitImage)                                                     \
  X(vkCmdPipelineBarrier)                                               \
  X(vkCreateFence)                                                      \
  X(vkDestroyFence)                                                     \
  X(vkWaitForFences)                                                    \
  X(vkResetFences)                                                      \
  X(vkGetPhysicalDeviceImageFormatProperties)

struct VkApi {
#define HGEN_DECL(name) PFN_##name name = nullptr;
  HGEN_VK_FUNCTIONS(HGEN_DECL)
#undef HGEN_DECL
  // Loader/ICD handles kept alive for the lifetime of the process.
  void* library = nullptr;
  void* instance_gpa = nullptr;  // vkGetInstanceProcAddr of the provider
  std::string provider;
};

// Resolve all entry points. Throws std::runtime_error on failure.
// `icd_override` may name a bare ICD shared library (e.g. SwiftShader);
// otherwise the system loader is preferred, then $VK_ICD_FILENAMES,
// $VK_DRIVER_FILES, and the usual libvk_swiftshader.so locations.
void load_vulkan(VkApi& api, const std::string& icd_override = std::string());

// Human readable list of the adapters visible to the loader (for --list-devices).
std::vector<std::string> list_devices(const VkApi& api);

}  // namespace hgen
