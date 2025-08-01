#pragma once

#include <vector>

#include "rive/renderer/render_context.hpp"

struct SDL_Window;

namespace rive::gpu
{
class RenderContextGLImpl;
class RenderContextVulkanImpl;
} // namespace rive::gpu

struct FiddleContextOptions
{
    bool retinaDisplay = true;
    bool synchronousShaderCompilations = false;
    bool enableReadPixels = false;
    bool disableRasterOrdering = false;
    bool coreFeaturesOnly = false;
    bool srgb = false;
    // request d3d12 to use software d3d11 driver
    bool d3d12UseWarpDevice = false;
    // Allow rendering to a texture instead of an OS window. (Speeds up the
    // execution of goldens & gms significantly on Vulkan/Windows.)
    bool allowHeadlessRendering = false;
    bool enableVulkanValidationLayers = false;
    bool disableDebugCallbacks = false;
    const char* gpuNameFilter = nullptr; // Substring of GPU name to use.
};

class FiddleContext
{
public:
    virtual ~FiddleContext() {}
    virtual float dpiScale(SDL_Window*) const = 0;
    virtual rive::Factory* factory() = 0;
    virtual rive::gpu::RenderContext* renderContextOrNull() = 0;
    virtual rive::gpu::RenderContextGLImpl* renderContextGLImpl() const
    {
        return nullptr;
    }
    virtual rive::gpu::RenderContextVulkanImpl* renderContextVulkanImpl() const
    {
        return nullptr;
    }
    virtual rive::gpu::RenderTarget* renderTargetOrNull() = 0;

    virtual void onSizeChanged(SDL_Window*,
                               int width,
                               int height,
                               uint32_t sampleCount)
    {}
    virtual void toggleZoomWindow() = 0;
    virtual std::unique_ptr<rive::Renderer> makeRenderer(int width,
                                                         int height) = 0;
    virtual void begin(const rive::gpu::RenderContext::FrameDescriptor&) = 0;
    // Called by end().
    virtual void flushPLSContext(
        rive::gpu::RenderTarget* offscreenRenderTarget = nullptr) = 0;
    virtual void end(SDL_Window*,
                     std::vector<uint8_t>* pixelData = nullptr) = 0;
    virtual void tick(){};
    virtual void hotloadShaders(){};

    static std::unique_ptr<FiddleContext> MakeGLPLS(FiddleContextOptions = {});
    static std::unique_ptr<FiddleContext> MakeGLSkia();
#ifdef RIVE_MACOSX
    static std::unique_ptr<FiddleContext> MakeMetalPLS(
        FiddleContextOptions options);
#else
    static std::unique_ptr<FiddleContext> MakeMetalPLS(
        FiddleContextOptions options);
#endif
    static std::unique_ptr<FiddleContext> MakeD3DPLS(FiddleContextOptions = {});
    static std::unique_ptr<FiddleContext> MakeD3D12PLS(
        FiddleContextOptions = {});
    static std::unique_ptr<FiddleContext> MakeVulkanPLS(
        FiddleContextOptions = {});
    static std::unique_ptr<FiddleContext> MakeDawnPLS(
        FiddleContextOptions = {});
};
