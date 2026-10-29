#include "vk_loader.hpp"

#include <dirent.h>
#include <dlfcn.h>

#include <fstream>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace hgen {
namespace {

void* dlopen_quiet(const std::string& path) {
  if (path.empty()) return nullptr;
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

// Resolve an ICD manifest (.json) to the shared library it names.
std::string icd_library_from_json(const std::string& json_path) {
  std::ifstream f(json_path);
  if (!f) return std::string();
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t p = content.find("\"library_path\"");
  if (p == std::string::npos) return std::string();
  p = content.find(':', p);
  if (p == std::string::npos) return std::string();
  size_t q1 = content.find('"', p);
  size_t q2 = content.find('"', q1 + 1);
  if (q1 == std::string::npos || q2 == std::string::npos) return std::string();
  std::string lib = content.substr(q1 + 1, q2 - q1 - 1);
  if (!lib.empty() && lib[0] != '/') {
    size_t slash = json_path.find_last_of('/');
    if (slash != std::string::npos) lib = json_path.substr(0, slash + 1) + lib;
  }
  return lib;
}

// Try the system loader first, then a bare ICD.
std::vector<std::string> provider_candidates(const std::string& icd_override) {
  std::vector<std::string> out;
  if (!icd_override.empty()) {
    out.push_back(icd_override);
    return out;
  }
  const char* env_icd = std::getenv("VK_ICD_FILENAMES");
  if (!env_icd) env_icd = std::getenv("VK_DRIVER_FILES");
  if (env_icd && *env_icd) {
    std::string e = env_icd;
    if (e.size() > 5 && e.compare(e.size() - 5, 5, ".json") == 0) {
      std::string lib = icd_library_from_json(e);
      if (!lib.empty()) out.push_back(lib);
    } else {
      out.push_back(e);
    }
  }
  // Standard ICD manifest locations.
  for (const char* dir : {"/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"}) {
    DIR* d = opendir(dir);
    if (!d) continue;
    while (dirent* ent = readdir(d)) {
      std::string name = ent->d_name;
      if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
        std::string lib = icd_library_from_json(std::string(dir) + "/" + name);
        if (!lib.empty()) out.push_back(lib);
      }
    }
    closedir(d);
  }
  out.push_back("libvulkan.so.1");
  out.push_back("libvulkan.so");
  out.push_back("libvk_swiftshader.so");
  out.push_back("/usr/lib/x86_64-linux-gnu/libvk_swiftshader.so");
  out.push_back("/usr/local/lib/libvk_swiftshader.so");
  out.push_back("./libvk_swiftshader.so");
  return out;
}

using PFN_vkGetInstanceProcAddr_t = PFN_vkGetInstanceProcAddr;

}  // namespace

void load_vulkan(VkApi& api, const std::string& icd_override) {
  PFN_vkGetInstanceProcAddr_t gipa = nullptr;
  for (const auto& cand : provider_candidates(icd_override)) {
    void* lib = dlopen_quiet(cand);
    if (!lib) continue;
    auto* sym = reinterpret_cast<PFN_vkGetInstanceProcAddr_t>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (!sym) sym = reinterpret_cast<PFN_vkGetInstanceProcAddr_t>(dlsym(lib, "vk_icdGetInstanceProcAddr"));
    if (!sym) {
      dlclose(lib);
      continue;
    }
    api.library = lib;
    api.instance_gpa = reinterpret_cast<void*>(sym);
    api.provider = cand;
    gipa = sym;
    break;
  }
  if (!gipa) {
    throw std::runtime_error(
        "No Vulkan implementation found. Install a Vulkan loader + driver, or pass "
        "--icd /path/to/libvk_swiftshader.so");
  }

  auto get_global = [&](const char* name) -> PFN_vkVoidFunction {
    return gipa(nullptr, name);
  };

  // Global / instance independent commands.
  api.vkEnumerateInstanceVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(get_global("vkEnumerateInstanceVersion"));
  api.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(get_global("vkCreateInstance"));
  api.vkEnumerateInstanceExtensionProperties =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          get_global("vkEnumerateInstanceExtensionProperties"));
  if (!api.vkCreateInstance) throw std::runtime_error("vkCreateInstance unavailable");

#define HGEN_LOAD(name)                                              \
  if (!api.name && get_global(#name))                                 \
    api.name = reinterpret_cast<PFN_##name>(get_global(#name));
  HGEN_VK_FUNCTIONS(HGEN_LOAD)
#undef HGEN_LOAD

  if (!api.vkEnumerateInstanceExtensionProperties) {
    // The SwiftShader ICD only exposes global commands through
    // vk_icdGetInstanceProcAddr on some builds; both spellings were tried above.
  }
}

std::vector<std::string> list_devices(const VkApi& api) {
  std::vector<std::string> names;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "HairGenVulkan";
  app.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (api.vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return names;

  // Instance level entry points must be resolved through the instance.
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(api.instance_gpa);
  auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
  auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
      gipa(instance, "vkGetPhysicalDeviceProperties"));
  auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
  if (!enumerate || !get_props) return names;

  uint32_t count = 0;
  enumerate(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  enumerate(instance, &count, devices.data());
  for (auto d : devices) {
    VkPhysicalDeviceProperties props{};
    get_props(d, &props);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s (%s, api %u.%u.%u, %s)", props.deviceName,
             props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "cpu"
             : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete"
             : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated"
                                                             : "other",
             VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
             VK_VERSION_PATCH(props.apiVersion), props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ?
                 "software" : "hardware");
    names.emplace_back(buf);
  }
  if (destroy) destroy(instance, nullptr);
  return names;
}

}  // namespace hgen
