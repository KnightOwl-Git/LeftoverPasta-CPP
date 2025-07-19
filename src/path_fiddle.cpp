#include "fiddle_context.hpp"

#include "rive/math/simd.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/layout.hpp"
#include "rive/animation/state_machine_instance.hpp"
#include "rive/static_scene.hpp"

#include <fstream>
#include <iterator>
#include <vector>
#include <sstream>

#include "asset_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

// Replace GLFW with SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>


#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <sstream>
#endif

using namespace rive;

constexpr static char kMoltenVKICD[] =
    "dependencies/MoltenVK/Package/Release/MoltenVK/dynamic/dylib/macOS/"
    "MoltenVK_icd.json";

constexpr static char kSwiftShaderICD[] = "dependencies/SwiftShader/build/"
#ifdef __APPLE__
                                          "Darwin"
#elif defined(_WIN32)
                                          "Windows"
#else
                                          "Linux"
#endif
                                          "/vk_swiftshader_icd.json";
static FiddleContextOptions options;
static SDL_Window* window = nullptr;
static SDL_GLContext glContext = nullptr;
static int msaa = 0;
static bool forceAtomicMode = false;
static bool wireframe = false;
static bool disableFill = false;
static bool disableStroke = false;
static bool clockwiseFill = false;
static bool hotloadShaders = false;

static std::unique_ptr<FiddleContext> fiddleContext;

// Remove mouse_button_callback, mousemove_callback, key_callback, and all related variables and code for dragging, interactive points, and view manipulation.
// Remove registration of these callbacks in main.
// Remove code that draws interactive points or handles dragging/translation/scale in the render loop.
// Keep launch-time options and core Rive file loading/playing logic.

int lastWidth = 0, lastHeight = 0;
double fpsLastTime = 0;
int fpsFrames = 0;
static bool needsTitleUpdate = false;

static int animation = -1;
static int stateMachine = 0;

enum class API
{
    gl,
    metal,
    d3d,
    d3d12,
    dawn,
    vulkan,
};

API api =
#if defined(__APPLE__)
    API::metal
#elif defined(_WIN32)
    API::d3d
#else
    API::gl
#endif
    ;

bool angle = false;
bool skia = false;

// Remove mouse_button_callback, mousemove_callback, key_callback, and all related variables and code for dragging, interactive points, and view manipulation.
// Remove registration of these callbacks in main.
// Remove code that draws interactive points or handles dragging/translation/scale in the render loop.
// Keep launch-time options and core Rive file loading/playing logic.

std::unique_ptr<File> rivFile;
std::vector<std::unique_ptr<Artboard>> artboards;
std::vector<std::unique_ptr<Scene>> scenes;
std::vector<rive::rcp<rive::ViewModelInstance>> viewModelInstances;

static void clear_scenes()
{
    artboards.clear();
    scenes.clear();
    viewModelInstances.clear();
}

// Remove all references to stateMachine, animation, horzRepeat, upRepeat, downRepeat, paused, scale, and translate.
// In make_scenes, always create a single scene: use the first state machine if available, else the first animation.
static void make_scenes(int width = 0, int height = 0) {
    clear_scenes();
    auto artboard = rivFile->artboardDefault();
    
    // Set artboard dimensions to match the current window size if provided
    if (width > 0 && height > 0) {
        artboard->width(static_cast<float>(width));
        artboard->height(static_cast<float>(height));
    }
    
    std::unique_ptr<Scene> scene;
    if (stateMachine >= 0) {
        scene = artboard->stateMachineAt(stateMachine);
    } else if (animation >= 0) {
        scene = artboard->animationAt(animation);
    } else if (artboard->stateMachineCount() > 0) {
        scene = artboard->stateMachineAt(0);
    } else if (artboard->animationCount() > 0) {
        scene = artboard->animationAt(0);
    } else {
        scene = std::make_unique<StaticScene>(artboard.get());
    }
    int viewModelId = artboard.get()->viewModelId();
    viewModelInstances.push_back(
        viewModelId == -1
            ? rivFile->createViewModelInstance(artboard.get())
            : rivFile->createViewModelInstance(viewModelId, 0));
    artboard->bindViewModelInstance(viewModelInstances.back());
    if (viewModelInstances.back() != nullptr) {
        scene->bindViewModelInstance(viewModelInstances.back());
    }
    artboards.push_back(std::move(artboard));
    scenes.push_back(std::move(scene));
}

#ifdef __EMSCRIPTEN__
EM_JS(int, window_inner_width, (), { return window["innerWidth"]; });
EM_JS(int, window_inner_height, (), { return window["innerHeight"]; });
EM_JS(char*, get_location_hash_str, (), {
    var jsString = window.location.hash.substring(1);
    var lengthBytes = lengthBytesUTF8(jsString) + 1;
    var stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(jsString, stringOnWasmHeap, lengthBytes);
    return stringOnWasmHeap;
});
#endif

// Remove mouse_button_callback, mousemove_callback, key_callback, and all related variables and code for dragging, interactive points, and view manipulation.
// Remove registration of these callbacks in main.
// Remove code that draws interactive points or handles dragging/translation/scale in the render loop.
// Keep launch-time options and core Rive file loading/playing logic.

static void sdl_error_callback(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    printf("SDL error: %s\n", message);
}

static void set_environment_variable(const char* name, const char* value)
{
    if (const char* existingValue = getenv(name))
    {
        printf("warning: %s=%s already set. Overriding with %s=%s\n",
               name,
               existingValue,
               name,
               value);
    }
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);
#else
    setenv(name, value, /*overwrite=*/true);
#endif
}

std::unique_ptr<Renderer> renderer;
std::string rivName;

void renderFrame();

// Add the window refresh callback
void window_refresh_callback(SDL_Window* window) {
    renderFrame();
    if (api == API::gl) {
        SDL_GL_SwapWindow(window);
    }
}

static double lastFrameTime = 0.0;
static bool appInitialized = false;

// SDL3 App Callbacks
extern "C" SDL_AppResult SDL_AppInit(void** applicationstate, int argc, char* argv[])
{
    // Cause stdout and stderr to print immediately without buffering.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    printf("SDL_AppInit: Starting initialization...\n");

#ifdef DEBUG
    options.enableVulkanValidationLayers = true;
#endif

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--gl"))
        {
            api = API::gl;
        }
        else if (!strcmp(argv[i], "--glatomic"))
        {
            api = API::gl;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--glcw"))
        {
            api = API::gl;
            forceAtomicMode = true;
            clockwiseFill = true;
        }
        else if (!strcmp(argv[i], "--metal"))
        {
            api = API::metal;
        }
        else if (!strcmp(argv[i], "--metalcw"))
        {
            api = API::metal;
            clockwiseFill = true;
        }
        else if (!strcmp(argv[i], "--metalatomic"))
        {
            api = API::metal;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--mvk") || !strcmp(argv[i], "--moltenvk"))
        {
            set_environment_variable("VK_ICD_FILENAMES", kMoltenVKICD);
            api = API::vulkan;
        }
        else if (!strcmp(argv[i], "--mvkatomic") ||
                 !strcmp(argv[i], "--moltenvkatomic"))
        {
            set_environment_variable("VK_ICD_FILENAMES", kMoltenVKICD);
            api = API::vulkan;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--sw") || !strcmp(argv[i], "--swiftshader"))
        {
            set_environment_variable("VK_ICD_FILENAMES", kSwiftShaderICD);
            api = API::vulkan;
        }
        else if (!strcmp(argv[i], "--swatomic") ||
                 !strcmp(argv[i], "--swiftshaderatomic"))
        {
            set_environment_variable("VK_ICD_FILENAMES", kSwiftShaderICD);
            api = API::vulkan;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--dawn"))
        {
            api = API::dawn;
        }
        else if (!strcmp(argv[i], "--d3d"))
        {
            api = API::d3d;
        }
        else if (!strcmp(argv[i], "--d3d12"))
        {
            api = API::d3d12;
        }
        else if (!strcmp(argv[i], "--d3datomic"))
        {
            api = API::d3d;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--d3d12atomic"))
        {
            api = API::d3d12;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--vulkan") || !strcmp(argv[i], "--vk"))
        {
            api = API::vulkan;
        }
        else if (!strcmp(argv[i], "--vkcw"))
        {
            api = API::vulkan;
            clockwiseFill = true;
        }
        else if (!strcmp(argv[i], "--vulkanatomic") ||
                 !strcmp(argv[i], "--vkatomic"))
        {
            api = API::vulkan;
            forceAtomicMode = true;
        }
        else if (!strcmp(argv[i], "--skia"))
        {
            skia = true;
        }
        else if (sscanf(argv[i], "-a%i", &animation))
        {
            // Already updated animation.
        }
        else if (sscanf(argv[i], "-s%i", &stateMachine))
        {
            // Already updated stateMachine.
        }
        else if (!strcmp(argv[i], "--d3d12Warp"))
        {
            options.d3d12UseWarpDevice = true;
        }
        else if (!strcmp(argv[i], "--atomic"))
        {
            forceAtomicMode = true;
        }
        else if (!strncmp(argv[i], "--msaa", 6))
        {
            msaa = argv[i][6] - '0';
        }
        else if (!strcmp(argv[i], "--validation"))
        {
            options.enableVulkanValidationLayers = true;
        }
        else if (!strcmp(argv[i], "--gpu") || !strcmp(argv[i], "-G"))
        {
            options.gpuNameFilter = argv[++i];
        }
        else
        {
            rivName = argv[i];
        }
    }

    // Always use the hardcoded .riv file path
    rivName = getAssetPath("lp_unity_v10.riv");

    printf("SDL_AppInit: About to create window with API %d\n", (int)api);

    // Set up SDL window hints based on API
    if (api == API::gl) {
        printf("SDL_AppInit: Setting SDL_GL attributes for OpenGL 2.1 compatibility profile\n");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    }
    switch (api)
    {
        case API::metal:
        case API::d3d:
        case API::d3d12:
        case API::dawn:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
            break;
        case API::vulkan:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
            break;
        case API::gl:
            if (angle)
            {
                SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
            }
            else
            {
                SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
            }
            break;
    }

    // Create the window
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    switch (api) {
        case API::gl:
            windowFlags |= SDL_WINDOW_OPENGL;
            break;
        case API::vulkan:
            windowFlags |= SDL_WINDOW_VULKAN;
            break;
        case API::metal:
            windowFlags |= SDL_WINDOW_METAL;
            break;
        case API::d3d:
        case API::d3d12:
        case API::dawn:
            // For D3D/Dawn, we don't need special window flags
            // SDL will handle the rendering through the render driver hint
            break;
    }
    
    printf("SDL_AppInit: Creating window with flags 0x%x\n", windowFlags);
    window = SDL_CreateWindow("Rive Renderer", 1600, 1600, windowFlags);
    if (!window)
    {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    printf("SDL_AppInit: Window created successfully\n");

    // Create OpenGL context if needed
    if (api == API::gl) {
        printf("SDL_AppInit: Creating OpenGL context...\n");
        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            return SDL_APP_FAILURE;
        }
        printf("SDL_AppInit: OpenGL context created successfully\n");
        SDL_GL_MakeCurrent(window, glContext);
        printf("SDL_AppInit: Made OpenGL context current\n");
        SDL_GL_SetSwapInterval(0); // Disable vsync
        printf("SDL_AppInit: Set swap interval\n");
        int major, minor;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
        printf("Actual OpenGL context version: %d.%d\n", major, minor);
    }
    
    // Show the window (equivalent to glfwShowWindow)
    SDL_ShowWindow(window);
    
    // Ensure the window is fully visible before proceeding
    SDL_RaiseWindow(window);
    SDL_PumpEvents();
    
    // Add a small delay to ensure window is fully initialized
    SDL_Delay(100);



    printf("SDL_AppInit: Creating fiddle context for API %d\n", (int)api);
    switch (api)
    {
        case API::metal:
            fiddleContext = FiddleContext::MakeMetalPLS(options);
            break;
        //case API::d3d:
         //   fiddleContext = FiddleContext::MakeD3DPLS(options);
          //  break;
        //case API::d3d12:
        //    fiddleContext = FiddleContext::MakeD3D12PLS(options);
        //    break;
       // case API::dawn:
       //     fiddleContext = FiddleContext::MakeDawnPLS(options);
        //    break;
        case API::vulkan:
            fiddleContext = FiddleContext::MakeVulkanPLS(options);
            break;
       // case API::gl:
        //    printf("SDL_AppInit: Creating GL fiddle context (skia=%d)\n", skia);
        //    fiddleContext =
        //        skia ? FiddleContext::MakeGLSkia() : FiddleContext::MakeGLPLS();
        //    break;
    }
    if (!fiddleContext)
    {
        fprintf(stderr, "Failed to create a fiddle context.\n");
        abort();
    }

    appInitialized = true;
    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppEvent(void* applicationstate, SDL_Event* event)
{
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            return SDL_APP_FAILURE; // Return failure to quit
        case SDL_EVENT_KEY_DOWN:
            if (event->key.key == SDLK_ESCAPE)
            {
                return SDL_APP_FAILURE; // Return failure to quit
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            // Force an immediate render to update the display
            if (appInitialized) {
                renderFrame();
                if (api == API::gl) {
                    SDL_GL_SwapWindow(window);
                }
            }
            break;
    }
    return SDL_APP_CONTINUE; // Return continue to keep running
}

extern "C" SDL_AppResult SDL_AppIterate(void* applicationstate)
{
    if (!appInitialized) {
        return SDL_APP_CONTINUE;
    }

    renderFrame();
    fiddleContext->tick();
    
    if (api == API::gl)
    {
        SDL_GL_SwapWindow(window);
    }
    // For Metal and other APIs, we don't need to do anything
    // The Rive renderer handles the presentation internally
    // This is equivalent to what GLFW does for non-OpenGL APIs
    
    return SDL_APP_CONTINUE; // Return continue to keep running
}

extern "C" void SDL_AppQuit(void* applicationstate, SDL_AppResult result)
{
    fiddleContext = nullptr;
    if (glContext) {
        SDL_GL_DestroyContext(glContext);
    }
    SDL_DestroyWindow(window);
}

// No main function needed when using SDL_MAIN_USE_CALLBACKS
// SDL3 will handle the main entry point

static void update_window_title(double fps,
                                int instances,
                                int width,
                                int height)
{
    std::ostringstream title;
    if (fps != 0)
    {
        title << '[' << fps << " FPS]";
    }
    if (instances > 1)
    {
        title << " (x" << instances << " instances)";
    }
    if (skia)
    {
        title << " | SKIA Renderer";
    }
    else
    {
        title << " | RIVE Renderer";
    }
    if (msaa)
    {
        title << " (msaa" << msaa << ')';
    }
    else if (forceAtomicMode)
    {
        title << " (atomic)";
    }
    title << " | " << width << " x " << height;
    SDL_SetWindowTitle(window, title.str().c_str());
}

void renderFrame() {
    double currentTime = SDL_GetTicks() / 1000.0;
    double deltaSeconds = lastFrameTime > 0.0 ? (currentTime - lastFrameTime) : (1.0 / 60.0);
    lastFrameTime = currentTime;

    int width = 0, height = 0;
    int windowWidth = 0, windowHeight = 0;
    // Get both window size and pixel size to understand the scaling
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    SDL_GetWindowSizeInPixels(window, &width, &height);
    
    // For Metal on macOS, we need to manually scale the dimensions based on the backing scale factor
    if (api == API::metal) {
        // Get the backing scale factor from the Metal context
        float scale = fiddleContext->dpiScale(window);
        width = static_cast<int>(windowWidth * scale);
        height = static_cast<int>(windowHeight * scale);
        printf("Window size: %dx%d, Scaled pixel size: %dx%d (scale: %f)\n", windowWidth, windowHeight, width, height, scale);
    } else {
        printf("Window size: %dx%d, Pixel size: %dx%d\n", windowWidth, windowHeight, width, height);
    }
    if (lastWidth != width || lastHeight != height)
    {
        printf("size changed to %ix%i\n", width, height);
        lastWidth = width;
        lastHeight = height;
        fiddleContext->onSizeChanged(window, width, height, msaa);
        renderer = fiddleContext->makeRenderer(width, height);
        needsTitleUpdate = true;
        
        // Update artboard dimensions immediately when size changes
        if (rivFile && !artboards.empty()) {
            auto artboard = artboards.front().get();
            artboard->width(static_cast<float>(width));
            artboard->height(static_cast<float>(height));
        }
    }
    if (needsTitleUpdate)
    {
        update_window_title(0, 1, width, height);
        needsTitleUpdate = false;
    }

    if (!rivName.empty() && !rivFile)
    {
        std::ifstream rivStream(rivName, std::ios::binary);
        if (!rivStream.is_open()) {
            fprintf(stderr, "Failed to open .riv file: %s\n", rivName.c_str());
        } else {
            printf("Loading Rive file: %s\n", rivName.c_str());
            std::vector<uint8_t> rivBytes(std::istreambuf_iterator<char>(rivStream),
                                          {});
            rivFile = File::import(rivBytes, fiddleContext->factory());
            if (rivFile) {
                printf("Successfully loaded Rive file with %zu artboards\n", rivFile->artboardCount());
            } else {
                printf("Failed to import Rive file\n");
            }
        }
    }

    // Call right before begin()
    if (hotloadShaders)
    {
        hotloadShaders = false;

#ifndef RIVE_BUILD_FOR_IOS
        std::system("sh rebuild_shaders.sh /tmp/rive");
#endif
        fiddleContext->hotloadShaders();
    }
    fiddleContext->begin({
        .renderTargetWidth = static_cast<uint32_t>(width),
        .renderTargetHeight = static_cast<uint32_t>(height),
        .clearColor = 0xff303030,
        .msaaSampleCount = msaa,
        .disableRasterOrdering = forceAtomicMode,
        .wireframe = wireframe,
        .fillsDisabled = disableFill,
        .strokesDisabled = disableStroke,
        .clockwiseFillOverride = clockwiseFill,
    });

    if (rivFile)
    {
        if (artboards.size() != 1 || scenes.size() != 1)
        {
            make_scenes(width, height);
            printf("Created %d scenes\n", (int)scenes.size());
        }
        else
        {
            for (const auto& scene : scenes)
            {
                scene->advanceAndApply(static_cast<float>(deltaSeconds));
            }
        }
        // Artboard dimensions are now updated immediately when window size changes
        auto artboard = artboards.front().get();

        Mat2D m = computeAlignment(
            rive::Fit::layout,
            rive::Alignment::center,
            rive::AABB(0, 0, width, height),
            artboard->bounds()
        );

        renderer->save();
        renderer->transform(m);
        scenes.front()->draw(renderer.get());
        renderer->restore();
        
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            printf("Rendered frame %d\n", frameCount);
        }
    }
    else
    {
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            printf("No Rive file loaded, frame %d\n", frameCount);
        }
    }

    fiddleContext->end(window);

    if (rivFile)
    {
        // Count FPS.
        ++fpsFrames;
        double time = SDL_GetTicks() / 1000.0;
        double fpsElapsed = time - fpsLastTime;
        if (fpsElapsed > 2)
        {
            int instances = 1;
            double fps = fpsLastTime == 0 ? 0 : fpsFrames / fpsElapsed;
            update_window_title(fps, instances, width, height);
            fpsFrames = 0;
            fpsLastTime = time;
        }
    }
}
