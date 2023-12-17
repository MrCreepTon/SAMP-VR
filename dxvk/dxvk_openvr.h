#pragma once

#include <mutex>
#include <vector>

#include "dxvk_extension_provider.h"

#include <openvr/openvr.h>
//#include <openvr/openvr_old.hpp>
#include "dxvk_device.h"
#include "dxvk_image.h"
#include "dxvk_instance.h"
#include "sa/sa_main.h"

// shit
/*namespace vr {
  class IVRCompositor;
  class IVRSystem;
}*/

namespace dxvk {

  class DxvkInstance;

  /**
   * \brief OpenVR instance
   * 
   * Loads Initializes OpenVR to provide
   * access to Vulkan extension queries.
   */
  class VrInstance : public DxvkExtensionProvider {
    
  public:
    
    VrInstance();
    ~VrInstance();

    void HandleImageInGame();
    void SendVulkanImage(DxvkDevice* pDevice, VkQueue* pQueue);

    std::string_view getName();

    DxvkNameSet getInstanceExtensions();

    DxvkNameSet getDeviceExtensions(
            uint32_t      adapterId);
    
    void initInstanceExtensions();

    void initDeviceExtensions(
      const DxvkInstance* instance);

    static VrInstance s_instance;

    VkImage image;
    vr::IVRSystem* vr_context = nullptr;

    vr::VRActionSetHandle_t actionSet;
    vr::VRActiveActionSet_t activeActionSet;

  private:

    vr::VRVulkanTextureData_t vulkanData;

    uint32_t renderWidth, renderHeight;

    vr::VRTextureBounds_t leftBounds, rightBounds;

    dxvk::mutex           m_mutex;
    HKEY                  m_vr_key     = nullptr;
    
    HMODULE               m_ovrApi     = nullptr;
    bool m_no_vr;
    bool m_loadedOvrApi      = false;
    bool m_initializedOpenVr = false;
    bool m_initializedInsExt = false;
    bool m_initializedDevExt = false;

    vr::IVRCompositor* m_compositor = nullptr;

    DxvkNameSet              m_insExtensions;
    std::vector<DxvkNameSet> m_devExtensions;
    
    DxvkNameSet queryInstanceExtensions() const;

    DxvkNameSet queryDeviceExtensions(
            Rc<DxvkAdapter>           adapter) const;

    DxvkNameSet parseExtensionList(
      const std::string&              str) const;
    
    void InstallApplicationManifest(const char* fileName);
    void SetActionManifest(const char* fileName);
    vr::IVRCompositor* getCompositor();

    void shutdown();

    HMODULE loadLibrary();

    void freeLibrary();

    void* getSym(const char* sym);

    bool waitVrKeyReady() const;
  };

  extern VrInstance g_vrInstance;
  
}
