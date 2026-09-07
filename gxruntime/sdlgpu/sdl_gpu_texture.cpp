#include "sdl_gpu_texture.h"

#include "../std.h"

#include <cstring>

#include <SDL3/SDL_gpu.h>

namespace sdlgpu {

SDL_GPUTexture* CreateTexture2D(SDL_GPUDevice* dev, unsigned w, unsigned h) {
	if (!dev || !w || !h) return nullptr;
	SDL_GPUTextureCreateInfo info{};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = w;
	info.height = h;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	return SDL_CreateGPUTexture(dev, &info);
}

bool UploadTextureRGBA(SDL_GPUDevice* dev, SDL_GPUTexture* tex, unsigned w, unsigned h, const void* px) {
	if (!dev || !tex || !w || !h || !px) return false;
	Uint32 size = w * h * 4;

	SDL_GPUTransferBufferCreateInfo bufInfo{};
	bufInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	bufInfo.size = size;
	SDL_GPUTransferBuffer* buf = SDL_CreateGPUTransferBuffer(dev, &bufInfo);
	if (!buf) return false;

	void* dst = SDL_MapGPUTransferBuffer(dev, buf, false);
	if (!dst) { SDL_ReleaseGPUTransferBuffer(dev, buf); return false; }
	memcpy(dst, px, size);
	SDL_UnmapGPUTransferBuffer(dev, buf);

	SDL_GPUCommandBuffer* cmds = SDL_AcquireGPUCommandBuffer(dev);
	if (!cmds) { SDL_ReleaseGPUTransferBuffer(dev, buf); return false; }
	SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmds);

	SDL_GPUTextureTransferInfo src{};
	src.transfer_buffer = buf;
	src.pixels_per_row = w;
	src.rows_per_layer = h;

	SDL_GPUTextureRegion dstReg{};
	dstReg.texture = tex;
	dstReg.w = w;
	dstReg.h = h;
	dstReg.d = 1;

	SDL_UploadToGPUTexture(pass, &src, &dstReg, false);
	SDL_EndGPUCopyPass(pass);
	bool ok = SDL_SubmitGPUCommandBuffer(cmds);
	SDL_ReleaseGPUTransferBuffer(dev, buf);
	return ok;
}

void ReleaseTexture(SDL_GPUDevice* dev, SDL_GPUTexture* tex) {
	if (!dev || !tex) return;
	SDL_ReleaseGPUTexture(dev, tex);
}

SDL_GPUTexture* CreateColorTarget(SDL_GPUDevice* dev, unsigned w, unsigned h) {
	if (!dev || !w || !h) return nullptr;
	SDL_GPUTextureCreateInfo info{};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = w;
	info.height = h;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	return SDL_CreateGPUTexture(dev, &info);
}

SDL_GPUTexture* CreateDepthTarget(SDL_GPUDevice* dev, unsigned w, unsigned h, int formatValue) {
	if (!dev || !w || !h) return nullptr;
	SDL_GPUTextureCreateInfo info{};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)formatValue;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	info.width = w;
	info.height = h;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	return SDL_CreateGPUTexture(dev, &info);
}

}
