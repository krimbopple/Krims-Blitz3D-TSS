#include "sdl_gpu_scene.h"
#include "sdl_gpu_mesh.h"
#include "sdl_gpu_pipeline.h"
#include "sdl_gpu_texture.h"

#include "../std.h"

#include <SDL3/SDL_gpu.h>

namespace sdlgpu {

static void ReleaseTargetsLocked(SDL_GPUDevice* dev, GpuSceneFrame& frame) {
	if (frame.colorTarget) { SDL_ReleaseGPUTexture(dev, frame.colorTarget); frame.colorTarget = nullptr; }
	if (frame.depthTarget) { SDL_ReleaseGPUTexture(dev, frame.depthTarget); frame.depthTarget = nullptr; }
	frame.width = frame.height = 0;
}

void ReleaseSceneTargets(SDL_GPUDevice* dev, GpuSceneFrame& frame) {
	if (!dev) return;
	ReleaseTargetsLocked(dev, frame);
}

bool BeginSceneFrame(GpuSceneFrame& frame, SDL_GPUDevice* dev, unsigned w, unsigned h, float clearR, float clearG, float clearB) {
	if (!dev || !w || !h) return false;

	if (frame.dev != dev || frame.width != w || frame.height != h || !frame.colorTarget || !frame.depthTarget) {
		ReleaseTargetsLocked(dev, frame);
		frame.colorTarget = CreateColorTarget(dev, w, h);
		frame.depthTarget = CreateDepthTarget(dev, w, h, MeshDepthFormat(dev));
		if (!frame.colorTarget || !frame.depthTarget) {
			ReleaseTargetsLocked(dev, frame);
			return false;
		}
		frame.dev = dev;
		frame.width = w;
		frame.height = h;
	}

	frame.cmds = SDL_AcquireGPUCommandBuffer(dev);
	if (!frame.cmds) return false;

	SDL_GPUColorTargetInfo colorInfo{};
	colorInfo.texture = frame.colorTarget;
	colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorInfo.store_op = SDL_GPU_STOREOP_STORE;
	colorInfo.clear_color = SDL_FColor{ clearR, clearG, clearB, 1.0f };

	SDL_GPUDepthStencilTargetInfo depthInfo{};
	depthInfo.texture = frame.depthTarget;
	depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	depthInfo.store_op = SDL_GPU_STOREOP_STORE;
	depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depthInfo.clear_depth = 1.0f;
	depthInfo.clear_stencil = 0;

	frame.pass = SDL_BeginGPURenderPass(frame.cmds, &colorInfo, 1, &depthInfo);
	if (!frame.pass) {
		SDL_CancelGPUCommandBuffer(frame.cmds);
		frame.cmds = nullptr;
		return false;
	}
	return true;
}

void RenderSceneMesh(GpuSceneFrame& frame, GpuMesh* mesh, const float* viewProjTransposed, SDL_GPUTexture* tex, int first_vert, int vert_cnt, int first_tri, int tri_cnt) {
	(void)vert_cnt;
	if (!frame.active() || !mesh || !viewProjTransposed || tri_cnt <= 0) return;

	unsigned indexCount = (unsigned)tri_cnt * 3;
	unsigned startIndex = (unsigned)first_tri * 3;
	DrawMesh(frame.dev, nullptr, frame.cmds, frame.pass, mesh, viewProjTransposed, tex, indexCount, startIndex, first_vert, SceneColorFormat(), MeshDepthFormat(frame.dev));
}

void EndSceneFrame(GpuSceneFrame& frame) {
	if (frame.pass) {
		SDL_EndGPURenderPass(frame.pass);
		frame.pass = nullptr;
	}
	if (frame.cmds) {
		SDL_SubmitGPUCommandBuffer(frame.cmds);
		frame.cmds = nullptr;
	}
}

}
