#ifndef HZ_CRT_GPU_H
#define HZ_CRT_GPU_H

#include <SDL3/SDL.h>

#include "../core/crt.h"
#include "platform.h"

/* Brought over from the PS2 port (src/platform/crt_gpu.c there) line for
 * line: only the includes, the palette's size here (FB_INDICES below,
 * PS2's video.h's) and the shaders' loading (from the disk alone: HZ has
 * no pack) differ. The "(build N)" marks are PS2's builds. */

/* The frame's palette: CRAM's 64 words, then the Esc screen's 16. */
#define FB_INDICES (OVERLAY_BASE + OVERLAY_COLOURS)

/* (build 107) The CRT simulation's phase 2: the chain on SDL_GPU, in SDR;
 * (build 108) phase 3, the HDR swapchains' encodes (the plan's, and the
 * flat picture's). docs/crt.md, "Phase 2" and "Phase 3".
 *
 * The frame's palette indices and each index's three DAC levels go up as
 * textures every frame. With the CRT on, two passes draw it -- the beams
 * down into a float target 320 wide and as tall as the output, then across,
 * the mask and sRGB's curve into the target -- from tables crt.c works out
 * with the model's own functions (crt_tables_build), made again only when
 * the plan changes. With it off, one pass draws the picture as the
 * SDL_Renderer placeholder did (crt_sharp_layout).
 *
 * The shaders are src/shaders/ (HLSL), compiled offline by `make shaders`
 * (SDL_shadercross) into shaders/ as SPIR-V, DXIL and MSL; this reads the
 * one the device takes. Used by platform_sdl3.c and tools/crt_gpu_check.c. */

typedef struct CrtGpu CrtGpu;

/* The shaders from `dir` in the format the device takes, and a sampler.
 * NULL, with the reason on stderr, when a file is missing (before `make
 * shaders` has been run) or the device refuses one. */
CrtGpu *crt_gpu_create(SDL_GPUDevice *dev, const char *dir);

/* The three pipelines, for a colour target of `format` (the swapchain's:
 * 8-bit UNORM for SDR, R10G10B10A2 for HDR10, RGBA16 float for scRGB).
 * Called again whenever the swapchain's composition changes. */
bool crt_gpu_set_target(CrtGpu *g, SDL_GPUTextureFormat format);

void crt_gpu_destroy(CrtGpu *g);

/* "SPIR-V", "DXIL" or "MSL": the shaders it read. */
const char *crt_gpu_shader_format(const CrtGpu *g);

/* A copy pass on `cmd`, before any render pass: the frame (320 x 224
 * indices) and `palette`, FB_INDICES CRAM words -- the game's 64, then the
 * Escape menu's overlay line. With a plan (resolved), its tables when it
 * differs from the last one, and the down pass's target. False when the
 * plan's tables could not be made: draw the flat picture instead. */
bool crt_gpu_upload(CrtGpu *g, SDL_GPUCommandBuffer *cmd, const u8 *index,
                    const Cram *palette, const CrtPlan *plan);

/* The CRT, for the plan last uploaded, into the whole of `target` (the
 * plan's out_w x out_h). */
void crt_gpu_draw_crt(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                      SDL_GPUTexture *target);

/* The flat picture, where `l` says, into the whole of `target`, encoded
 * as `enc` says (build 108; NULL: the SDR swapchain's codes, as 107). */
void crt_gpu_draw_sharp(CrtGpu *g, SDL_GPUCommandBuffer *cmd,
                        SDL_GPUTexture *target, const CrtSharp *l,
                        const CrtEncodeParams *enc);

#endif /* HZ_CRT_GPU_H */
