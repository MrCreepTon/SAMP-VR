#include "dxvk_openvr.h"
#include "../wsi/wsi_window.h"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include "../dxgi/dxgi_monitor.h"

using VR_InitInternalProc        = vr::IVRSystem* (VR_CALLTYPE *)(vr::EVRInitError*, vr::EVRApplicationType);
using VR_ShutdownInternalProc    = void  (VR_CALLTYPE *)();
using VR_GetGenericInterfaceProc = void* (VR_CALLTYPE *)(const char*, vr::EVRInitError*);

namespace dxvk {
  
  struct VrFunctions {
    VR_InitInternalProc        initInternal        = nullptr;
    VR_ShutdownInternalProc    shutdownInternal    = nullptr;
    VR_GetGenericInterfaceProc getGenericInterface = nullptr;
  };
  
  VrFunctions g_vrFunctions;
  VrInstance VrInstance::s_instance;
  bool firstLaunch = true;

  VrInstance:: VrInstance() {
    m_no_vr = env::getEnvVar("DXVK_NO_VR") == "1";
  }
  VrInstance::~VrInstance() { }


  std::string_view VrInstance::getName() {
    return "OpenVR";
  }
  
  
  DxvkNameSet VrInstance::getInstanceExtensions() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    return m_insExtensions;
  }


  DxvkNameSet VrInstance::getDeviceExtensions(uint32_t adapterId) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    
    if (adapterId < m_devExtensions.size())
      return m_devExtensions[adapterId];
    
    return DxvkNameSet();
  }

  HWND g_HWND = NULL;
  BOOL CALLBACK EnumWindowsProcMy(HWND hwnd, LPARAM lParam)
  {
      DWORD lpdwProcessId;
      GetWindowThreadProcessId(hwnd, &lpdwProcessId);
      if (lpdwProcessId == lParam && g_HWND != GetConsoleWindow())
      {
          g_HWND = hwnd;
          return FALSE;
      }
      return TRUE;
  }

  void VrInstance::HandleImageInGame() {
      if (vr_context)
        vr::VRInput()->UpdateActionState(&activeActionSet, sizeof(vr::VRActiveActionSet_t), 1);
      sa::OnVR(vr_context, renderWidth, renderHeight);
  }

  // Send vulkan image
  void VrInstance::SendVulkanImage(DxvkDevice* pDevice, VkQueue* pQueue) {
      if (vr_context) {

        vulkanData.m_nImage = (uint64_t)image;
        vulkanData.m_pDevice = (VkDevice_T*)(pDevice->handle());
        vulkanData.m_pPhysicalDevice = (VkPhysicalDevice_T*)pDevice->adapter()->handle();
        vulkanData.m_pQueue = (VkQueue_T*)*pQueue;
        vulkanData.m_pInstance = (VkInstance_T*)pDevice->instance()->handle();
        vulkanData.m_nQueueFamilyIndex = pDevice->queues().graphics.queueFamily;

        wsi::WsiMode mode = { };
        wsi::getCurrentDisplayMode(wsi::getDefaultMonitor(), &mode);
        renderWidth = mode.width;
        renderHeight = mode.height;

        vulkanData.m_nWidth = renderWidth;
        vulkanData.m_nHeight = renderHeight;
        vulkanData.m_nFormat = VK_FORMAT_R8G8B8A8_SRGB;
        vulkanData.m_nSampleCount = 4;

        vr::Texture_t texture = { &vulkanData, vr::TextureType_Vulkan, vr::ColorSpace_Auto };
        vr::VRCompositor()->Submit(vr::Eye_Left, &texture, &leftBounds);
        vr::VRCompositor()->Submit(vr::Eye_Right, &texture, &rightBounds);
        
     }
  }

  void VrInstance::InstallApplicationManifest(const char* fileName)
  {
      char currentDir[256];
      GetCurrentDirectory(256, currentDir);
      char path[256];
      sprintf_s(path, 256, "%s\\%s", currentDir, fileName);

      vr::VRApplications()->AddApplicationManifest(path);
  }

  void VrInstance::SetActionManifest(const char* fileName) {
      char currentDir[256];
      GetCurrentDirectory(256, currentDir);
      char path[256];
      sprintf_s(path, 256, "%s\\%s", currentDir, fileName);

      auto vrInput = vr::VRInput();

      if (vrInput->SetActionManifestPath(path) != vr::VRInputError_None)
      {
          MessageBox(GetConsoleWindow(), "SetActionManifestPath failed", "Oops", 0);
      }

      vrInput->GetActionHandle("/actions/main/in/Walk", &sa::vrActions.actionWalk);
      vrInput->GetActionHandle("/actions/main/in/RotateOrJump", &sa::vrActions.actionRotate);

      vrInput->GetActionHandle("/actions/main/in/Crouch", &sa::vrActions.actionCrouch);
      vrInput->GetActionHandle("/actions/main/in/EnterExit", &sa::vrActions.actionEnterExit);
        vrInput->GetActionHandle("/actions/main/in/Hit", &sa::vrActions.actionHit);

      vrInput->GetActionSetHandle("/actions/main", &actionSet);
      activeActionSet = {};
      activeActionSet.ulActionSet = actionSet;

  }


  void VrInstance::initInstanceExtensions() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    vr::EVRInitError err;
    vr_context = vr::VR_Init(&err, vr::EVRApplicationType::VRApplication_Scene);
    if (vr_context) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);

        vr_context->GetRecommendedRenderTargetSize(&renderWidth, &renderHeight);
        printf("Recommended resolution: %dx%d\n", renderWidth, renderHeight);

        InstallApplicationManifest("sampvr_manifest/manifest.vrmanifest");
        SetActionManifest("sampvr_manifest/action_manifest.json");
        

        vr::IVRSystem* m_System = vr::OpenVRInternal_ModuleContext().VRSystem();

        float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
        m_System->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

        float r_left = 0.0f, r_right = 0.0f, r_top = 0.0f, r_bottom = 0.0f;
        m_System->GetProjectionRaw(vr::EVREye::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

        float tanHalfFov[2];

        tanHalfFov[0] = std::max({ -l_left, l_right, -r_left, r_right });
        tanHalfFov[1] = std::max({ -l_top, l_bottom, -r_top, r_bottom });

        leftBounds.uMin = 0.5f + 0.5f * l_left / tanHalfFov[0];
        leftBounds.uMax = 0.5f + 0.5f * l_right / tanHalfFov[0];
        leftBounds.vMin = 0.5f - 0.5f * l_bottom / tanHalfFov[1];
        leftBounds.vMax = 0.5f - 0.5f * l_top / tanHalfFov[1];

        rightBounds.uMin = 0.5f + 0.5f * r_left / tanHalfFov[0];
        rightBounds.uMax = 0.5f + 0.5f * r_right / tanHalfFov[0];
        rightBounds.vMin = 0.5f - 0.5f * r_bottom / tanHalfFov[1];
        rightBounds.vMax = 0.5f - 0.5f * r_top / tanHalfFov[1];

    }

  }


  void VrInstance::initDeviceExtensions(const DxvkInstance* instance) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    if (m_no_vr || (!m_vr_key && !m_compositor) || m_initializedDevExt)
      return;
    
    for (uint32_t i = 0; instance->enumAdapters(i) != nullptr; i++) {
      m_devExtensions.push_back(this->queryDeviceExtensions(
        instance->enumAdapters(i)));
    }

    m_initializedDevExt = true;
    this->shutdown();
  }

  bool VrInstance::waitVrKeyReady() const {
    DWORD type, value, wait_status, size;
    LSTATUS status;
    HANDLE event;

    size = sizeof(value);
    if ((status = RegQueryValueExA(m_vr_key, "state", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size)))
    {
        Logger::err(str::format("OpenVR: could not query value, status ", status));
        return false;
    }
    if (type != REG_DWORD)
    {
        Logger::err(str::format("OpenVR: unexpected value type ", type));
        return false;
    }

    if (value)
        return value == 1;

    event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    while (1)
    {
        if (RegNotifyChangeKeyValue(m_vr_key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, event, TRUE))
        {
            Logger::err("Error registering registry change notification");
            goto done;
        }
        size = sizeof(value);
        if ((status = RegQueryValueExA(m_vr_key, "state", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size)))
        {
            Logger::err(str::format("OpenVR: could not query value, status ", status));
            goto done;
        }
        if (value)
            break;
        while ((wait_status = WaitForSingleObject(event, 1000)) == WAIT_TIMEOUT)
            Logger::warn("VR state wait timeout (retrying)");

        if (wait_status != WAIT_OBJECT_0)
        {
            Logger::err(str::format("Got unexpected wait status ", wait_status));
            break;
        }
    }

  done:
    CloseHandle(event);
    return value == 1;
  }

  DxvkNameSet VrInstance::queryInstanceExtensions() const {
    std::vector<char> extensionList;
    DWORD len;

    if (m_vr_key)
    {
        LSTATUS status;
        DWORD type;

        if (!this->waitVrKeyReady())
            return DxvkNameSet();

        len = 0;
        if ((status = RegQueryValueExA(m_vr_key, "openvr_vulkan_instance_extensions", nullptr, &type, nullptr, &len)))
        {
            Logger::err(str::format("OpenVR: could not query value, status ", status));
            return DxvkNameSet();
        }
        extensionList.resize(len);
        if ((status = RegQueryValueExA(m_vr_key, "openvr_vulkan_instance_extensions", nullptr, &type, reinterpret_cast<BYTE*>(extensionList.data()), &len)))
        {
            Logger::err(str::format("OpenVR: could not query value, status ", status));
            return DxvkNameSet();
        }
    }
    else
    {
        len = m_compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
        extensionList.resize(len);
        len = m_compositor->GetVulkanInstanceExtensionsRequired(extensionList.data(), len);
    }
    return parseExtensionList(std::string(extensionList.data(), len));
  }
  
  
  DxvkNameSet VrInstance::queryDeviceExtensions(Rc<DxvkAdapter> adapter) const {
    std::vector<char> extensionList;
    DWORD len;

    if (m_vr_key)
    {
        LSTATUS status;
        char name[256];
        DWORD type;

        if (!this->waitVrKeyReady())
            return DxvkNameSet();

        sprintf(name, "PCIID:%04x:%04x", adapter->deviceProperties().vendorID, adapter->deviceProperties().deviceID);
        len = 0;
        if ((status = RegQueryValueExA(m_vr_key, name, nullptr, &type, nullptr, &len)))
        {
            Logger::err(str::format("OpenVR: could not query value, status ", status));
            return DxvkNameSet();
        }
        extensionList.resize(len);
        if ((status = RegQueryValueExA(m_vr_key, name, nullptr, &type, reinterpret_cast<BYTE*>(extensionList.data()), &len)))
        {
            Logger::err(str::format("OpenVR: could not query value, status ", status));
            return DxvkNameSet();
        }
    }
    else
    {
        len = m_compositor->GetVulkanDeviceExtensionsRequired(adapter->handle(), nullptr, 0);
        extensionList.resize(len);
        len = m_compositor->GetVulkanDeviceExtensionsRequired(adapter->handle(), extensionList.data(), len);
    }
    return parseExtensionList(std::string(extensionList.data(), len));
  }
  
  
  DxvkNameSet VrInstance::parseExtensionList(const std::string& str) const {
    DxvkNameSet result;
    
    std::stringstream strstream(str);
    std::string       section;
    
    while (std::getline(strstream, section, ' '))
      result.add(section.c_str());
    
    return result;
  }
  
  
  vr::IVRCompositor* VrInstance::getCompositor() {
    // Skip OpenVR initialization if requested
    
    // Locate the OpenVR DLL if loaded by the process. Some
    // applications may not have OpenVR loaded at the time
    // they create the DXGI instance, so we try our own DLL.
    
    m_ovrApi = this->loadLibrary();
    
    if (!m_ovrApi) {
      Logger::info("OpenVR: Failed to locate module");
      return nullptr;
    }
    
    // Load method used to retrieve the IVRCompositor interface
    g_vrFunctions.initInternal        = reinterpret_cast<VR_InitInternalProc>       (this->getSym("VR_InitInternal"));
    g_vrFunctions.shutdownInternal    = reinterpret_cast<VR_ShutdownInternalProc>   (this->getSym("VR_ShutdownInternal"));
    g_vrFunctions.getGenericInterface = reinterpret_cast<VR_GetGenericInterfaceProc>(this->getSym("VR_GetGenericInterface"));
    
    if (!g_vrFunctions.getGenericInterface) {
      Logger::warn("OpenVR: VR_GetGenericInterface not found");
      return nullptr;
    }
    
    // Retrieve the compositor interface
    vr::EVRInitError error = vr::VRInitError_None;
    
    vr::IVRCompositor* compositor = reinterpret_cast<vr::IVRCompositor*>(
      g_vrFunctions.getGenericInterface(vr::IVRCompositor_Version, &error));
    
    if (error != vr::VRInitError_None || !compositor) {
      if (!g_vrFunctions.initInternal
       || !g_vrFunctions.shutdownInternal) {
        Logger::warn("OpenVR: VR_InitInternal or VR_ShutdownInternal not found");
        return nullptr;
      }

      // If the app has not initialized OpenVR yet, we need
      // to do it now in order to grab a compositor instance
      g_vrFunctions.initInternal(&error, vr::VRApplication_Background);
      m_initializedOpenVr = error == vr::VRInitError_None;

      if (error != vr::VRInitError_None) {
        Logger::warn("OpenVR: Failed to initialize OpenVR");
        return nullptr;
      }

      compositor = reinterpret_cast<vr::IVRCompositor*>(
        g_vrFunctions.getGenericInterface(vr::IVRCompositor_Version, &error));
      
      if (error != vr::VRInitError_None || !compositor) {
        Logger::warn("OpenVR: Failed to query compositor interface");
        this->shutdown();
        return nullptr;
      }
    }
    
    Logger::info("OpenVR: Compositor interface found");
    return compositor;
  }


  void VrInstance::shutdown() {
    if (m_vr_key)
    {
        RegCloseKey(m_vr_key);
        m_vr_key = nullptr;
    }

    if (m_initializedOpenVr)
      g_vrFunctions.shutdownInternal();

    if (m_loadedOvrApi)
      this->freeLibrary();
    
    m_initializedOpenVr = false;
    m_loadedOvrApi      = false;
  }


  HMODULE VrInstance::loadLibrary() {
    HMODULE handle;

    // Use openvr_api.dll only if already loaded in the process (and reference it which GetModuleHandleEx does without
    // GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT flag).
    if (!::GetModuleHandleEx(0, "openvr_api.dll", &handle))
      handle = ::LoadLibrary("openvr_api_dxvk.dll");

    m_loadedOvrApi = handle != nullptr;
    return handle;
  }


  void VrInstance::freeLibrary() {
    ::FreeLibrary(m_ovrApi);
  }

  
  void* VrInstance::getSym(const char* sym) {
    return reinterpret_cast<void*>(
      ::GetProcAddress(m_ovrApi, sym));
  }
  
}
