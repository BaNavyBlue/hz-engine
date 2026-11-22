/* A stub of SDL3's declarations as src/platform/platform_sdl3.c uses them,
 * and (session 34) src/platform/crt_gpu.c and tools/crt_gpu_check.c, the
 * CRT chain's SDL_GPU, for `make sdl-syntax`: a syntax and type check where
 * SDL3 isn't installed (Claude's sandbox). Written from SDL3's headers as
 * known, not copied from them; a real `make hz` is the check that counts.
 * Enum values here are not SDL's. */
#ifndef SDL_STUB_H
#define SDL_STUB_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t Uint8; typedef int16_t Sint16; typedef uint16_t Uint16;
typedef uint32_t Uint32; typedef uint64_t Uint64; typedef int32_t Sint32;
#define SDLCALL
typedef Uint32 SDL_JoystickID; typedef Uint32 SDL_DisplayID; typedef Uint32 SDL_WindowID;
typedef Uint32 SDL_Keycode; typedef Uint16 SDL_Keymod; typedef Uint32 SDL_KeyboardID;
typedef Uint64 SDL_WindowFlags; typedef Uint32 SDL_InitFlags;
typedef enum { SDL_SCANCODE_UNKNOWN = 0, SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41,
  SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_F10 = 67, SDL_SCANCODE_F11 = 68, SDL_SCANCODE_COUNT = 512 } SDL_Scancode;
typedef enum { SDL_GAMEPAD_BUTTON_INVALID = -1, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
  SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE,
  SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
  SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
  SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  SDL_GAMEPAD_BUTTON_COUNT = 26 } SDL_GamepadButton;
typedef enum { SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY } SDL_GamepadAxis;
/* (Session 54) Any gamepad's type, and a joystick SDL has no mapping for. */
typedef enum { SDL_GAMEPAD_TYPE_UNKNOWN, SDL_GAMEPAD_TYPE_STANDARD, SDL_GAMEPAD_TYPE_XBOX360,
  SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_TYPE_PS3, SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_TYPE_PS5,
  SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
  SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR,
  SDL_GAMEPAD_TYPE_GAMECUBE, SDL_GAMEPAD_TYPE_COUNT } SDL_GamepadType;
#define SDL_KMOD_ALT 0x0300u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_GAMEPAD 0x2000u
#define SDL_WINDOW_RESIZABLE 0x20ull
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x2000ull
typedef enum { SDL_PIXELFORMAT_XRGB8888 = 1 } SDL_PixelFormat;
typedef enum { SDL_TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STREAMING, SDL_TEXTUREACCESS_TARGET } SDL_TextureAccess;
typedef enum { SDL_SCALEMODE_NEAREST, SDL_SCALEMODE_LINEAR } SDL_ScaleMode;
typedef Uint32 SDL_BlendMode;
#define SDL_BLENDMODE_NONE 0u
typedef enum { SDL_AUDIO_F32 = 0x8120 } SDL_AudioFormat;
typedef Uint32 SDL_AudioDeviceID;
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)
typedef struct { SDL_AudioFormat format; int channels; int freq; } SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef void (SDLCALL *SDL_AudioStreamCallback)(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);
typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture; typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct { SDL_DisplayID displayID; SDL_PixelFormat format; int w, h; float pixel_density; float refresh_rate; } SDL_DisplayMode;
typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_WindowID windowID; SDL_KeyboardID which;
  SDL_Scancode scancode; SDL_Keycode key; SDL_Keymod mod; Uint16 raw; bool down; bool repeat; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_JoystickID which; } SDL_GamepadDeviceEvent;
typedef struct { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_JoystickID which; Uint8 button; bool down; Uint8 padding1, padding2; } SDL_GamepadButtonEvent;
typedef struct { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_JoystickID which; } SDL_JoyDeviceEvent;
typedef struct { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_JoystickID which; Uint8 button; bool down; Uint8 padding1, padding2; } SDL_JoyButtonEvent;
typedef union { Uint32 type; SDL_KeyboardEvent key; SDL_GamepadDeviceEvent gdevice; SDL_GamepadButtonEvent gbutton;
  SDL_JoyDeviceEvent jdevice; SDL_JoyButtonEvent jbutton; Uint8 padding[128]; } SDL_Event;
enum { SDL_EVENT_QUIT = 0x100, SDL_EVENT_WINDOW_DISPLAY_CHANGED = 0x214,
  SDL_EVENT_WINDOW_ENTER_FULLSCREEN = 0x217, SDL_EVENT_WINDOW_LEAVE_FULLSCREEN,
  SDL_EVENT_WINDOW_HDR_STATE_CHANGED = 0x21A,
  SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_JOYSTICK_BUTTON_DOWN = 0x603, SDL_EVENT_JOYSTICK_ADDED = 0x605,
  SDL_EVENT_JOYSTICK_REMOVED, SDL_EVENT_GAMEPAD_BUTTON_DOWN = 0x651, SDL_EVENT_GAMEPAD_ADDED = 0x653, SDL_EVENT_GAMEPAD_REMOVED };
bool SDL_Init(SDL_InitFlags); void SDL_Quit(void); const char *SDL_GetError(void);
void SDL_Log(const char *fmt, ...); void *SDL_calloc(size_t, size_t); void SDL_free(void *);
/* (Session 53) The start-up's: the program's folder, a path's type, the
 * error box, the window icon's hints. */
typedef enum { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY,
  SDL_PATHTYPE_OTHER } SDL_PathType;
typedef struct { SDL_PathType type; Uint64 size; int64_t create_time, modify_time,
  access_time; } SDL_PathInfo;
const char *SDL_GetBasePath(void); char *SDL_GetCurrentDirectory(void); bool SDL_GetPathInfo(const char *, SDL_PathInfo *);
typedef Uint32 SDL_MessageBoxFlags;
#define SDL_MESSAGEBOX_ERROR 0x10u
bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags, const char *, const char *, SDL_Window *);
bool SDL_SetHint(const char *, const char *);
#define SDL_HINT_WINDOWS_INTRESOURCE_ICON "SDL_WINDOWS_INTRESOURCE_ICON"
#define SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL "SDL_WINDOWS_INTRESOURCE_ICON_SMALL"
#define SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER "SDL_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER"
SDL_Window *SDL_CreateWindow(const char *, int, int, SDL_WindowFlags); void SDL_DestroyWindow(SDL_Window *);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *, const char *); void SDL_DestroyRenderer(SDL_Renderer *);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, SDL_PixelFormat, SDL_TextureAccess, int, int);
void SDL_DestroyTexture(SDL_Texture *); bool SDL_SetTextureScaleMode(SDL_Texture *, SDL_ScaleMode);
bool SDL_SetTextureBlendMode(SDL_Texture *, SDL_BlendMode); bool SDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
bool SDL_SetRenderVSync(SDL_Renderer *, int); const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID);
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *); const char *SDL_GetCurrentVideoDriver(void);
const char *SDL_GetRendererName(SDL_Renderer *); Uint64 SDL_GetTicksNS(void); void SDL_DelayNS(Uint64); void SDL_DelayPrecise(Uint64);
bool SDL_SetWindowFullscreenMode(SDL_Window *, const SDL_DisplayMode *); bool SDL_SetWindowFullscreen(SDL_Window *, bool);
bool SDL_SyncWindow(SDL_Window *); bool SDL_HideCursor(void); bool SDL_ShowCursor(void);
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
bool SDL_PutAudioStreamData(SDL_AudioStream *, const void *, int); bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *);
void SDL_DestroyAudioStream(SDL_AudioStream *);
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID); void SDL_CloseGamepad(SDL_Gamepad *);
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *); const char *SDL_GetGamepadName(SDL_Gamepad *);
bool SDL_GetGamepadButton(SDL_Gamepad *, SDL_GamepadButton); Sint16 SDL_GetGamepadAxis(SDL_Gamepad *, SDL_GamepadAxis);
bool SDL_RumbleGamepad(SDL_Gamepad *, Uint16, Uint16, Uint32);
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *); int SDL_AddGamepadMappingsFromFile(const char *);
bool SDL_IsGamepad(SDL_JoystickID); SDL_Joystick *SDL_OpenJoystick(SDL_JoystickID); void SDL_CloseJoystick(SDL_Joystick *);
const char *SDL_GetJoystickName(SDL_Joystick *); bool SDL_RumbleJoystick(SDL_Joystick *, Uint16, Uint16, Uint32);
int SDL_GetNumJoystickButtons(SDL_Joystick *); bool SDL_GetJoystickButton(SDL_Joystick *, int);
int SDL_GetNumJoystickHats(SDL_Joystick *); Uint8 SDL_GetJoystickHat(SDL_Joystick *, int);
int SDL_GetNumJoystickAxes(SDL_Joystick *); Sint16 SDL_GetJoystickAxis(SDL_Joystick *, int);
bool SDL_PollEvent(SDL_Event *); const bool *SDL_GetKeyboardState(int *); SDL_Keymod SDL_GetModState(void);
bool SDL_GetRenderOutputSize(SDL_Renderer *, int *, int *); bool SDL_SetRenderTarget(SDL_Renderer *, SDL_Texture *);
bool SDL_RenderTexture(SDL_Renderer *, SDL_Texture *, const SDL_FRect *, const SDL_FRect *);
bool SDL_RenderClear(SDL_Renderer *); bool SDL_RenderPresent(SDL_Renderer *);
bool SDL_UpdateTexture(SDL_Texture *, const void *, const void *, int);

/* ---- the CRT chain's: properties, files, SDL_GPU ---- */
void *SDL_memset(void *, int, size_t); void *SDL_malloc(size_t);
#define SDL_zero(x) SDL_memset(&(x), 0, sizeof((x)))
const char *SDL_getenv(const char *); void *SDL_LoadFile(const char *, size_t *);
int SDL_GetVersion(void);
#define SDL_VERSIONNUM_MAJOR(v) ((v) / 1000000)
#define SDL_VERSIONNUM_MINOR(v) (((v) / 1000) % 1000)
#define SDL_VERSIONNUM_MICRO(v) ((v) % 1000)
typedef Uint32 SDL_PropertiesID;
#define SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN "SDL.window.HDR_enabled"
#define SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT "SDL.window.HDR_headroom"
#define SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT "SDL.window.SDR_white_level"
SDL_PropertiesID SDL_GetWindowProperties(SDL_Window *);
bool SDL_GetBooleanProperty(SDL_PropertiesID, const char *, bool);
float SDL_GetFloatProperty(SDL_PropertiesID, const char *, float);
const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID);
float SDL_GetWindowPixelDensity(SDL_Window *);
bool SDL_GetWindowSizeInPixels(SDL_Window *, int *, int *);

typedef struct SDL_GPUDevice SDL_GPUDevice; typedef struct SDL_GPUCommandBuffer SDL_GPUCommandBuffer;
typedef struct SDL_GPUCopyPass SDL_GPUCopyPass; typedef struct SDL_GPURenderPass SDL_GPURenderPass;
typedef struct SDL_GPUTexture SDL_GPUTexture; typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUShader SDL_GPUShader; typedef struct SDL_GPUGraphicsPipeline SDL_GPUGraphicsPipeline;
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer; typedef struct SDL_GPUFence SDL_GPUFence;
typedef Uint32 SDL_GPUShaderFormat;
#define SDL_GPU_SHADERFORMAT_SPIRV (1u << 1)
#define SDL_GPU_SHADERFORMAT_DXIL (1u << 3)
#define SDL_GPU_SHADERFORMAT_MSL (1u << 4)
typedef Uint32 SDL_GPUTextureUsageFlags;
#define SDL_GPU_TEXTUREUSAGE_SAMPLER (1u << 0)
#define SDL_GPU_TEXTUREUSAGE_COLOR_TARGET (1u << 1)
typedef enum { SDL_GPU_SHADERSTAGE_VERTEX, SDL_GPU_SHADERSTAGE_FRAGMENT } SDL_GPUShaderStage;
typedef enum { SDL_GPU_TEXTUREFORMAT_INVALID, SDL_GPU_TEXTUREFORMAT_R8_UNORM = 2,
  SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM = 4, SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM = 8,
  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM = 12, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT = 28,
  SDL_GPU_TEXTUREFORMAT_R32_FLOAT = 29, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT = 31 } SDL_GPUTextureFormat;
typedef enum { SDL_GPU_TEXTURETYPE_2D } SDL_GPUTextureType;
typedef enum { SDL_GPU_SAMPLECOUNT_1 } SDL_GPUSampleCount;
typedef enum { SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
  SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR, SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084 } SDL_GPUSwapchainComposition;
typedef enum { SDL_GPU_PRESENTMODE_VSYNC, SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX } SDL_GPUPresentMode;
typedef enum { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD } SDL_GPUTransferBufferUsage;
typedef enum { SDL_GPU_PRIMITIVETYPE_TRIANGLELIST } SDL_GPUPrimitiveType;
typedef enum { SDL_GPU_LOADOP_LOAD, SDL_GPU_LOADOP_CLEAR, SDL_GPU_LOADOP_DONT_CARE } SDL_GPULoadOp;
typedef enum { SDL_GPU_STOREOP_STORE, SDL_GPU_STOREOP_DONT_CARE } SDL_GPUStoreOp;
typedef enum { SDL_GPU_FILLMODE_FILL, SDL_GPU_FILLMODE_LINE } SDL_GPUFillMode;
typedef enum { SDL_GPU_CULLMODE_NONE, SDL_GPU_CULLMODE_FRONT, SDL_GPU_CULLMODE_BACK } SDL_GPUCullMode;
typedef enum { SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE, SDL_GPU_FRONTFACE_CLOCKWISE } SDL_GPUFrontFace;
typedef enum { SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_LINEAR } SDL_GPUFilter;
typedef enum { SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, SDL_GPU_SAMPLERMIPMAPMODE_LINEAR } SDL_GPUSamplerMipmapMode;
typedef enum { SDL_GPU_SAMPLERADDRESSMODE_REPEAT, SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
  SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE } SDL_GPUSamplerAddressMode;
typedef struct { size_t code_size; const Uint8 *code; const char *entrypoint; SDL_GPUShaderFormat format;
  SDL_GPUShaderStage stage; Uint32 num_samplers, num_storage_textures, num_storage_buffers, num_uniform_buffers;
  SDL_PropertiesID props; } SDL_GPUShaderCreateInfo;
typedef struct { SDL_GPUTextureType type; SDL_GPUTextureFormat format; SDL_GPUTextureUsageFlags usage;
  Uint32 width, height, layer_count_or_depth, num_levels; SDL_GPUSampleCount sample_count;
  SDL_PropertiesID props; } SDL_GPUTextureCreateInfo;
typedef struct { SDL_GPUFilter min_filter, mag_filter; SDL_GPUSamplerMipmapMode mipmap_mode;
  SDL_GPUSamplerAddressMode address_mode_u, address_mode_v, address_mode_w; float mip_lod_bias, max_anisotropy;
  int compare_op; float min_lod, max_lod; bool enable_anisotropy, enable_compare; Uint8 padding1, padding2;
  SDL_PropertiesID props; } SDL_GPUSamplerCreateInfo;
typedef struct { SDL_GPUTransferBufferUsage usage; Uint32 size; SDL_PropertiesID props; } SDL_GPUTransferBufferCreateInfo;
typedef struct { int src_color_blendfactor, dst_color_blendfactor, color_blend_op, src_alpha_blendfactor,
  dst_alpha_blendfactor, alpha_blend_op; Uint8 color_write_mask; bool enable_blend, enable_color_write_mask; } SDL_GPUColorTargetBlendState;
typedef struct { SDL_GPUTextureFormat format; SDL_GPUColorTargetBlendState blend_state; } SDL_GPUColorTargetDescription;
typedef struct { const SDL_GPUColorTargetDescription *color_target_descriptions; Uint32 num_color_targets;
  SDL_GPUTextureFormat depth_stencil_format; bool has_depth_stencil_target; } SDL_GPUGraphicsPipelineTargetInfo;
typedef struct { SDL_GPUFillMode fill_mode; SDL_GPUCullMode cull_mode; SDL_GPUFrontFace front_face;
  float depth_bias_constant_factor, depth_bias_clamp, depth_bias_slope_factor; bool enable_depth_bias, enable_depth_clip; } SDL_GPURasterizerState;
typedef struct { SDL_GPUSampleCount sample_count; Uint32 sample_mask; bool enable_mask; } SDL_GPUMultisampleState;
typedef struct { const void *vertex_buffer_descriptions; Uint32 num_vertex_buffers; const void *vertex_attributes;
  Uint32 num_vertex_attributes; } SDL_GPUVertexInputState;
typedef struct { int compare_op; bool enable_depth_test, enable_depth_write, enable_stencil_test; } SDL_GPUDepthStencilState;
typedef struct { SDL_GPUShader *vertex_shader; SDL_GPUShader *fragment_shader; SDL_GPUVertexInputState vertex_input_state;
  SDL_GPUPrimitiveType primitive_type; SDL_GPURasterizerState rasterizer_state; SDL_GPUMultisampleState multisample_state;
  SDL_GPUDepthStencilState depth_stencil_state; SDL_GPUGraphicsPipelineTargetInfo target_info;
  SDL_PropertiesID props; } SDL_GPUGraphicsPipelineCreateInfo;
typedef struct { float r, g, b, a; } SDL_FColor;
typedef struct { SDL_GPUTexture *texture; Uint32 mip_level, layer_or_depth_plane; SDL_FColor clear_color;
  SDL_GPULoadOp load_op; SDL_GPUStoreOp store_op; SDL_GPUTexture *resolve_texture; Uint32 resolve_mip_level,
  resolve_layer; bool cycle, cycle_resolve_texture; Uint8 padding1, padding2; } SDL_GPUColorTargetInfo;
typedef struct SDL_GPUDepthStencilTargetInfo SDL_GPUDepthStencilTargetInfo;
typedef struct { SDL_GPUTexture *texture; SDL_GPUSampler *sampler; } SDL_GPUTextureSamplerBinding;
typedef struct { SDL_GPUTransferBuffer *transfer_buffer; Uint32 offset, pixels_per_row, rows_per_layer; } SDL_GPUTextureTransferInfo;
typedef struct { SDL_GPUTexture *texture; Uint32 mip_level, layer, x, y, z, w, h, d; } SDL_GPUTextureRegion;
SDL_GPUDevice *SDL_CreateGPUDevice(SDL_GPUShaderFormat, bool, const char *); void SDL_DestroyGPUDevice(SDL_GPUDevice *);
const char *SDL_GetGPUDeviceDriver(SDL_GPUDevice *); SDL_GPUShaderFormat SDL_GetGPUShaderFormats(SDL_GPUDevice *);
bool SDL_ClaimWindowForGPUDevice(SDL_GPUDevice *, SDL_Window *); void SDL_ReleaseWindowFromGPUDevice(SDL_GPUDevice *, SDL_Window *);
bool SDL_SetGPUSwapchainParameters(SDL_GPUDevice *, SDL_Window *, SDL_GPUSwapchainComposition, SDL_GPUPresentMode);
SDL_GPUTextureFormat SDL_GetGPUSwapchainTextureFormat(SDL_GPUDevice *, SDL_Window *);
bool SDL_WindowSupportsGPUSwapchainComposition(SDL_GPUDevice *, SDL_Window *, SDL_GPUSwapchainComposition);
bool SDL_WaitForGPUIdle(SDL_GPUDevice *);
SDL_GPUShader *SDL_CreateGPUShader(SDL_GPUDevice *, const SDL_GPUShaderCreateInfo *); void SDL_ReleaseGPUShader(SDL_GPUDevice *, SDL_GPUShader *);
SDL_GPUTexture *SDL_CreateGPUTexture(SDL_GPUDevice *, const SDL_GPUTextureCreateInfo *); void SDL_ReleaseGPUTexture(SDL_GPUDevice *, SDL_GPUTexture *);
bool SDL_GPUTextureSupportsFormat(SDL_GPUDevice *, SDL_GPUTextureFormat, SDL_GPUTextureType, SDL_GPUTextureUsageFlags);
SDL_GPUSampler *SDL_CreateGPUSampler(SDL_GPUDevice *, const SDL_GPUSamplerCreateInfo *); void SDL_ReleaseGPUSampler(SDL_GPUDevice *, SDL_GPUSampler *);
SDL_GPUTransferBuffer *SDL_CreateGPUTransferBuffer(SDL_GPUDevice *, const SDL_GPUTransferBufferCreateInfo *);
void SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *);
void *SDL_MapGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *, bool); void SDL_UnmapGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *);
SDL_GPUGraphicsPipeline *SDL_CreateGPUGraphicsPipeline(SDL_GPUDevice *, const SDL_GPUGraphicsPipelineCreateInfo *);
void SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice *, SDL_GPUGraphicsPipeline *);
SDL_GPUCommandBuffer *SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *); bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *);
SDL_GPUFence *SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *); bool SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *);
bool SDL_WaitForGPUFences(SDL_GPUDevice *, bool, SDL_GPUFence *const *, Uint32); void SDL_ReleaseGPUFence(SDL_GPUDevice *, SDL_GPUFence *);
bool SDL_WaitAndAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *, SDL_Window *, SDL_GPUTexture **, Uint32 *, Uint32 *);
SDL_GPUCopyPass *SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *); void SDL_EndGPUCopyPass(SDL_GPUCopyPass *);
void SDL_UploadToGPUTexture(SDL_GPUCopyPass *, const SDL_GPUTextureTransferInfo *, const SDL_GPUTextureRegion *, bool);
void SDL_DownloadFromGPUTexture(SDL_GPUCopyPass *, const SDL_GPUTextureRegion *, const SDL_GPUTextureTransferInfo *);
SDL_GPURenderPass *SDL_BeginGPURenderPass(SDL_GPUCommandBuffer *, const SDL_GPUColorTargetInfo *, Uint32, const SDL_GPUDepthStencilTargetInfo *);
void SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass *, SDL_GPUGraphicsPipeline *);
void SDL_BindGPUFragmentSamplers(SDL_GPURenderPass *, Uint32, const SDL_GPUTextureSamplerBinding *, Uint32);
void SDL_PushGPUFragmentUniformData(SDL_GPUCommandBuffer *, Uint32, const void *, Uint32);
void SDL_DrawGPUPrimitives(SDL_GPURenderPass *, Uint32, Uint32, Uint32, Uint32); void SDL_EndGPURenderPass(SDL_GPURenderPass *);

#endif /* SDL_STUB_H */
