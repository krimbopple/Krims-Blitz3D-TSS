#include "sdl_gpu_pipeline.h"
#include "sdl_gpu_mesh.h"

#include "../std.h"

#include <cstring>

#include <SDL3/SDL_gpu.h>

#include "shaders/blit_shaders.h"
#include "shaders/mesh_shaders.h"

namespace sdlgpu {

	namespace {
		SDL_GPUDevice* g_blitDev = nullptr;
		SDL_GPUTextureFormat g_blitFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUGraphicsPipeline* g_blitPipe = nullptr;
		SDL_GPUSampler* g_blitSamp = nullptr;
		SDL_GPUTexture* g_blitTex = nullptr;
		unsigned g_blitW = 0, g_blitH = 0;
	}

	static void TeardownBlit();

	static SDL_GPUShader* LoadShader(SDL_GPUDevice* dev, SDL_GPUShaderFormat fmt, SDL_GPUShaderStage stage, const char* entry, const uint8_t* code, size_t size, unsigned samplers = 0) {
		SDL_GPUShaderCreateInfo info{};
		info.code = code;
		info.code_size = size;
		info.entrypoint = entry;
		info.format = fmt;
		info.stage = stage;
		info.num_samplers = samplers;
		return SDL_CreateGPUShader(dev, &info);
	}

	static void TeardownBlit() {
		if (g_blitPipe && g_blitDev) SDL_ReleaseGPUGraphicsPipeline(g_blitDev, g_blitPipe);
		if (g_blitSamp && g_blitDev) SDL_ReleaseGPUSampler(g_blitDev, g_blitSamp);
		if (g_blitTex && g_blitDev) SDL_ReleaseGPUTexture(g_blitDev, g_blitTex);
		g_blitPipe = nullptr;
		g_blitSamp = nullptr;
		g_blitTex = nullptr;
		g_blitDev = nullptr;
		g_blitFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		g_blitW = g_blitH = 0;
	}

	static bool EnsureBlit(SDL_GPUDevice* dev, SDL_Window* win, unsigned w, unsigned h) {
		SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);
		if (g_blitPipe && g_blitDev == dev && g_blitFormat == fmt && g_blitTex && g_blitW == w && g_blitH == h) return true;
		if (!g_blitPipe || g_blitDev != dev || g_blitFormat != fmt) {
			TeardownBlit();
			SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(dev);
			const uint8_t* vsCode = nullptr;
			const uint8_t* psCode = nullptr;
			size_t vsSize = 0, psSize = 0;
			SDL_GPUShaderFormat useFmt = SDL_GPU_SHADERFORMAT_INVALID;
			if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
				useFmt = SDL_GPU_SHADERFORMAT_SPIRV;
				vsCode = kBlitVS_SPIRV; vsSize = kBlitVS_SPIRV_size;
				psCode = kBlitPS_SPIRV; psSize = kBlitPS_SPIRV_size;
			}
			else if (supported & SDL_GPU_SHADERFORMAT_DXIL) {
				useFmt = SDL_GPU_SHADERFORMAT_DXIL;
				vsCode = kBlitVS_DXIL; vsSize = kBlitVS_DXIL_size;
				psCode = kBlitPS_DXIL; psSize = kBlitPS_DXIL_size;
			}
			if (useFmt == SDL_GPU_SHADERFORMAT_INVALID) return false;

			SDL_GPUShader* vs = LoadShader(dev, useFmt, SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", vsCode, vsSize);
			if (!vs) return false;
			SDL_GPUShader* ps = LoadShader(dev, useFmt, SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", psCode, psSize, 1);
			if (!ps) { SDL_ReleaseGPUShader(dev, vs); return false; }

			SDL_GPUColorTargetDescription target{};
			target.format = fmt;

			SDL_GPUGraphicsPipelineCreateInfo info{};
			info.vertex_shader = vs;
			info.fragment_shader = ps;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
			info.target_info.num_color_targets = 1;
			info.target_info.color_target_descriptions = &target;

			g_blitPipe = SDL_CreateGPUGraphicsPipeline(dev, &info);
			SDL_ReleaseGPUShader(dev, vs);
			SDL_ReleaseGPUShader(dev, ps);
			if (!g_blitPipe) return false;

			SDL_GPUSamplerCreateInfo samp{};
			samp.min_filter = SDL_GPU_FILTER_LINEAR;
			samp.mag_filter = SDL_GPU_FILTER_LINEAR;
			samp.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
			samp.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
			g_blitSamp = SDL_CreateGPUSampler(dev, &samp);
			if (!g_blitSamp) { TeardownBlit(); return false; }

			g_blitDev = dev;
			g_blitFormat = fmt;
		}
		if (!g_blitTex || g_blitW != w || g_blitH != h) {
			if (g_blitTex) SDL_ReleaseGPUTexture(g_blitDev, g_blitTex);
			SDL_GPUTextureCreateInfo texInfo{};
			texInfo.type = SDL_GPU_TEXTURETYPE_2D;
			texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
			texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
			texInfo.width = w;
			texInfo.height = h;
			texInfo.layer_count_or_depth = 1;
			texInfo.num_levels = 1;
			g_blitTex = SDL_CreateGPUTexture(dev, &texInfo);
			if (!g_blitTex) return false;
			g_blitW = w;
			g_blitH = h;
		}
		return true;
	}

	bool PresentBlit(SDL_GPUDevice* dev, SDL_Window* win, float r, float g, float b, unsigned w, unsigned h, const void* px) {
		if (!dev || !win || !w || !h) return false;
		if (!EnsureBlit(dev, win, w, h)) return false;

		SDL_GPUCommandBuffer* cmds = SDL_AcquireGPUCommandBuffer(dev);
		if (!cmds) return false;

		SDL_GPUTransferBuffer* buf = nullptr;
		if (px) {
			Uint32 size = w * h * 4;
			SDL_GPUTransferBufferCreateInfo bufInfo{};
			bufInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
			bufInfo.size = size;
			buf = SDL_CreateGPUTransferBuffer(dev, &bufInfo);
			if (!buf) { SDL_CancelGPUCommandBuffer(cmds); return false; }
			void* dst = SDL_MapGPUTransferBuffer(dev, buf, false);
			if (!dst) {
				SDL_ReleaseGPUTransferBuffer(dev, buf);
				SDL_CancelGPUCommandBuffer(cmds);
				return false;
			}
			memcpy(dst, px, size);
			SDL_UnmapGPUTransferBuffer(dev, buf);

			SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmds);
			SDL_GPUTextureTransferInfo src{};
			src.transfer_buffer = buf;
			src.pixels_per_row = w;
			src.rows_per_layer = h;
			SDL_GPUTextureRegion reg{};
			reg.texture = g_blitTex;
			reg.w = w;
			reg.h = h;
			reg.d = 1;
			SDL_UploadToGPUTexture(copy, &src, &reg, false);
			SDL_EndGPUCopyPass(copy);
		}

		SDL_GPUTexture* tex = nullptr;
		Uint32 sw = 0, sh = 0;
		if (!SDL_AcquireGPUSwapchainTexture(cmds, win, &tex, &sw, &sh)) {
			SDL_CancelGPUCommandBuffer(cmds);
			if (buf) SDL_ReleaseGPUTransferBuffer(dev, buf);
			return false;
		}
		if (tex) {
			SDL_GPUColorTargetInfo target{};
			target.texture = tex;
			target.load_op = SDL_GPU_LOADOP_CLEAR;
			target.store_op = SDL_GPU_STOREOP_STORE;
			target.clear_color = SDL_FColor{ r, g, b, 1.0f };
			SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmds, &target, 1, nullptr);
			SDL_BindGPUGraphicsPipeline(pass, g_blitPipe);
			SDL_GPUTextureSamplerBinding bind{};
			bind.texture = g_blitTex;
			bind.sampler = g_blitSamp;
			SDL_BindGPUFragmentSamplers(pass, 0, &bind, 1);
			SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
			SDL_EndGPURenderPass(pass);
		}
		bool ok = SDL_SubmitGPUCommandBuffer(cmds);
		if (buf) SDL_ReleaseGPUTransferBuffer(dev, buf);
		return ok;
	}

	namespace {
		SDL_GPUDevice* g_meshDev = nullptr;
		SDL_GPUTextureFormat g_meshFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUTextureFormat g_meshDepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUGraphicsPipeline* g_meshPipe = nullptr;
		SDL_GPUSampler* g_meshSamp = nullptr;
		unsigned g_meshStride = 0;
		SDL_GPUDevice* g_whiteDev = nullptr;
		SDL_GPUTexture* g_whiteTex = nullptr;
	}

	static void TeardownMeshPipe() {
		if (g_meshPipe && g_meshDev) SDL_ReleaseGPUGraphicsPipeline(g_meshDev, g_meshPipe);
		if (g_meshSamp && g_meshDev) SDL_ReleaseGPUSampler(g_meshDev, g_meshSamp);
		g_meshPipe = nullptr;
		g_meshSamp = nullptr;
		g_meshDev = nullptr;
		g_meshFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		g_meshDepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		g_meshStride = 0;
	}

	static void TeardownWhiteTexture() {
		if (g_whiteTex && g_whiteDev) SDL_ReleaseGPUTexture(g_whiteDev, g_whiteTex);
		g_whiteTex = nullptr;
		g_whiteDev = nullptr;
	}

	static SDL_GPUTextureFormat PickMeshDepthFormat(SDL_GPUDevice* dev) {
		if (SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
			return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
		if (SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
			return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
		return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	}

	int MeshDepthFormat(SDL_GPUDevice* dev) {
		return (int)PickMeshDepthFormat(dev);
	}

	int SceneColorFormat() {
		return (int)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	}

	static SDL_GPUTexture* EnsureWhiteTexture(SDL_GPUDevice* dev) {
		if (g_whiteTex && g_whiteDev == dev) return g_whiteTex;
		TeardownWhiteTexture();
		SDL_GPUTextureCreateInfo info{};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = 1;
		info.height = 1;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &info);
		if (!tex) return nullptr;

		unsigned char white[4] = { 255, 255, 255, 255 };
		SDL_GPUTransferBufferCreateInfo bufInfo{};
		bufInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		bufInfo.size = 4;
		SDL_GPUTransferBuffer* buf = SDL_CreateGPUTransferBuffer(dev, &bufInfo);
		if (!buf) { SDL_ReleaseGPUTexture(dev, tex); return nullptr; }
		void* dst = SDL_MapGPUTransferBuffer(dev, buf, false);
		if (!dst) { SDL_ReleaseGPUTransferBuffer(dev, buf); SDL_ReleaseGPUTexture(dev, tex); return nullptr; }
		memcpy(dst, white, 4);
		SDL_UnmapGPUTransferBuffer(dev, buf);

		SDL_GPUCommandBuffer* cmds = SDL_AcquireGPUCommandBuffer(dev);
		if (!cmds) { SDL_ReleaseGPUTransferBuffer(dev, buf); SDL_ReleaseGPUTexture(dev, tex); return nullptr; }
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmds);
		SDL_GPUTextureTransferInfo src{};
		src.transfer_buffer = buf;
		src.pixels_per_row = 1;
		src.rows_per_layer = 1;
		SDL_GPUTextureRegion reg{};
		reg.texture = tex;
		reg.w = 1;
		reg.h = 1;
		reg.d = 1;
		SDL_UploadToGPUTexture(copy, &src, &reg, false);
		SDL_EndGPUCopyPass(copy);
		bool ok = SDL_SubmitGPUCommandBuffer(cmds);
		SDL_ReleaseGPUTransferBuffer(dev, buf);
		if (!ok) { SDL_ReleaseGPUTexture(dev, tex); return nullptr; }

		g_whiteDev = dev;
		g_whiteTex = tex;
		return g_whiteTex;
	}

	static bool EnsureMeshPipe(SDL_GPUDevice* dev, SDL_Window* win, unsigned stride, int colorFormatOverride, int depthFormatOverride) {
		SDL_GPUTextureFormat fmt = colorFormatOverride ? (SDL_GPUTextureFormat)colorFormatOverride : SDL_GetGPUSwapchainTextureFormat(dev, win);
		SDL_GPUTextureFormat depthFmt = depthFormatOverride ? (SDL_GPUTextureFormat)depthFormatOverride : PickMeshDepthFormat(dev);
		if (g_meshPipe && g_meshDev == dev && g_meshFormat == fmt && g_meshDepthFormat == depthFmt && g_meshStride == stride) return true;
		TeardownMeshPipe();

		SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(dev);
		const uint8_t* vsCode = nullptr;
		const uint8_t* psCode = nullptr;
		size_t vsSize = 0, psSize = 0;
		SDL_GPUShaderFormat useFmt = SDL_GPU_SHADERFORMAT_INVALID;
		if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
			useFmt = SDL_GPU_SHADERFORMAT_SPIRV;
			vsCode = kMeshVS_SPIRV; vsSize = kMeshVS_SPIRV_size;
			psCode = kMeshPS_SPIRV; psSize = kMeshPS_SPIRV_size;
		}
		else if (supported & SDL_GPU_SHADERFORMAT_DXIL) {
			useFmt = SDL_GPU_SHADERFORMAT_DXIL;
			vsCode = kMeshVS_DXIL; vsSize = kMeshVS_DXIL_size;
			psCode = kMeshPS_DXIL; psSize = kMeshPS_DXIL_size;
		}
		if (useFmt == SDL_GPU_SHADERFORMAT_INVALID) return false;

		SDL_GPUShader* vs = LoadShader(dev, useFmt, SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", vsCode, vsSize, 0);
		if (!vs) return false;
		SDL_GPUShader* ps = LoadShader(dev, useFmt, SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", psCode, psSize, 1);
		if (!ps) { SDL_ReleaseGPUShader(dev, vs); return false; }

		SDL_GPUVertexBufferDescription vb{};
		vb.slot = 0;
		vb.pitch = stride;
		vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUVertexAttribute attrs[4]{};
		attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
		attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
		attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[2].offset = 24;
		attrs[3].location = 3; attrs[3].buffer_slot = 0; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[3].offset = 28;
		SDL_GPUVertexInputState vin{};
		vin.vertex_buffer_descriptions = &vb;
		vin.num_vertex_buffers = 1;
		vin.vertex_attributes = attrs;
		vin.num_vertex_attributes = 4;

		SDL_GPUColorTargetDescription target{};
		target.format = fmt;

		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vs;
		info.fragment_shader = ps;
		info.vertex_input_state = vin;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

		info.depth_stencil_state.enable_depth_test = true;
		info.depth_stencil_state.enable_depth_write = true;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		info.depth_stencil_state.enable_stencil_test = false;

		info.target_info.num_color_targets = 1;
		info.target_info.color_target_descriptions = &target;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = depthFmt;

		g_meshPipe = SDL_CreateGPUGraphicsPipeline(dev, &info);
		SDL_ReleaseGPUShader(dev, vs);
		SDL_ReleaseGPUShader(dev, ps);
		if (!g_meshPipe) return false;

		SDL_GPUSamplerCreateInfo samp{};
		samp.min_filter = SDL_GPU_FILTER_LINEAR;
		samp.mag_filter = SDL_GPU_FILTER_LINEAR;
		samp.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		samp.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		g_meshSamp = SDL_CreateGPUSampler(dev, &samp);
		if (!g_meshSamp) { TeardownMeshPipe(); return false; }

		g_meshDev = dev;
		g_meshFormat = fmt;
		g_meshDepthFormat = depthFmt;
		g_meshStride = stride;
		return true;
	}

	void DrawMesh(SDL_GPUDevice* dev, SDL_Window* win, SDL_GPUCommandBuffer* cmds, SDL_GPURenderPass* pass, GpuMesh* mesh, const float* viewProj, SDL_GPUTexture* tex, unsigned indexCount, unsigned startIndex, int firstVertex, int colorFormat, int depthFormat) {
		if (!dev || !cmds || !pass || !mesh || !viewProj || !indexCount) return;
		if (!colorFormat && !win) return;
		if (!EnsureMeshPipe(dev, win, mesh->vertStride, colorFormat, depthFormat)) return;

		SDL_GPUTexture* boundTex = tex;
		if (!boundTex) {
			boundTex = EnsureWhiteTexture(dev);
			if (!boundTex) return;
		}

		SDL_PushGPUVertexUniformData(cmds, 0, viewProj, 64);
		SDL_BindGPUGraphicsPipeline(pass, g_meshPipe);
		SDL_GPUBufferBinding vb{};
		vb.buffer = mesh->verts;
		SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
		SDL_GPUBufferBinding ib{};
		ib.buffer = mesh->indices;
		SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
		SDL_GPUTextureSamplerBinding bind{};
		bind.texture = boundTex;
		bind.sampler = g_meshSamp;
		SDL_BindGPUFragmentSamplers(pass, 0, &bind, 1);
		SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, startIndex, firstVertex, 0);
	}

	void TeardownPipelines() {
		TeardownBlit();
		TeardownMeshPipe();
		TeardownWhiteTexture();
	}

}