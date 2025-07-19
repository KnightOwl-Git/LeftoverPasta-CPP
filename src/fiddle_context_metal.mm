#include "fiddle_context.hpp"

#include "rive/renderer/rive_renderer.hpp"

#include "rive/renderer/metal/render_context_metal_impl.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>

using namespace rive;
using namespace rive::gpu;

class FiddleContextMetalPLS : public FiddleContext
{
public:
    FiddleContextMetalPLS(FiddleContextOptions fiddleOptions) :
        m_fiddleOptions(fiddleOptions)
    {
        RenderContextMetalImpl::ContextOptions metalOptions;
        if (m_fiddleOptions.synchronousShaderCompilations)
        {
            // Turn on synchronous shader compilations to ensure deterministic
            // rendering and to make sure we test every unique shader.
            metalOptions.synchronousShaderCompilations = true;
        }
        if (m_fiddleOptions.disableRasterOrdering)
        {
            // Turn on synchronous shader compilations to ensure deterministic
            // rendering and to make sure we test every unique shader.
            metalOptions.disableFramebufferReads = true;
        }
        m_renderContext =
            RenderContextMetalImpl::MakeContext(m_gpu, metalOptions);
        printf("==== MTLDevice: %s ====\n", m_gpu.name.UTF8String);
    }

    float dpiScale(SDL_Window* window) const override
    {
        // Get the native NSWindow from SDL3 to access the backing scale factor
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        if (props) {
            NSWindow* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
            if (nsWindow) {
                float scale = nsWindow.backingScaleFactor;
                printf("Metal backend: Got backing scale factor: %f\n", scale);
                return scale;
            }
        }
        // Fallback to default scale factor
        printf("Metal backend: Using fallback scale factor: 1.0f\n");
        return 1.0f;
    }

    Factory* factory() override { return m_renderContext.get(); }

    rive::gpu::RenderContext* renderContextOrNull() override
    {
        return m_renderContext.get();
    }

    rive::gpu::RenderTarget* renderTargetOrNull() override
    {
        return m_renderTarget.get();
    }

    void onSizeChanged(SDL_Window* window,
                       int width,
                       int height,
                       uint32_t sampleCount) override
    {
        printf("Metal backend: Creating Metal layer for %dx%d\n", width, height);
        
        // Get the native NSWindow from SDL3
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        if (props) {
            NSWindow* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
            if (nsWindow) {
                printf("Metal backend: Got NSWindow from SDL3\n");
                NSView* view = [nsWindow contentView];
                view.wantsLayer = YES;

                m_swapchain = [CAMetalLayer layer];
                m_swapchain.device = m_gpu;
                m_swapchain.opaque = YES;
                m_swapchain.framebufferOnly = !m_fiddleOptions.enableReadPixels;
                m_swapchain.pixelFormat = MTLPixelFormatBGRA8Unorm;
                float scale = dpiScale(window);
                m_swapchain.contentsScale = scale;
                m_swapchain.displaySyncEnabled = NO;
                view.layer = m_swapchain;
                m_swapchain.drawableSize = CGSizeMake(width, height);

                auto renderContextImpl =
                    m_renderContext->static_impl_cast<RenderContextMetalImpl>();
                m_renderTarget = renderContextImpl->makeRenderTarget(
                    MTLPixelFormatBGRA8Unorm, width, height);
                m_pixelReadBuff = nil;
                
                printf("Metal backend: Metal layer created and attached to NSWindow successfully\n");
                return;
            } else {
                printf("Metal backend: Failed to get NSWindow from SDL3\n");
            }
        } else {
            printf("Metal backend: Failed to get window properties from SDL3\n");
        }
        
        // Fallback: create Metal layer without attaching to window
        printf("Metal backend: Using fallback - creating Metal layer without window attachment\n");
        m_swapchain = [CAMetalLayer layer];
        m_swapchain.device = m_gpu;
        m_swapchain.opaque = YES;
        m_swapchain.framebufferOnly = !m_fiddleOptions.enableReadPixels;
        m_swapchain.pixelFormat = MTLPixelFormatBGRA8Unorm;
        float scale = dpiScale(window);
        m_swapchain.contentsScale = scale;
        m_swapchain.displaySyncEnabled = NO;
        m_swapchain.drawableSize = CGSizeMake(width, height);

        auto renderContextImpl =
            m_renderContext->static_impl_cast<RenderContextMetalImpl>();
        m_renderTarget = renderContextImpl->makeRenderTarget(
            MTLPixelFormatBGRA8Unorm, width, height);
        m_pixelReadBuff = nil;
        
        printf("Metal backend: Metal layer created (fallback)\n");
    }

    void toggleZoomWindow() override {}

    std::unique_ptr<Renderer> makeRenderer(int width, int height) override
    {
        return std::make_unique<RiveRenderer>(m_renderContext.get());
    }

    void begin(const RenderContext::FrameDescriptor& frameDescriptor) override
    {
        m_renderContext->beginFrame(frameDescriptor);
    }

    void flushPLSContext(RenderTarget* offscreenRenderTarget) final
    {
        if (m_currentFrameSurface == nil)
        {
            m_currentFrameSurface = [m_swapchain nextDrawable];
            assert(m_currentFrameSurface.texture.width ==
                   m_renderTarget->width());
            assert(m_currentFrameSurface.texture.height ==
                   m_renderTarget->height());
            m_renderTarget->setTargetTexture(m_currentFrameSurface.texture);
        }

        id<MTLCommandBuffer> flushCommandBuffer = [m_queue commandBuffer];
        m_renderContext->flush({
            .renderTarget = offscreenRenderTarget != nullptr
                                ? offscreenRenderTarget
                                : m_renderTarget.get(),
            .externalCommandBuffer = (__bridge void*)flushCommandBuffer,
        });
        [flushCommandBuffer commit];
    }

    void end(SDL_Window* window, std::vector<uint8_t>* pixelData) final
    {
        flushPLSContext(nullptr);

        if (pixelData != nil)
        {
            // Read back pixels from the framebuffer!
            size_t w = m_renderTarget->width();
            size_t h = m_renderTarget->height();

            // Create a buffer to receive the pixels.
            if (m_pixelReadBuff == nil)
            {
                m_pixelReadBuff =
                    [m_gpu newBufferWithLength:h * w * 4
                                       options:MTLResourceStorageModeShared];
            }
            assert(m_pixelReadBuff.length == h * w * 4);

            id<MTLCommandBuffer> commandBuffer = [m_queue commandBuffer];
            id<MTLBlitCommandEncoder> blitEncoder =
                [commandBuffer blitCommandEncoder];

            // Blit the framebuffer into m_pixelReadBuff.
            [blitEncoder copyFromTexture:m_renderTarget->targetTexture()
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(w, h, 1)
                                toBuffer:m_pixelReadBuff
                       destinationOffset:0
                  destinationBytesPerRow:w * 4
                destinationBytesPerImage:h * w * 4];

            [blitEncoder endEncoding];
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];

            // Copy the image data from m_pixelReadBuff to pixelData.
            pixelData->resize(h * w * 4);
            const uint8_t* contents =
                reinterpret_cast<const uint8_t*>(m_pixelReadBuff.contents);
            const size_t rowBytes = w * 4;
            for (size_t y = 0; y < h; ++y)
            {
                // Flip Y.
                const uint8_t* src = &contents[(h - y - 1) * w * 4];
                uint8_t* dst = &(*pixelData)[y * w * 4];
                for (size_t x = 0; x < rowBytes; x += 4)
                {
                    // BGBRA -> RGBA.
                    dst[x + 0] = src[x + 2];
                    dst[x + 1] = src[x + 1];
                    dst[x + 2] = src[x + 0];
                    dst[x + 3] = src[x + 3];
                }
            }
        }

        id<MTLCommandBuffer> presentCommandBuffer = [m_queue commandBuffer];
        [presentCommandBuffer presentDrawable:m_currentFrameSurface];
        [presentCommandBuffer commit];

        m_currentFrameSurface = nil;
        m_renderTarget->setTargetTexture(nil);
    }

private:
    const FiddleContextOptions m_fiddleOptions;
    id<MTLDevice> m_gpu = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> m_queue = [m_gpu newCommandQueue];
    std::unique_ptr<RenderContext> m_renderContext;
    CAMetalLayer* m_swapchain;
    rcp<RenderTargetMetal> m_renderTarget;
    id<MTLBuffer> m_pixelReadBuff;
    id<CAMetalDrawable> m_currentFrameSurface = nil;
};

std::unique_ptr<FiddleContext> FiddleContext::MakeMetalPLS(
    FiddleContextOptions fiddleOptions)
{
    return std::make_unique<FiddleContextMetalPLS>(fiddleOptions);
}
