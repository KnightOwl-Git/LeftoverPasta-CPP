#include "fiddle_context.hpp"

#ifdef RIVE_TOOLS_NO_GLFW

std::unique_ptr<FiddleContext> FiddleContext::MakeGLPLS(FiddleContextOptions)
{
    return nullptr;
}

#else

#include "path_fiddle.hpp"
#include "rive/renderer/rive_renderer.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"
#include "rive/renderer/gl/render_target_gl.hpp"

#ifdef RIVE_WEBGL
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <SDL3/SDL.h>

using namespace rive;
using namespace rive::gpu;

#ifdef RIVE_DESKTOP_GL
#ifdef DEBUG
static void GLAPIENTRY err_msg_callback(GLenum source,
                                        GLenum type,
                                        GLuint id,
                                        GLenum severity,
                                        GLsizei length,
                                        const GLchar* message,
                                        const void* userParam)
{
    if (type == GL_DEBUG_TYPE_ERROR)
    {
        printf("GL ERROR: %s\n", message);
        fflush(stdout);
        // Don't abort if it's a shader compile error; let our internal handlers
        // print the source (for debugging) and exit on their own.
        if (!strstr(message, "SHADER_ID_COMPILE error has been generated"))
        {
            assert(0);
        }
    }
    else if (type == GL_DEBUG_TYPE_PERFORMANCE)
    {
        if (strstr(
                message,
                "API_ID_REDUNDANT_FBO performance warning has been generated. "
                "Redundant state change in glBindFramebuffer API call, FBO"))
        {
            return;
        }
        if (strstr(message, "is being recompiled based on GL state") ||
            strstr(message, "shader recompiled due to state change"))
        {
            return;
        }
        if (strcmp(message,
                   "Pixel-path performance warning: Pixel transfer is "
                   "synchronized with 3D rendering.") == 0)
        {
            return;
        }
        printf("GL PERF: %s\n", message);
        fflush(stdout);
    }
}
#endif
#endif

class FiddleContextGLBase : public FiddleContext
{
public:
    FiddleContextGLBase() {
        printf("FiddleContextGLBase: Constructor start\n");
    }
    ~FiddleContextGLBase() override
    {
        glDeleteFramebuffers(1, &m_zoomWindowFBO);
    }

    float dpiScale(SDL_Window*) const override
    {
#if defined(__APPLE__) || defined(RIVE_WEBGL)
        return 2;
#else
        return 1;
#endif
    }

    void toggleZoomWindow() override
    {
        if (m_zoomWindowFBO)
        {
            glDeleteFramebuffers(1, &m_zoomWindowFBO);
            m_zoomWindowFBO = 0;
        }
        else
        {
            GLuint tex;
            glGenTextures(1, &tex);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexStorage2D(GL_TEXTURE_2D,
                           1,
                           GL_RGB8,
                           kZoomWindowWidth,
                           kZoomWindowHeight);

            glGenFramebuffers(1, &m_zoomWindowFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, m_zoomWindowFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   tex,
                                   0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glDeleteTextures(1, &tex);
        }
    }

    void end(SDL_Window* window, std::vector<uint8_t>* pixelData) final
    {
        onEnd(pixelData);
        if (m_zoomWindowFBO)
        {
            // Blit the zoom window.
            float xd, yd;
            SDL_GetMouseState(&xd, &yd);
            xd *= dpiScale(window);
            yd *= dpiScale(window);
            int width = 0, height = 0;
            SDL_GetWindowSize(window, &width, &height);
            int x = (int)xd, y = height - (int)yd;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_zoomWindowFBO);
            glBlitFramebuffer(x - kZoomWindowWidth / 2,
                              y - kZoomWindowHeight / 2,
                              x + kZoomWindowWidth / 2,
                              y + kZoomWindowHeight / 2,
                              0,
                              0,
                              kZoomWindowWidth,
                              kZoomWindowHeight,
                              GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glEnable(GL_SCISSOR_TEST);
            glScissor(0,
                      0,
                      kZoomWindowWidth * kZoomWindowScale + 2,
                      kZoomWindowHeight * kZoomWindowScale + 2);
            glClearColor(.6f, .6f, .6f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_zoomWindowFBO);
            glBlitFramebuffer(0,
                              0,
                              kZoomWindowWidth,
                              kZoomWindowHeight,
                              0,
                              0,
                              kZoomWindowWidth * kZoomWindowScale,
                              kZoomWindowHeight * kZoomWindowScale,
                              GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
            glDisable(GL_SCISSOR_TEST);
        }
    }

protected:
    virtual void onEnd(std::vector<uint8_t>* pixelData) = 0;

private:
    GLuint m_zoomWindowFBO = 0;
};

class FiddleContextGL : public FiddleContextGLBase
{
public:
    FiddleContextGL(FiddleContextOptions options)
    {
        printf("FiddleContextGL: Constructor start\n");
#ifdef RIVE_DESKTOP_GL
        printf("FiddleContextGL: Starting GLAD initialization...\n");
        
        // Test if SDL_GL_GetProcAddress is working
        SDL_FunctionPointer testFunc = SDL_GL_GetProcAddress("glGetString");
        printf("FiddleContextGL: glGetString function pointer: %p\n", (void*)testFunc);
        
        // Try to call a basic OpenGL function to ensure context is ready
        typedef const GLubyte* (*glGetStringFunc)(GLenum);
        glGetStringFunc glGetStringPtr = (glGetStringFunc)testFunc;
        if (glGetStringPtr) {
            const GLubyte* version = glGetStringPtr(GL_VERSION);
            printf("FiddleContextGL: OpenGL version: %s\n", version ? (const char*)version : "NULL");
        }
        
        // Load the OpenGL API using glad.
        printf("FiddleContextGL: About to call gladLoadCustomLoader...\n");
        if (!gladLoadCustomLoader((GLADloadproc)SDL_GL_GetProcAddress))
        {
            fprintf(stderr, "Failed to initialize glad.\n");
            abort();
        }
        printf("FiddleContextGL: GLAD initialization successful\n");
#endif
        printf("FiddleContextGL: Constructor end\n");

        printf("==== GL GPU: %s ====\n", glGetString(GL_RENDERER));
#if 0
        int n;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (size_t i = 0; i < n; ++i)
        {
            printf("  %s\n", glGetStringi(GL_EXTENSIONS, i));
        }
#endif

#ifdef RIVE_DESKTOP_GL
#ifdef DEBUG
        if (GLAD_GL_KHR_debug)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glDebugMessageControlKHR(GL_DONT_CARE,
                                     GL_DONT_CARE,
                                     GL_DONT_CARE,
                                     0,
                                     NULL,
                                     GL_TRUE);
            glDebugMessageCallbackKHR(&err_msg_callback, nullptr);
        }
#endif
#endif

        m_renderContext = RenderContextGLImpl::MakeContext({
            .disableFragmentShaderInterlock = options.disableRasterOrdering,
        });
        if (!m_renderContext)
        {
            fprintf(stderr, "Failed to create a RiveRenderContext for GL.\n");
            abort();
        }
    }

    rive::Factory* factory() final { return m_renderContext.get(); }

    RenderContext* renderContextOrNull() final { return m_renderContext.get(); }

    RenderContextGLImpl* renderContextGLImpl() const final
    {
        return m_renderContext->static_impl_cast<RenderContextGLImpl>();
    }

    RenderTarget* renderTargetOrNull() final { return m_renderTarget.get(); }

    void onSizeChanged(SDL_Window* window,
                       int width,
                       int height,
                       uint32_t sampleCount) final
    {
        m_renderTarget =
            make_rcp<FramebufferRenderTargetGL>(width, height, 0, sampleCount);
        glViewport(0, 0, width, height);
    }

    std::unique_ptr<Renderer> makeRenderer(int width, int height) final
    {
        return std::make_unique<RiveRenderer>(m_renderContext.get());
    }

    void begin(const RenderContext::FrameDescriptor& frameDescriptor) final
    {
        renderContextGLImpl()->invalidateGLState();
        m_renderContext->beginFrame(frameDescriptor);
    }

    void flushPLSContext(RenderTarget* offscreenRenderTarget) final
    {
        m_renderContext->flush({
            .renderTarget = offscreenRenderTarget != nullptr
                                ? offscreenRenderTarget
                                : m_renderTarget.get(),
        });
    }

    void onEnd(std::vector<uint8_t>* pixelData) final
    {
        flushPLSContext(nullptr);
        renderContextGLImpl()->unbindGLInternalResources();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (pixelData)
        {
            pixelData->resize(m_renderTarget->height() *
                              m_renderTarget->width() * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadPixels(0,
                         0,
                         m_renderTarget->width(),
                         m_renderTarget->height(),
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         pixelData->data());
        }
    }

private:
    std::unique_ptr<RenderContext> m_renderContext;
    rcp<RenderTargetGL> m_renderTarget;
};

std::unique_ptr<FiddleContext> FiddleContext::MakeGLPLS(
    FiddleContextOptions options)
{
    printf("FiddleContext::MakeGLPLS: start\n");
    auto ctx = std::make_unique<FiddleContextGL>(options);
    printf("FiddleContext::MakeGLPLS: end\n");
    return ctx;
}

#ifndef RIVE_SKIA

std::unique_ptr<FiddleContext> FiddleContext::MakeGLSkia() { return nullptr; }

#else

#include "skia_factory.hpp"
#include "skia_renderer.hpp"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkSurface.h"
#include "skia/include/gpu/GrDirectContext.h"
#include "skia/include/gpu/gl/GrGLAssembleInterface.h"
#include "skia/include/gpu/gl/GrGLInterface.h"
#include "include/effects/SkImageFilters.h"

static GrGLFuncPtr get_skia_gl_proc_address(void* ctx, const char name[])
{
    printf("get_skia_gl_proc_address: Looking up function '%s'\n", name);
    return (GrGLFuncPtr)SDL_GL_GetProcAddress(name);
}

class FiddleContextGLSkia : public FiddleContextGLBase
{
public:
    FiddleContextGLSkia() :
        m_grContext(GrDirectContext::MakeGL(
            GrGLMakeAssembledInterface(nullptr, get_skia_gl_proc_address)))
    {
        if (!m_grContext)
        {
            fprintf(stderr, "GrDirectContext::MakeGL failed.\n");
            abort();
        }
    }

    rive::Factory* factory() final { return &m_factory; }

    RenderContext* renderContextOrNull() final { return nullptr; }

    RenderTarget* renderTargetOrNull() final { return nullptr; }

    std::unique_ptr<Renderer> makeRenderer(int width, int height) final
    {
        GrBackendRenderTarget backendRT(width,
                                        height,
                                        1 /*samples*/,
                                        0 /*stencilBits*/,
                                        {0 /*fbo 0*/, GL_RGBA8});

        SkSurfaceProps surfProps(0, kUnknown_SkPixelGeometry);

        m_skSurface =
            SkSurface::MakeFromBackendRenderTarget(m_grContext.get(),
                                                   backendRT,
                                                   kBottomLeft_GrSurfaceOrigin,
                                                   kRGBA_8888_SkColorType,
                                                   nullptr,
                                                   &surfProps);
        if (!m_skSurface)
        {
            fprintf(stderr, "SkSurface::MakeFromBackendRenderTarget failed.\n");
            abort();
        }
        return std::make_unique<SkiaRenderer>(m_skSurface->getCanvas());
    }

    void begin(const RenderContext::FrameDescriptor& frameDescriptor) final
    {
        m_skSurface->getCanvas()->clear(frameDescriptor.clearColor);
        m_grContext->resetContext();
        m_skSurface->getCanvas()->save();
    }

    void onEnd(std::vector<uint8_t>* pixelData) final
    {
        m_skSurface->getCanvas()->restore();
        m_skSurface->flush();
    }

    void flushPLSContext(RenderTarget* offscreenRenderTarget) final {}

private:
    SkiaFactory m_factory;
    const sk_sp<GrDirectContext> m_grContext;
    sk_sp<SkSurface> m_skSurface;
};

std::unique_ptr<FiddleContext> FiddleContext::MakeGLSkia()
{
    return std::make_unique<FiddleContextGLSkia>();
}
#endif

#endif
