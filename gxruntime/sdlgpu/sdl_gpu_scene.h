#ifndef SDL_GPU_SCENE_H
#define SDL_GPU_SCENE_H

struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
struct SDL_GPUTexture;

namespace sdlgpu {

struct GpuMesh;

struct GpuSceneFrame {
	SDL_GPUDevice* dev = nullptr;
	SDL_GPUCommandBuffer* cmds = nullptr;
	SDL_GPURenderPass* pass = nullptr;

	SDL_GPUTexture* colorTarget = nullptr;
	SDL_GPUTexture* depthTarget = nullptr;
	unsigned width = 0, height = 0;

	bool active() const { return pass != nullptr; }
};

bool BeginSceneFrame(GpuSceneFrame& frame, SDL_GPUDevice* dev, unsigned w, unsigned h, float clearR, float clearG, float clearB);
void RenderSceneMesh(GpuSceneFrame& frame, GpuMesh* mesh, const float* viewProjTransposed, SDL_GPUTexture* tex, int first_vert, int vert_cnt, int first_tri, int tri_cnt);
void EndSceneFrame(GpuSceneFrame& frame);
void ReleaseSceneTargets(SDL_GPUDevice* dev, GpuSceneFrame& frame);

}

#endif
