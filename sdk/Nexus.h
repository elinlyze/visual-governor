#pragma once

// Minimal Nexus API v6 declarations required by this addon.
// Keep the field order identical to Nexus.h for every AddonAPI_t field we access.
// Source of truth: RaidcoreGG/RCGG-lib-nexus-api Nexus.h.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <windows.h>

#define NEXUS_API_VERSION 6
#define DL_NEXUS_LINK "DL_NEXUS_LINK"
#define DL_MUMBLE_LINK_IDENTITY "DL_MUMBLE_LINK_IDENTITY"

struct AddonAPI_t;

typedef enum ERenderType
{
    RT_PreRender,
    RT_Render,
    RT_PostRender,
    RT_OptionsRender
} ERenderType;

typedef enum EMHStatus
{
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} EMHStatus;

typedef enum ELogLevel
{
    LOGL_CRITICAL = 1,
    LOGL_WARNING = 2,
    LOGL_INFO = 3,
    LOGL_DEBUG = 4,
    LOGL_TRACE = 5
} ELogLevel;

// Only required as a type for function pointer layout.
typedef enum EGameBinds
{
    GB_None = -1
} EGameBinds;

typedef enum EAddonFlags
{
    AF_None              = 0,
    AF_IsVolatile        = 1 << 0,
    AF_DisableHotloading = 1 << 1,
    AF_LaunchOnly        = 1 << 2
} EAddonFlags;

typedef enum EUpdateProvider
{
    UP_None     = 0,
    UP_Raidcore = 1,
    UP_GitHub   = 2,
    UP_Direct   = 3,
    UP_Self     = 4
} EUpdateProvider;

typedef struct Keybind_t
{
    uint16_t Key;
    bool     Alt;
    bool     Ctrl;
    bool     Shift;
} Keybind_t;

typedef struct Texture_t
{
    uint32_t Width;
    uint32_t Height;
    void*    Resource; // ID3D11ShaderResourceView*
} Texture_t;

typedef struct NexusLinkData_t
{
    uint32_t Width;
    uint32_t Height;
    float    Scaling;
    bool     IsMoving;
    bool     IsCameraMoving;
    bool     IsGameplay;
    void*    Font;    // ImFont*
    void*    FontBig; // ImFont*
    void*    FontUI;  // ImFont*
} NexusLinkData_t;

typedef struct AddonVersion_t
{
    uint16_t Major;
    uint16_t Minor;
    uint16_t Build;
    uint16_t Revision;
} AddonVersion_t;

typedef void        (*ADDON_LOAD)                       (AddonAPI_t* aAPI);
typedef void        (*ADDON_UNLOAD)                     ();
typedef void        (*GUI_RENDER)                       ();
typedef void        (*GUI_ADDRENDER)                    (ERenderType aRenderType, GUI_RENDER aRenderCallback);
typedef void        (*GUI_REMRENDER)                    (GUI_RENDER aRenderCallback);
typedef void        (*GUI_REGISTERCLOSEONESCAPE)        (const char* aWindowName, bool* aIsVisible);
typedef void        (*GUI_DEREGISTERCLOSEONESCAPE)      (const char* aWindowName);
typedef void        (*UPDATER_REQUESTUPDATE)            (uint32_t aSignature, const char* aUpdateURL);
typedef const char* (*PATHS_GETGAMEDIR)                 ();
typedef const char* (*PATHS_GETADDONDIR)                (const char* aName);
typedef const char* (*PATHS_GETCOMMONDIR)               ();
typedef EMHStatus   (__stdcall* MINHOOK_CREATE)         (LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef EMHStatus   (__stdcall* MINHOOK_REMOVE)         (LPVOID pTarget);
typedef EMHStatus   (__stdcall* MINHOOK_ENABLE)         (LPVOID pTarget);
typedef EMHStatus   (__stdcall* MINHOOK_DISABLE)        (LPVOID pTarget);
typedef void        (*LOGGER_LOG)                       (ELogLevel aLogLevel, const char* aChannel, const char* aStr);
typedef void        (*ALERTS_NOTIFY)                    (const char* aMessage);
typedef void        (*EVENT_CONSUME)                    (void* aEventArgs);
typedef void        (*EVENTS_RAISE)                     (const char* aIdentifier, void* aEventData);
typedef void        (*EVENTS_RAISENOTIFICATION)         (const char* aIdentifier);
typedef void        (*EVENTS_RAISE_TARGETED)            (uint32_t aSignature, const char* aIdentifier, void* aEventData);
typedef void        (*EVENTS_RAISENOTIFICATION_TARGETED)(uint32_t aSignature, const char* aIdentifier);
typedef void        (*EVENTS_SUBSCRIBE)                 (const char* aIdentifier, EVENT_CONSUME aConsumeEventCallback);
typedef UINT        (*WNDPROC_CALLBACK)                 (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
typedef void        (*WNDPROC_ADDREM)                   (WNDPROC_CALLBACK aWndProcCallback);
typedef LRESULT     (*WNDPROC_SENDTOGAME)               (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
typedef void        (*INPUTBINDS_PROCESS)               (const char* aIdentifier, bool aIsRelease);
typedef void        (*INPUTBINDS_INVOKE)                (const char* aIdentifier, bool aIsRelease);
typedef void        (*INPUTBINDS_REGISTERWITHSTRING)    (const char* aIdentifier, INPUTBINDS_PROCESS aKeybindHandler, const char* aKeybind);
typedef void        (*INPUTBINDS_REGISTERWITHSTRUCT)    (const char* aIdentifier, INPUTBINDS_PROCESS aKeybindHandler, Keybind_t aKeybind);
typedef void        (*INPUTBINDS_DEREGISTER)            (const char* aIdentifier);
typedef void        (*GAMEBINDS_PRESSASYNC)             (EGameBinds aGameBind);
typedef void        (*GAMEBINDS_RELEASEASYNC)           (EGameBinds aGameBind);
typedef void        (*GAMEBINDS_INVOKEASYNC)            (EGameBinds aGameBind, int32_t aDuration);
typedef void        (*GAMEBINDS_PRESS)                  (EGameBinds aGameBind);
typedef void        (*GAMEBINDS_RELEASE)                (EGameBinds aGameBind);
typedef bool        (*GAMEBINDS_ISBOUND)                (EGameBinds aGameBind);
typedef void*       (*DATALINK_GET)                     (const char* aIdentifier);
typedef void*       (*DATALINK_SHARE)                   (const char* aIdentifier, uint64_t aResourceSize);

typedef struct AddonDefinition_t
{
    uint32_t        Signature;
    uint32_t        APIVersion;
    const char*     Name;
    AddonVersion_t  Version;
    const char*     Author;
    const char*     Description;
    ADDON_LOAD      Load;
    ADDON_UNLOAD    Unload;
    EAddonFlags     Flags;
    EUpdateProvider Provider;
    const char*     UpdateLink;
} AddonDefinition_t;

typedef struct AddonAPI_t
{
    void* SwapChain;
    void* ImguiContext;
    void* ImguiMalloc;
    void* ImguiFree;

    GUI_ADDRENDER                     GUI_Register;
    GUI_REMRENDER                     GUI_Deregister;
    UPDATER_REQUESTUPDATE             RequestUpdate;
    LOGGER_LOG                        Log;
    ALERTS_NOTIFY                     GUI_SendAlert;
    GUI_REGISTERCLOSEONESCAPE         GUI_RegisterCloseOnEscape;
    GUI_DEREGISTERCLOSEONESCAPE       GUI_DeregisterCloseOnEscape;
    PATHS_GETGAMEDIR                  Paths_GetGameDirectory;
    PATHS_GETADDONDIR                 Paths_GetAddonDirectory;
    PATHS_GETCOMMONDIR                Paths_GetCommonDirectory;
    MINHOOK_CREATE                    MinHook_Create;
    MINHOOK_REMOVE                    MinHook_Remove;
    MINHOOK_ENABLE                    MinHook_Enable;
    MINHOOK_DISABLE                   MinHook_Disable;
    EVENTS_RAISE                      Events_Raise;
    EVENTS_RAISENOTIFICATION          Events_RaiseNotification;
    EVENTS_RAISE_TARGETED             Events_RaiseTargeted;
    EVENTS_RAISENOTIFICATION_TARGETED Events_RaiseNotificationTargeted;
    EVENTS_SUBSCRIBE                  Events_Subscribe;
    EVENTS_SUBSCRIBE                  Events_Unsubscribe;
    WNDPROC_ADDREM                    WndProc_Register;
    WNDPROC_ADDREM                    WndProc_Deregister;
    WNDPROC_SENDTOGAME                WndProc_SendToGameOnly;
    INPUTBINDS_INVOKE                 InputBinds_Invoke;
    INPUTBINDS_REGISTERWITHSTRING     InputBinds_RegisterWithString;
    INPUTBINDS_REGISTERWITHSTRUCT     InputBinds_RegisterWithStruct;
    INPUTBINDS_DEREGISTER             InputBinds_Deregister;
    GAMEBINDS_PRESSASYNC              GameBinds_PressAsync;
    GAMEBINDS_RELEASEASYNC            GameBinds_ReleaseAsync;
    GAMEBINDS_INVOKEASYNC             GameBinds_InvokeAsync;
    GAMEBINDS_PRESS                   GameBinds_Press;
    GAMEBINDS_RELEASE                 GameBinds_Release;
    GAMEBINDS_ISBOUND                 GameBinds_IsBound;
    DATALINK_GET                      DataLink_Get;
    DATALINK_SHARE                    DataLink_Share;
} AddonAPI_t;

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef();
