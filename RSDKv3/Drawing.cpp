#include "RetroEngine.hpp"

// Workaround for a "bug" in Linux with AMD cards where the presented buffer
// isn't cleared and displays corrupted memory in the letter/pillar boxes.
//
// It's very possible the same thing happens in Windows and Nvidia on Linux but
// the GPU driver preemptively clears the texture to avoid out-of-bounds reads.
//
// The problem comes down to how viewAngle is used, or rather, unused. It is
// initialized to 0, never changed, then checked if it's greater or equal to
// 180.0f or greater than 0.0f to determine if the texture should be cleared.
// That means the texture is never cleared correctly.
//
// For the sake of maintaining the original code, we'll use a macro to disable
// it rather than remove it outright.
#define DONT_USE_VIEW_ANGLE (1)

ushort blendLookupTable[0x100 * 0x20];
ushort subtractLookupTable[0x100 * 0x20];
ushort tintLookupTable[0x10000];

// Extras used in blending
#define maxVal(a, b) (a >= b ? a : b)
#define minVal(a, b) (a <= b ? a : b)

int SCREEN_XSIZE        = 424;
int SCREEN_CENTERX      = 424 / 2;
int SCREEN_XSIZE_CONFIG = 424;

int touchWidth  = SCREEN_XSIZE;
int touchHeight = SCREEN_YSIZE;

DrawListEntry drawListEntries[DRAWLAYER_COUNT];

int gfxDataPosition;
GFXSurface gfxSurface[SURFACE_COUNT];
byte graphicData[GFXDATA_SIZE];

DrawVertex gfxPolyList[VERTEX_COUNT];
short gfxPolyListIndex[INDEX_COUNT];
ushort gfxVertexSize       = 0;
ushort gfxVertexSizeOpaque = 0;
ushort gfxIndexSize        = 0;
ushort gfxIndexSizeOpaque  = 0;

DrawVertex3D polyList3D[VERTEX3D_COUNT];

ushort vertexSize3D = 0;
ushort indexSize3D  = 0;
ushort tileUVArray[TILEUV_SIZE];
float floor3DXPos     = 0.0f;
float floor3DYPos     = 0.0f;
float floor3DZPos     = 0.0f;
float floor3DAngle    = 0;
bool render3DEnabled  = false;
bool hq3DFloorEnabled = false;

ushort texBuffer[HW_TEXBUFFER_SIZE];
byte texBufferMode = 0;

#if !RETRO_USE_ORIGINAL_CODE
int viewOffsetX = 0;
#endif
int viewWidth     = 0;
int viewHeight    = 0;
float viewAspect  = 0;
int bufferWidth   = 0;
int bufferHeight  = 0;
int virtualX      = 0;
int virtualY      = 0;
int virtualWidth  = 0;
int virtualHeight = 0;

#if !DONT_USE_VIEW_ANGLE
float viewAngle    = 0;
float viewAnglePos = 0;
#endif

#if RETRO_USING_OPENGL
GLuint gfxTextureID[HW_TEXTURE_COUNT];
GLuint framebufferHW  = 0;
GLuint renderbufferHW = 0;
GLuint retroBuffer    = 0;
GLuint retroBuffer2x  = 0;
GLuint videoBuffer    = 0;
#endif
DrawVertex screenRect[4];
DrawVertex retroScreenRect[4];

#if !RETRO_USE_ORIGINAL_CODE
// enable integer scaling, which is a modification of enhanced scaling
bool integerScaling = false;
// allows me to disable it to prevent blur on resolutions that match only on 1 axis
bool disableEnhancedScaling = false;
// enable bilinear scaling, which just disables the fancy upscaling that enhanced scaling does.
bool bilinearScaling = false;
#endif

int InitRenderDevice()
{
    char gameTitle[0x40];

    sprintf(gameTitle, Engine.gameWindowText, NULL);

#if RETRO_USING_SDL2
    SDL_Init(SDL_INIT_EVERYTHING);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, Engine.vsync ? "1" : "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    byte flags = 0;
#if RETRO_USING_OPENGL
    flags |= SDL_WINDOW_OPENGL;

#if RETRO_PLATFORM != RETRO_OSX // dude idk either you just gotta trust that this works
#if RETRO_PLATFORM != RETRO_ANDROID
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
#endif
#endif

#if RETRO_GAMEPLATFORM == RETRO_MOBILE
    Engine.startFullScreen = true;

    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);

    bool landscape = dm.h < dm.w;
    int h          = landscape ? dm.w : dm.h;
    int w          = landscape ? dm.h : dm.w;

    SCREEN_XSIZE = ((float)SCREEN_YSIZE * h / w);
    if (SCREEN_XSIZE % 1)
        ++SCREEN_XSIZE;

    if (SCREEN_XSIZE >= 500)
        SCREEN_XSIZE = 500;
#endif

    SCREEN_CENTERX = SCREEN_XSIZE / 2;
    viewOffsetX    = 0;

    Engine.window = SDL_CreateWindow(gameTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_XSIZE * Engine.windowScale,
                                     SCREEN_YSIZE * Engine.windowScale, SDL_WINDOW_ALLOW_HIGHDPI | flags);
#if !RETRO_USING_OPENGL
    Engine.renderer = SDL_CreateRenderer(Engine.window, -1, SDL_RENDERER_ACCELERATED);
#endif

    if (!Engine.window) {
        PrintLog("ERROR: failed to create window!");
        Engine.gameMode = ENGINE_EXITGAME;
        return 0;
    }

#if !RETRO_USING_OPENGL
    if (!Engine.renderer) {
        PrintLog("ERROR: failed to create renderer!");
        Engine.gameMode = ENGINE_EXITGAME;
        return 0;
    }

    SDL_RenderSetLogicalSize(Engine.renderer, SCREEN_XSIZE, SCREEN_YSIZE);
    SDL_SetRenderDrawBlendMode(Engine.renderer, SDL_BLENDMODE_BLEND);
#endif

#if !RETRO_USING_OPENGL
    Engine.screenBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCREEN_XSIZE, SCREEN_YSIZE);

    if (!Engine.screenBuffer) {
        PrintLog("ERROR: failed to create screen buffer!\nerror msg: %s", SDL_GetError());
        return 0;
    }

    if(Engine.useHQModes) {
        Engine.screenBuffer2x =
        SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCREEN_XSIZE * 2, SCREEN_YSIZE * 2);

        if (!Engine.screenBuffer2x) {
            PrintLog("ERROR: failed to create screen buffer HQ!\nerror msg: %s", SDL_GetError());
            return 0;
        }
    }
#endif

    if (Engine.borderless) {
        SDL_RestoreWindow(Engine.window);
        SDL_SetWindowBordered(Engine.window, SDL_FALSE);
    }

    SDL_SetWindowPosition(Engine.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_DisplayMode disp;
    int winID = SDL_GetWindowDisplayIndex(Engine.window);
    if (SDL_GetCurrentDisplayMode(winID, &disp) == 0) {
        Engine.screenRefreshRate = disp.refresh_rate;
    }
    else {
        printf("error: %s", SDL_GetError());
    }

#endif

#if RETRO_USING_SDL1
    SDL_Init(SDL_INIT_EVERYTHING);

    // SDL1.2 doesn't support hints it seems
    // SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, Engine.vsync ? "1" : "0");
    // SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    // SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    Engine.windowSurface = SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 32, SDL_SWSURFACE);
    if (!Engine.windowSurface) {
        PrintLog("ERROR: failed to create window!\nerror msg: %s", SDL_GetError());
        return 0;
    }
    // Set the window caption
    SDL_WM_SetCaption(gameTitle, NULL);

    Engine.screenBuffer =
        SDL_CreateRGBSurface(0, SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, 0xF800, 0x7E0, 0x1F, 0x00);

    if (!Engine.screenBuffer) {
        PrintLog("ERROR: failed to create screen buffer!\nerror msg: %s", SDL_GetError());
        return 0;
    }

    /*Engine.screenBuffer2x = SDL_SetVideoMode(SCREEN_XSIZE * 2, SCREEN_YSIZE * 2, 16, SDL_SWSURFACE);

    if (!Engine.screenBuffer2x) {
        PrintLog("ERROR: failed to create screen buffer HQ!\nerror msg: %s", SDL_GetError());
        return 0;
    }*/

    if (Engine.startFullScreen) {
        Engine.windowSurface =
            SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
        SDL_ShowCursor(SDL_FALSE);
        Engine.isFullScreen = true;
    }

    // TODO: not supported in 1.2?
    if (Engine.borderless) {
        // SDL_RestoreWindow(Engine.window);
        // SDL_SetWindowBordered(Engine.window, SDL_FALSE);
    }

    // SDL_SetWindowPosition(Engine.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    Engine.useHQModes = false; // disabled
    Engine.borderless = false; // disabled
#endif

#if RETRO_USING_OPENGL
    // Init GL
    Engine.glContext = SDL_GL_CreateContext(Engine.window);

    SDL_GL_SetSwapInterval(Engine.vsync ? 1 : 0);

#if RETRO_PLATFORM != RETRO_ANDROID && RETRO_PLATFORM != RETRO_OSX
    // glew Setup
    GLenum err = glewInit();
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        PrintLog("glew init error:");
        PrintLog((const char *)glewGetErrorString(err));
        return false;
    }
#endif
    Engine.highResMode = false;
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glDisable(GL_LIGHTING);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    SetupPolygonLists();

    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();

    // Allows for texture locations in pixels instead of from 0.0 to 1.0, saves us having to do this every time we set UVs
    glScalef(1.0 / HW_TEXTURE_SIZE, 1.0 / HW_TEXTURE_SIZE, 1.0f);
    glMatrixMode(GL_PROJECTION);

    for (int i = 0; i < HW_TEXTURE_COUNT; i++) {
        glGenTextures(1, &gfxTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, gfxTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, HW_TEXTURE_SIZE, HW_TEXTURE_SIZE, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, texBuffer);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenFramebuffers(1, &framebufferHW);
    glBindFramebuffer(GL_FRAMEBUFFER, framebufferHW);
    glGenTextures(1, &renderbufferHW);
    glBindTexture(GL_TEXTURE_2D, renderbufferHW);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderbufferHW, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    UpdateHardwareTextures();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClear(GL_COLOR_BUFFER_BIT);

    glGenTextures(1, &retroBuffer);
    glBindTexture(GL_TEXTURE_2D, retroBuffer);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_XSIZE, SCREEN_YSIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    glGenTextures(1, &retroBuffer2x);
    glBindTexture(GL_TEXTURE_2D, retroBuffer2x);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_XSIZE * 2, SCREEN_YSIZE * 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    for (int c = 0; c < 0x10000; ++c) {
        int r               = (c & 0b1111100000000000) >> 8;
        int g               = (c & 0b0000011111100000) >> 3;
        int b               = (c & 0b0000000000011111) << 3;
        gfxPalette16to32[c] = (0xFF << 24) | (b << 16) | (g << 8) | (r << 0);
    }
    SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE, SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
#endif

#if RETRO_USING_SDL2 && (RETRO_PLATFORM == RETRO_iOS || RETRO_PLATFORM == RETRO_ANDROID || RETRO_PLATFORM == RETRO_WP7)
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(0, &mode);
    int vw = mode.w;
    int vh = mode.h;
    if (mode.h > mode.w) {
        vw = mode.h;
        vh = mode.w;
    }
    SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE, vw, vh);
#elif RETRO_USING_SDL2 && RETRO_USING_OPENGL
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(Engine.window, &drawableWidth, &drawableHeight);
    SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE, drawableWidth, drawableHeight);
#elif RETRO_USING_SDL2
    SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE, SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
#endif

    if (renderType == RENDER_SW) {
        Engine.frameBuffer   = new ushort[GFX_LINESIZE * SCREEN_YSIZE];
        memset(Engine.frameBuffer, 0, (GFX_LINESIZE * SCREEN_YSIZE) * sizeof(ushort));
        if (Engine.useHQModes) {
            Engine.frameBuffer2x = new ushort[GFX_LINESIZE_DOUBLE * (SCREEN_YSIZE * 2)];
            memset(Engine.frameBuffer2x, 0, GFX_LINESIZE_DOUBLE * (SCREEN_YSIZE * 2) * sizeof(ushort));
        }
        
#if RETRO_USING_OPENGL
        Engine.texBuffer   = new uint[SCREEN_XSIZE * SCREEN_YSIZE];
        Engine.texBuffer2x = new uint[(SCREEN_XSIZE * 2) * (SCREEN_YSIZE * 2)];
        memset(Engine.texBuffer, 0, (SCREEN_XSIZE * SCREEN_YSIZE) * sizeof(uint));
        memset(Engine.texBuffer2x, 0, (SCREEN_XSIZE * 2) * (SCREEN_YSIZE * 2) * sizeof(uint));
#endif
    }

    if (Engine.startFullScreen) {
        Engine.isFullScreen = true;
        SetFullScreen(Engine.isFullScreen);
    }

    OBJECT_BORDER_X2 = SCREEN_XSIZE + 0x80;
    // OBJECT_BORDER_Y2 = SCREEN_YSIZE + 0x100;

    return 1;
}
void FlipScreen()
{
    if (Engine.gameMode == ENGINE_EXITGAME)
        return;

#if !RETRO_USE_ORIGINAL_CODE
    float dimAmount = 1.0;
    if ((!Engine.masterPaused || Engine.frameStep) && !drawStageGFXHQ) {
        if (Engine.dimTimer < Engine.dimLimit) {
            if (Engine.dimPercent < 1.0) {
                Engine.dimPercent += 0.05;
                if (Engine.dimPercent > 1.0)
                    Engine.dimPercent = 1.0;
            }
        }
        else if (Engine.dimPercent > 0.25 && Engine.dimLimit >= 0) {
            Engine.dimPercent *= 0.9;
        }

        dimAmount = Engine.dimMax * Engine.dimPercent;
    }
#endif

    if (renderType == RENDER_SW) {
#if RETRO_USING_OPENGL

#if !RETRO_USE_ORIGINAL_CODE
        if (dimAmount < 1.0 && stageMode != STAGEMODE_PAUSED)
            DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, 0, 0, 0, 0xFF - (dimAmount * 0xFF));
#endif
        if (Engine.gameMode == ENGINE_VIDEOWAIT) {
            FlipScreenVideo();
        }
        else {
            TransferRetroBuffer();
            RenderFromRetroBuffer();
        }
#elif RETRO_USING_SDL2
        SDL_Rect *destScreenPos = NULL;
        SDL_Rect destScreenPos_scaled, destScreenPosRect;
        SDL_Texture *texTarget = NULL;

        switch (Engine.scalingMode) {
            // reset to default if value is invalid.
            default: Engine.scalingMode = 0; break;
            case 0: break;                         // nearest
            case 1: integerScaling = true; break;  // integer scaling
            case 2: break;                         // sharp bilinear
            case 3: bilinearScaling = true; break; // regular old bilinear
        }

        SDL_GetWindowSize(Engine.window, &Engine.windowXSize, &Engine.windowYSize);
        float screenxsize = SCREEN_XSIZE;
        float screenysize = SCREEN_YSIZE;

        // check if enhanced scaling is even necessary to be calculated by checking if the screen size is close enough on one axis
        // unfortunately it has to be "close enough" because of floating point precision errors. dang it
        if (Engine.scalingMode == 2) {
            bool cond1 = std::round((Engine.windowXSize / screenxsize) * 24) / 24 == std::floor(Engine.windowXSize / screenxsize);
            bool cond2 = std::round((Engine.windowYSize / screenysize) * 24) / 24 == std::floor(Engine.windowYSize / screenysize);
            if (cond1 || cond2)
                disableEnhancedScaling = true;
        }

        // get 2x resolution if HQ is enabled.
        if (drawStageGFXHQ) {
            screenxsize *= 2;
            screenysize *= 2;
        }

        if ((Engine.scalingMode != 0 && !disableEnhancedScaling) && Engine.gameMode != ENGINE_VIDEOWAIT) {
            // set up integer scaled texture, which is scaled to the largest integer scale of the screen buffer
            // before you make a texture that's larger than the window itself. This texture will then be scaled
            // up to the actual screen size using linear interpolation. This makes even window/screen scales
            // nice and sharp, and uneven scales as sharp as possible without creating wonky pixel scales,
            // creating a nice image.

            // get integer scale
            float scale = 1;
            if (!bilinearScaling) {
                scale = std::fminf(std::floor((float)Engine.windowXSize / (float)SCREEN_XSIZE),
                                   std::floor((float)Engine.windowYSize / (float)SCREEN_YSIZE));
            }
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // set interpolation to linear
            // create texture that's integer scaled.
            texTarget =
                SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, SCREEN_XSIZE * scale, SCREEN_YSIZE * scale);

            // keep aspect
            float aspectScale = std::fminf(Engine.windowYSize / screenysize, Engine.windowXSize / screenxsize);
            if (integerScaling) {
                aspectScale = std::floor(aspectScale);
            }
            float xoffset          = (Engine.windowXSize - (screenxsize * aspectScale)) / 2;
            float yoffset          = (Engine.windowYSize - (screenysize * aspectScale)) / 2;
            destScreenPos_scaled.x = std::round(xoffset);
            destScreenPos_scaled.y = std::round(yoffset);
            destScreenPos_scaled.w = std::round(screenxsize * aspectScale);
            destScreenPos_scaled.h = std::round(screenysize * aspectScale);
            // fill the screen with the texture, making lerp work.
            SDL_RenderSetLogicalSize(Engine.renderer, Engine.windowXSize, Engine.windowYSize);
        }
        else if (Engine.gameMode == ENGINE_VIDEOWAIT) {
            float screenAR = float(SCREEN_XSIZE) / float(SCREEN_YSIZE);
            if (screenAR > videoAR) {                               // If the screen is wider than the video. (Pillarboxed)
                uint videoW         = uint(SCREEN_YSIZE * videoAR); // This is to force Pillarboxed mode if the screen is wider than the video.
                destScreenPosRect.x = (SCREEN_XSIZE - videoW) / 2;  // Centers the video horizontally.
                destScreenPosRect.w = videoW;

                destScreenPosRect.y = 0;
                destScreenPosRect.h = SCREEN_YSIZE;
            }
            else {
                uint videoH         = uint(float(SCREEN_XSIZE) / videoAR); // This is to force letterbox mode if the video is wider than the screen.
                destScreenPosRect.y = (SCREEN_YSIZE - videoH) / 2;         // Centers the video vertically.
                destScreenPosRect.h = videoH;

                destScreenPosRect.x = 0;
                destScreenPosRect.w = SCREEN_XSIZE;
            }
            destScreenPos = &destScreenPosRect;
        }

        int pitch = 0;
        SDL_SetRenderTarget(Engine.renderer, texTarget);

        // Clear the screen. This is needed to keep the
        // pillarboxes in fullscreen from displaying garbage data.
        SDL_RenderClear(Engine.renderer);
		int mirrorMode = GetGlobalVariableByName("Options.MirrorMode");

        ushort *pixels = NULL;
        if (Engine.gameMode != ENGINE_VIDEOWAIT) {
            if (!drawStageGFXHQ) {
                SDL_UpdateTexture(Engine.screenBuffer, NULL, (void *)Engine.frameBuffer, GFX_LINESIZE * sizeof(ushort));
                SDL_RenderCopy(Engine.renderer, Engine.screenBuffer, NULL, destScreenPos);
            }
            else {
                int w = 0, h = 0;
                SDL_QueryTexture(Engine.screenBuffer2x, NULL, NULL, &w, &h);
                SDL_LockTexture(Engine.screenBuffer2x, NULL, (void **)&pixels, &pitch);

                // Width of the SDL texture in 16bit pixels
                const int lineWidth = pitch / sizeof(ushort);

                // 2x nearest neighbor scaling of the upper lines
                ushort *framebufferPtr = Engine.frameBuffer;
                for (int y = 0; y < (SCREEN_YSIZE / 2) + 12; ++y) {
                    for (int x = 0; x < SCREEN_XSIZE; ++x) {
						if (mirrorMode == 0) {
							*pixels = *framebufferPtr;
						} else {
							*pixels = framebufferPtr[SCREEN_XSIZE - 1 - x];
						}
                        *(pixels + 1) = *framebufferPtr;
                        *(pixels + lineWidth) = *framebufferPtr;
                        *(pixels + lineWidth + 1) = *framebufferPtr;
                        pixels += 2;
                        framebufferPtr++;
                    }
                    framebufferPtr += GFX_LINESIZE - SCREEN_XSIZE;
                    pixels += lineWidth;
                }

                // Simple copy of the lower lines from the pre-scaled framebuffer
                framebufferPtr = Engine.frameBuffer2x;
                for (int y = 0; y < ((SCREEN_YSIZE / 2) - 12) * 2; ++y) {
                    memcpy(pixels, framebufferPtr, (SCREEN_XSIZE * 2) * sizeof(ushort));
                    framebufferPtr += GFX_LINESIZE_DOUBLE;
                    pixels += lineWidth;
                }
                SDL_UnlockTexture(Engine.screenBuffer2x);
                SDL_RenderCopy(Engine.renderer, Engine.screenBuffer2x, NULL, destScreenPos);
            }
        }
        else {
            SDL_RenderCopy(Engine.renderer, Engine.videoBuffer, NULL, destScreenPos);
        }

        if ((Engine.scalingMode != 0 && !disableEnhancedScaling) && Engine.gameMode != ENGINE_VIDEOWAIT) {
            // set render target back to the screen.
            SDL_SetRenderTarget(Engine.renderer, NULL);
            // clear the screen itself now, for same reason as above
            SDL_RenderClear(Engine.renderer);
            // copy texture to screen with lerp
            SDL_RenderCopy(Engine.renderer, texTarget, NULL, &destScreenPos_scaled);
            // Apply dimming
            SDL_SetRenderDrawColor(Engine.renderer, 0, 0, 0, 0xFF - (dimAmount * 0xFF));
            if (dimAmount < 1.0)
                SDL_RenderFillRect(Engine.renderer, NULL);
            // finally present it
            SDL_RenderPresent(Engine.renderer);
            // reset everything just in case
            SDL_RenderSetLogicalSize(Engine.renderer, SCREEN_XSIZE, SCREEN_YSIZE);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
            // putting some FLEX TAPE� on that memory leak
            SDL_DestroyTexture(texTarget);
        }
        else {
            // Apply dimming
            SDL_SetRenderDrawColor(Engine.renderer, 0, 0, 0, 0xFF - (dimAmount * 0xFF));
            if (dimAmount < 1.0)
                SDL_RenderFillRect(Engine.renderer, NULL);
            // no change here
            SDL_RenderPresent(Engine.renderer);
        }
#elif RETRO_USING_SDL1
        ushort *px = (ushort *)Engine.screenBuffer->pixels;
        int w      = SCREEN_XSIZE * Engine.windowScale;
        int h      = SCREEN_YSIZE * Engine.windowScale;

        // TODO: there's gotta be a way to have SDL1.2 fill the window with the surface... right?
        if (Engine.gameMode != ENGINE_VIDEOWAIT) {
            if (Engine.windowScale == 1) {
                ushort *frameBufferPtr = Engine.frameBuffer;
                for (int y = 0; y < SCREEN_YSIZE; ++y) {
                    for (int x = 0; x < SCREEN_XSIZE; ++x) {
                        pixels[x] = frameBufferPtr[x];
                    }
                    frameBufferPtr += GFX_LINESIZE;
                    px += Engine.screenBuffer->pitch / sizeof(ushort);
                }
                // memcpy(Engine.screenBuffer->pixels, Engine.frameBuffer, Engine.screenBuffer->pitch * SCREEN_YSIZE);
            }
            else {
                // TODO: this better, I really dont know how to use SDL1.2 well lol
                int dx = 0, dy = 0;
                do {
                    do {
                        int x = (int)(dx * (1.0f / Engine.windowScale));
                        int y = (int)(dy * (1.0f / Engine.windowScale));

                        px[dx + (dy * w)] = Engine.frameBuffer[x + (y * GFX_LINESIZE)];

                        dx++;
                    } while (dx < w);
                    dy++;
                    dx = 0;
                } while (dy < h);
            }
            // Apply image to screen
            SDL_BlitSurface(Engine.screenBuffer, NULL, Engine.windowSurface, NULL);
        }
        else {
            // Apply image to screen
            SDL_BlitSurface(Engine.videoBuffer, NULL, Engine.windowSurface, NULL);
        }

        // Update Screen
        SDL_Flip(Engine.windowSurface);
#endif
    }
    else if (renderType == RENDER_HW) {
        if (dimAmount < 1.0 && stageMode != STAGEMODE_PAUSED)
            DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, 0, 0, 0, 0xFF - (dimAmount * 0xFF));

        bool fb             = Engine.useFBTexture;
        Engine.useFBTexture = Engine.useFBTexture || stageMode == STAGEMODE_PAUSED && Engine.gameMode != ENGINE_DEVMENU;

        if (Engine.gameMode == ENGINE_VIDEOWAIT)
            FlipScreenVideo();
        else
            Engine.highResMode ? FlipScreenHRes() : Engine.useFBTexture ? FlipScreenFB() : FlipScreenNoFB();

        Engine.useFBTexture = fb;
    }
}

void FlipScreenFB()
{
#if RETRO_USING_OPENGL
    glLoadIdentity();
    glRotatef(-90.0, 0.0, 0.0, 1.0);
    glOrtho(0, SCREEN_XSIZE << 4, 0.0, SCREEN_YSIZE << 4, -1.0, 1.0);
    glViewport(0, 0, SCREEN_YSIZE, SCREEN_XSIZE);

    glBindFramebuffer(GL_FRAMEBUFFER, framebufferHW);

    glBindTexture(GL_TEXTURE_2D, gfxTextureID[texPaletteNum]);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisable(GL_BLEND);

    if (render3DEnabled) {
        float floor3DTop    = 2.0;
        float floor3DBottom = SCREEN_YSIZE + 4.0;

        // Non Blended rendering
        glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
        glDrawElements(GL_TRIANGLES, gfxIndexSizeOpaque, GL_UNSIGNED_SHORT, gfxPolyListIndex);
        glEnable(GL_BLEND);

        // Init 3D Plane
        glViewport(floor3DTop, 0, floor3DBottom, SCREEN_XSIZE);
        glPushMatrix();
        glLoadIdentity();
        CalcPerspective(1.8326f, viewAspect, 0.1f, 2000.0f);
        glRotatef(-90.0, 0.0, 0.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glScalef(1.20f, 0.98f, -1.0f);
        glRotatef(floor3DAngle + 180.0f, 0, 1.0f, 0);
        glTranslatef(floor3DXPos, floor3DYPos, floor3DZPos);

        glVertexPointer(3, GL_FLOAT, sizeof(DrawVertex3D), &polyList3D[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex3D), &polyList3D[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex3D), &polyList3D[0].colour);
        glDrawElements(GL_TRIANGLES, indexSize3D, GL_UNSIGNED_SHORT, gfxPolyListIndex);
        glLoadIdentity();

        // Return for blended rendering
        glMatrixMode(GL_PROJECTION);
        glViewport(0, 0, SCREEN_YSIZE, SCREEN_XSIZE);
        glPopMatrix();
    }
    else {
        // Non Blended rendering
        glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
        glDrawElements(GL_TRIANGLES, gfxIndexSizeOpaque, GL_UNSIGNED_SHORT, gfxPolyListIndex);

        glEnable(GL_BLEND);
    }

    int blendedGfxCount = gfxIndexSize - gfxIndexSizeOpaque;

    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
    glDrawElements(GL_TRIANGLES, blendedGfxCount, GL_UNSIGNED_SHORT, &gfxPolyListIndex[gfxIndexSizeOpaque]);
    glDisableClientState(GL_COLOR_ARRAY);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
    RenderFromTexture();
}

void FlipScreenNoFB()
{
#if RETRO_USING_OPENGL
    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();
    glOrtho(0, SCREEN_XSIZE << 4, SCREEN_YSIZE << 4, 0.0, -1.0, 1.0);
    glViewport(viewOffsetX, 0, viewWidth, viewHeight);

    glBindTexture(GL_TEXTURE_2D, gfxTextureID[texPaletteNum]);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisable(GL_BLEND);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);

    if (render3DEnabled) {
        float scale         = viewHeight / SCREEN_YSIZE;
        float floor3DTop    = -2.0 * scale;
        float floor3DBottom = (viewHeight)-4.0 * scale;

        // Non Blended rendering
        glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
        glDrawElements(GL_TRIANGLES, gfxIndexSizeOpaque, GL_UNSIGNED_SHORT, gfxPolyListIndex);
        glEnable(GL_BLEND);

        // Init 3D Plane
        glViewport(viewOffsetX, floor3DTop, viewWidth, floor3DBottom);
        glPushMatrix();
        glLoadIdentity();
        CalcPerspective(1.8326f, viewAspect, 0.1f, 2000.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glScalef(1.35f, -0.9f, -1.0f);
        glRotatef(floor3DAngle + 180.0f, 0, 1.0f, 0);
        glTranslatef(floor3DXPos, floor3DYPos, floor3DZPos);

        glVertexPointer(3, GL_FLOAT, sizeof(DrawVertex3D), &polyList3D[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex3D), &polyList3D[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex3D), &polyList3D[0].colour);
        glDrawElements(GL_TRIANGLES, indexSize3D, GL_UNSIGNED_SHORT, gfxPolyListIndex);
        glLoadIdentity();

        // Return for blended rendering
        glMatrixMode(GL_PROJECTION);
        glViewport(viewOffsetX, 0, viewWidth, viewHeight);
        glPopMatrix();
    }
    else {
        // Non Blended rendering
        glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
        glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
        glDrawElements(GL_TRIANGLES, gfxIndexSizeOpaque, GL_UNSIGNED_SHORT, gfxPolyListIndex);

        glEnable(GL_BLEND);
    }

    int blendedGfxCount = gfxIndexSize - gfxIndexSizeOpaque;

    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
    glDrawElements(GL_TRIANGLES, blendedGfxCount, GL_UNSIGNED_SHORT, &gfxPolyListIndex[gfxIndexSizeOpaque]);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glDisableClientState(GL_COLOR_ARRAY);
#endif
}

void FlipScreenHRes()
{
#if RETRO_USING_OPENGL
#if DONT_USE_VIEW_ANGLE
    glClear(GL_COLOR_BUFFER_BIT);
#else
    if (viewAngle >= 180.0) {
        if (viewAnglePos < 180.0) {
            viewAnglePos += 7.5;
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    else if (viewAnglePos > 0.0) {
        viewAnglePos -= 7.5;
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif

    glLoadIdentity();

    glOrtho(0, SCREEN_XSIZE << 4, SCREEN_YSIZE << 4, 0.0, -1.0, 1.0);
    glViewport(viewOffsetX, 0, bufferWidth, bufferHeight);
    glBindTexture(GL_TEXTURE_2D, gfxTextureID[texPaletteNum]);
    glDisable(GL_BLEND);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
    glDrawElements(GL_TRIANGLES, gfxIndexSizeOpaque, GL_UNSIGNED_SHORT, gfxPolyListIndex);

    glEnable(GL_BLEND);

    int blendedGfxCount = gfxIndexSize - gfxIndexSizeOpaque;
    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &gfxPolyList[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), &gfxPolyList[0].colour);
    glDrawElements(GL_TRIANGLES, blendedGfxCount, GL_UNSIGNED_SHORT, &gfxPolyListIndex[gfxIndexSizeOpaque]);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glDisableClientState(GL_COLOR_ARRAY);
#endif
}

void RenderFromTexture()
{
#if RETRO_USING_OPENGL
    glBindTexture(GL_TEXTURE_2D, renderbufferHW);
#if DONT_USE_VIEW_ANGLE
    glClear(GL_COLOR_BUFFER_BIT);
#else
    if (viewAngle >= 180.0) {
        if (viewAnglePos < 180.0) {
            viewAnglePos += 7.5;
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    else if (viewAnglePos > 0.0) {
        viewAnglePos -= 7.5;
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif
    glLoadIdentity();
    glViewport(viewOffsetX, 0, viewWidth, viewHeight);
    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &screenRect[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &screenRect[0].u);
    glDisable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, &gfxPolyListIndex);
#endif
}

void RenderFromRetroBuffer()
{
#if RETRO_USING_OPENGL
    if (drawStageGFXHQ) {
        glBindTexture(GL_TEXTURE_2D, retroBuffer2x);

        uint *texBufferPtr     = Engine.texBuffer2x;
        ushort *framebufferPtr = Engine.frameBuffer;
        for (int y = 0; y < (SCREEN_YSIZE / 2) + 12; ++y) {
            for (int x = 0; x < SCREEN_XSIZE; ++x) {
                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                texBufferPtr++;

                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                texBufferPtr++;

                framebufferPtr++;
            }
            framebufferPtr += GFX_LINESIZE - SCREEN_XSIZE;

            framebufferPtr -= GFX_LINESIZE;
            for (int x = 0; x < SCREEN_XSIZE; ++x) {
                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                texBufferPtr++;

                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                texBufferPtr++;

                framebufferPtr++;
            }
            framebufferPtr += GFX_LINESIZE - SCREEN_XSIZE;
        }

        framebufferPtr = Engine.frameBuffer2x;
        for (int y = 0; y < ((SCREEN_YSIZE / 2) - 12) * 2; ++y) {
            for (int x = 0; x < SCREEN_XSIZE; ++x) {
                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                framebufferPtr++;
                texBufferPtr++;

                *texBufferPtr = gfxPalette16to32[*framebufferPtr];
                framebufferPtr++;
                texBufferPtr++;
            }
            framebufferPtr += 2 * (GFX_LINESIZE - SCREEN_XSIZE);
        }

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_XSIZE * 2, SCREEN_YSIZE * 2, GL_RGBA, GL_UNSIGNED_BYTE, Engine.texBuffer2x);
    }

    glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D, drawStageGFXHQ ? retroBuffer2x : retroBuffer);
#if DONT_USE_VIEW_ANGLE
    glClear(GL_COLOR_BUFFER_BIT);
#else
    if (viewAngle >= 180.0) {
        if (viewAnglePos < 180.0) {
            viewAnglePos += 7.5;
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    else if (viewAnglePos > 0.0) {
        viewAnglePos -= 7.5;
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif
    glViewport(viewOffsetX, 0, viewWidth, viewHeight);

    glVertexPointer(2, GL_SHORT, sizeof(DrawVertex), &retroScreenRect[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex), &retroScreenRect[0].u);
    glDisable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, &gfxPolyListIndex);
#endif
}

#define normalize(val, minVal, maxVal) ((float)(val) - (float)(minVal)) / ((float)(maxVal) - (float)(minVal))
void FlipScreenVideo()
{
#if RETRO_USING_OPENGL
    DrawVertex3D screenVerts[4];
    for (int i = 0; i < 4; ++i) {
        screenVerts[i].u = retroScreenRect[i].u;
        screenVerts[i].v = retroScreenRect[i].v;
    }

    float best = minVal(viewWidth / (float)videoWidth, viewHeight / (float)videoHeight);

    float w = videoWidth * best;
    float h = videoHeight * best;

    float x = normalize((viewWidth - w) / 2, 0, viewWidth) * 2 - 1.0f;
    float y = -(normalize((viewHeight - h) / 2, 0, viewHeight) * 2 - 1.0f);

    w = normalize(w, 0, viewWidth) * 2;
    h = -(normalize(h, 0, viewHeight) * 2);

    screenVerts[0].x = x;
    screenVerts[0].y = y;

    screenVerts[1].x = w + x;
    screenVerts[1].y = y;

    screenVerts[2].x = x;
    screenVerts[2].y = h + y;

    screenVerts[3].x = w + x;
    screenVerts[3].y = h + y;

    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D, videoBuffer);
#if DONT_USE_VIEW_ANGLE
    glClear(GL_COLOR_BUFFER_BIT);
#else
    if (viewAngle >= 180.0) {
        if (viewAnglePos < 180.0) {
            viewAnglePos += 7.5;
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    else if (viewAnglePos > 0.0) {
        viewAnglePos -= 7.5;
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif
    glViewport(viewOffsetX, 0, viewWidth, viewHeight);
    glVertexPointer(2, GL_FLOAT, sizeof(DrawVertex3D), &screenVerts[0].x);
    glTexCoordPointer(2, GL_SHORT, sizeof(DrawVertex3D), &screenVerts[0].u);
    glDisable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, &gfxPolyListIndex);
#endif
}

void ReleaseRenderDevice()
{
    if (Engine.frameBuffer)
        delete[] Engine.frameBuffer;
    if (Engine.frameBuffer2x)
        delete[] Engine.frameBuffer2x;

#if RETRO_USING_OPENGL
    if (Engine.texBuffer)
        delete[] Engine.texBuffer;
    if (Engine.texBuffer2x)
        delete[] Engine.texBuffer2x;
#endif

    if (renderType == RENDER_SW) {
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        SDL_DestroyTexture(Engine.screenBuffer);
        Engine.screenBuffer = NULL;
#endif

#if RETRO_USING_SDL1 && !RETRO_USING_OPENGL
        SDL_FreeSurface(Engine.screenBuffer);
#endif
    }

#if RETRO_USING_OPENGL
	if (Engine.glContext) {
		for (int i = 0; i < HW_TEXTURE_COUNT; i++) glDeleteTextures(1, &gfxTextureID[i]);
#if RETRO_USING_SDL2
		SDL_GL_DeleteContext(Engine.glContext);
#endif
	}
#endif

#if RETRO_USING_SDL2
#if !RETRO_USING_OPENGL
    SDL_DestroyRenderer(Engine.renderer);
#endif
    SDL_DestroyWindow(Engine.window);
#endif
}

void SetFullScreen(bool fs)
{

    if (fs) {
#if RETRO_USING_SDL1
        Engine.windowSurface =
            SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
        SDL_ShowCursor(SDL_FALSE);
#endif
#if RETRO_USING_SDL2
        SDL_RestoreWindow(Engine.window);
        SDL_SetWindowFullscreen(Engine.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(0, &mode);

        int w = mode.w;
        int h = mode.h;
        if (mode.h > mode.w) {
            w = mode.h;
            h = mode.w;
        }

        float scaleH        = (mode.h / (float)SCREEN_YSIZE);
        Engine.useFBTexture = ((float)scaleH - (int)scaleH) != 0 || Engine.scalingMode;

        float width = w;
#if RETRO_PLATFORM != RETRO_iOS && RETRO_PLATFORM != RETRO_ANDROID
        float aspect = SCREEN_XSIZE_CONFIG / (float)SCREEN_YSIZE;
        width        = aspect * h;
        viewOffsetX  = abs(w - width) / 2;
        if (width > w) {
            int gameWidth = (w / (float)h) * SCREEN_YSIZE;
            if (renderType == RENDER_SW) {
                SetScreenSize(gameWidth, (gameWidth + 9) & -0x8);
            }
            else if (renderType == RENDER_HW){
                SetScreenSize(gameWidth, (gameWidth + 9) & -0x10);
            }

            width = 0;
            while (width <= w) {
                width += SCREEN_XSIZE;
            }
            width -= SCREEN_XSIZE;
            viewOffsetX = abs(w - width) / 2;
        }
#else
        viewOffsetX = 0;
#endif

        SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE, width, h);
    }
    else {
        viewOffsetX = 0;
#if RETRO_USING_SDL1
        Engine.windowSurface = SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE);
        SDL_ShowCursor(SDL_TRUE);
#endif
        Engine.useFBTexture = Engine.scalingMode;
        SetScreenDimensions(SCREEN_XSIZE_CONFIG, SCREEN_YSIZE, SCREEN_XSIZE_CONFIG * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
#if RETRO_USING_SDL2
        SDL_SetWindowFullscreen(Engine.window, SDL_FALSE);
        SDL_SetWindowSize(Engine.window, SCREEN_XSIZE_CONFIG * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
        SDL_SetWindowPosition(Engine.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_RestoreWindow(Engine.window);
#endif
    }
}

void GenerateBlendLookupTable()
{
    for (int y = 0; y < 0x100; y++) {
        for (int x = 0; x < 0x20; x++) {
            blendLookupTable[x + (0x20 * y)]    = y * x >> 8;
            subtractLookupTable[x + (0x20 * y)] = y * (0x1F - x) >> 8;
        }
    }

    for (int i = 0; i < 0x10000; i++) {
        int tintValue      = ((i & 0x1F) + ((i & 0x7E0) >> 6) + ((i & 0xF800) >> 11)) / 3 + 6;
        tintLookupTable[i] = 0x841 * minVal(tintValue, 0x1F);
    }
}

void ClearScreen(byte index)
{
    if (renderType == RENDER_SW) {
        ushort colour       = activePalette[index];
        ushort *framebuffer = Engine.frameBuffer;
        int cnt             = GFX_LINESIZE * SCREEN_YSIZE;
        while (cnt--) {
            *framebuffer = colour;
            ++framebuffer;
        }
    }
    else if (renderType == RENDER_HW) {
        gfxPolyList[gfxVertexSize].x        = 0.0f;
        gfxPolyList[gfxVertexSize].y        = 0.0f;
        gfxPolyList[gfxVertexSize].colour.r = activePalette32[index].r;
        gfxPolyList[gfxVertexSize].colour.g = activePalette32[index].g;
        gfxPolyList[gfxVertexSize].colour.b = activePalette32[index].b;
        gfxPolyList[gfxVertexSize].colour.a = 0xFF;
        gfxPolyList[gfxVertexSize].u        = 0.0f;
        gfxPolyList[gfxVertexSize].v        = 0.0f;

        gfxVertexSize++;
        gfxPolyList[gfxVertexSize].x        = SCREEN_XSIZE << 4;
        gfxPolyList[gfxVertexSize].y        = 0.0f;
        gfxPolyList[gfxVertexSize].colour.r = activePalette32[index].r;
        gfxPolyList[gfxVertexSize].colour.g = activePalette32[index].g;
        gfxPolyList[gfxVertexSize].colour.b = activePalette32[index].b;
        gfxPolyList[gfxVertexSize].colour.a = 0xFF;
        gfxPolyList[gfxVertexSize].u        = 0.0f;
        gfxPolyList[gfxVertexSize].v        = 0.0f;

        gfxVertexSize++;
        gfxPolyList[gfxVertexSize].x        = 0.0f;
        gfxPolyList[gfxVertexSize].y        = SCREEN_YSIZE << 4;
        gfxPolyList[gfxVertexSize].colour.r = activePalette32[index].r;
        gfxPolyList[gfxVertexSize].colour.g = activePalette32[index].g;
        gfxPolyList[gfxVertexSize].colour.b = activePalette32[index].b;
        gfxPolyList[gfxVertexSize].colour.a = 0xFF;
        gfxPolyList[gfxVertexSize].u        = 0.0f;
        gfxPolyList[gfxVertexSize].v        = 0.0f;

        gfxVertexSize++;
        gfxPolyList[gfxVertexSize].x        = SCREEN_XSIZE << 4;
        gfxPolyList[gfxVertexSize].y        = SCREEN_YSIZE << 4;
        gfxPolyList[gfxVertexSize].colour.r = activePalette32[index].r;
        gfxPolyList[gfxVertexSize].colour.g = activePalette32[index].g;
        gfxPolyList[gfxVertexSize].colour.b = activePalette32[index].b;
        gfxPolyList[gfxVertexSize].colour.a = 0xFF;
        gfxPolyList[gfxVertexSize].u        = 0.0f;
        gfxPolyList[gfxVertexSize].v        = 0.0f;
        gfxVertexSize++;

        gfxIndexSize += 6;
    }
}

void SetScreenSize(int width, int lineSize)
{
    SCREEN_XSIZE        = width;
    SCREEN_CENTERX      = (width / 2);
    SCREEN_SCROLL_LEFT  = SCREEN_CENTERX - 8;
    SCREEN_SCROLL_RIGHT = SCREEN_CENTERX + 8;
    OBJECT_BORDER_X2    = width + 0x80;

    GFX_LINESIZE          = lineSize;
    GFX_LINESIZE_MINUSONE = lineSize - 1;
    GFX_LINESIZE_DOUBLE   = 2 * lineSize;
    GFX_FRAMEBUFFERSIZE   = SCREEN_YSIZE * lineSize;
    GFX_FBUFFERMINUSONE   = SCREEN_YSIZE * lineSize - 1;
}

void CopyFrameOverlay2x()
{
    ushort *frameBuffer   = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    ushort *frameBuffer2x = Engine.frameBuffer2x;

    for (int y = 0; y < (SCREEN_YSIZE / 2) - 12; ++y) {
        for (int x = 0; x < GFX_LINESIZE; ++x) {
            if (*frameBuffer == 0xF81F) { // magenta
                frameBuffer2x += 2;
            }
            else {
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
            }
            ++frameBuffer;
        }

        frameBuffer -= GFX_LINESIZE;
        for (int x = 0; x < GFX_LINESIZE; ++x) {
            if (*frameBuffer == 0xF81F) { // magenta
                frameBuffer2x += 2;
            }
            else {
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
            }
            ++frameBuffer;
        }
    }
}
void TransferRetroBuffer()
{
#if RETRO_USING_OPENGL
    glBindTexture(GL_TEXTURE_2D, retroBuffer);

    ushort *frameBufferPtr = Engine.frameBuffer;
    uint *texBufferPtr     = Engine.texBuffer;
	int mirrorMode = GetGlobalVariableByName("Options.MirrorMode");
	int SkyHighMode = GetGlobalVariableByName("Options.SkyHighMode");
    for (int y = 0; y < SCREEN_YSIZE; ++y) {
		for (int x = 0; x < SCREEN_XSIZE; ++x) {
			if (mirrorMode == 0) {
				texBufferPtr[x] = gfxPalette16to32[frameBufferPtr[x]];
			} else {
				texBufferPtr[SCREEN_XSIZE - 1 - x] = gfxPalette16to32[frameBufferPtr[x]];
			}
		}

        texBufferPtr += SCREEN_XSIZE;
        frameBufferPtr += GFX_LINESIZE;
    }

	if (SkyHighMode == 0) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_XSIZE, SCREEN_YSIZE, GL_RGBA, GL_UNSIGNED_BYTE, Engine.texBuffer);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_XSIZE, SCREEN_YSIZE, GL_BGRA, GL_UNSIGNED_BYTE, Engine.texBuffer);
	}

    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void UpdateHardwareTextures()
{
	int SkyHighMode = GetGlobalVariableByName("Options.SkyHighMode");
    SetActivePalette(0, 0, SCREEN_YSIZE);
    UpdateTextureBufferWithTiles();
    UpdateTextureBufferWithSortedSprites();

#if RETRO_USING_OPENGL
    glBindTexture(GL_TEXTURE_2D, gfxTextureID[0]);
	if (SkyHighMode == 0) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HW_TEXTURE_SIZE, HW_TEXTURE_SIZE, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, texBuffer);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HW_TEXTURE_SIZE, HW_TEXTURE_SIZE, GL_BGRA, GL_UNSIGNED_SHORT_5_5_5_1, texBuffer);
	}
#endif

    for (byte b = 1; b < HW_TEXTURE_COUNT; ++b) {
        SetActivePalette(b, 0, SCREEN_YSIZE);
        UpdateTextureBufferWithTiles();
        UpdateTextureBufferWithSprites();

#if RETRO_USING_OPENGL
        glBindTexture(GL_TEXTURE_2D, gfxTextureID[b]);
		if (SkyHighMode == 0) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HW_TEXTURE_SIZE, HW_TEXTURE_SIZE, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, texBuffer);
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HW_TEXTURE_SIZE, HW_TEXTURE_SIZE, GL_BGRA, GL_UNSIGNED_SHORT_5_5_5_1, texBuffer);
		}
#endif
    }
    SetActivePalette(0, 0, SCREEN_YSIZE);
}
void SetScreenDimensions(int width, int height, int winWidth, int winHeight)
{
    bufferWidth  = width;
    bufferHeight = height;
    bufferWidth = viewWidth = touchWidth = winWidth;
    bufferHeight = viewHeight = touchHeight = winHeight;

    viewAspect = 0.75f;
    if (viewHeight > SCREEN_YSIZE * 2)
        hq3DFloorEnabled = true;
    else
        hq3DFloorEnabled = false;

    if (renderType == RENDER_SW) {
        SetScreenSize(width, (width + 9) & -0x8);
    }
    else if (renderType == RENDER_HW) {
        SetScreenSize(width, (width + 9) & -0x10);
    }
#if RETRO_USING_OPENGL
    if (framebufferHW)
        glDeleteFramebuffers(1, &framebufferHW);

    if (renderbufferHW)
        glDeleteTextures(1, &renderbufferHW);

    if (retroBuffer)
        glDeleteTextures(1, &retroBuffer);

    if (retroBuffer2x)
        glDeleteTextures(1, &retroBuffer2x);

    // Setup framebuffer texture

    int bufferW = 0;
    int val     = 0;
    do {
        val = 1 << bufferW++;
    } while (val < GFX_LINESIZE);
    bufferW--;

    int bufferH = 0;
    val         = 0;
    do {
        val = 1 << bufferH++;
    } while (val < SCREEN_YSIZE);
    bufferH--;

    glGenFramebuffers(1, &framebufferHW);
    glBindFramebuffer(GL_FRAMEBUFFER, framebufferHW);
    glGenTextures(1, &renderbufferHW);
    glBindTexture(GL_TEXTURE_2D, renderbufferHW);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1 << bufferH, 1 << bufferW, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderbufferHW, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenTextures(1, &retroBuffer);
    glBindTexture(GL_TEXTURE_2D, retroBuffer);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_XSIZE, SCREEN_YSIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    glGenTextures(1, &retroBuffer2x);
    glBindTexture(GL_TEXTURE_2D, retroBuffer2x);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_XSIZE * 2, SCREEN_YSIZE * 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#endif

    screenRect[0].x = -1;
    screenRect[0].y = 1;
    screenRect[0].u = 0;
    screenRect[0].v = SCREEN_XSIZE * 2;

    screenRect[1].x = 1;
    screenRect[1].y = 1;
    screenRect[1].u = 0;
    screenRect[1].v = 0;

    screenRect[2].x = -1;
    screenRect[2].y = -1;
    screenRect[2].u = (SCREEN_YSIZE - 0.5) * 4;
    screenRect[2].v = SCREEN_XSIZE * 2;

    screenRect[3].x = 1;
    screenRect[3].y = -1;
    screenRect[3].u = (SCREEN_YSIZE - 0.5) * 4;
    screenRect[3].v = 0;

    // HW_TEXTURE_SIZE == 1.0 due to the scaling we did on the Texture Matrix earlier

    retroScreenRect[0].x = -1;
    retroScreenRect[0].y = 1;
    retroScreenRect[0].u = 0;
    retroScreenRect[0].v = 0;

    retroScreenRect[1].x = 1;
    retroScreenRect[1].y = 1;
    retroScreenRect[1].u = HW_TEXTURE_SIZE;
    retroScreenRect[1].v = 0;

    retroScreenRect[2].x = -1;
    retroScreenRect[2].y = -1;
    retroScreenRect[2].u = 0;
    retroScreenRect[2].v = HW_TEXTURE_SIZE;

    retroScreenRect[3].x = 1;
    retroScreenRect[3].y = -1;
    retroScreenRect[3].u = HW_TEXTURE_SIZE;
    retroScreenRect[3].v = HW_TEXTURE_SIZE;

    ScaleViewport(winWidth, winHeight);
}

void ScaleViewport(int width, int height)
{
    virtualWidth  = width;
    virtualHeight = height;
    virtualX      = 0;
    virtualY      = 0;
    float virtualAspect = (float)width / height;
    float realAspect    = (float)viewWidth / viewHeight;
    if (virtualAspect < realAspect) {
        virtualHeight = viewHeight * ((float)width / viewWidth);
        virtualY      = (height - virtualHeight) >> 1;
    }
    else {
        virtualWidth = viewWidth * ((float)height / viewHeight);
        virtualX     = (width - virtualWidth) >> 1;
    }
}

void CalcPerspective(float fov, float aspectRatio, float nearPlane, float farPlane)
{
    float matrix[16];
    float w = 1.0 / tanf(fov * 0.5f);
    float h = 1.0 / (w * aspectRatio);
    float q = (nearPlane + farPlane) / (farPlane - nearPlane);

    matrix[0] = w;
    matrix[1] = 0;
    matrix[2] = 0;
    matrix[3] = 0;

    matrix[4] = 0;
    matrix[5] = h / 2;
    matrix[6] = 0;
    matrix[7] = 0;

    matrix[8]  = 0;
    matrix[9]  = 0;
    matrix[10] = q;
    matrix[11] = 1.0;

    matrix[12] = 0;
    matrix[13] = 0;
    matrix[14] = (((farPlane * -2.0f) * nearPlane) / (farPlane - nearPlane));
    matrix[15] = 0;

#if RETRO_USING_OPENGL
    glMultMatrixf(matrix);
#endif
}

void SetupPolygonLists()
{
    int vID = 0;
    for (int i = 0; i < VERTEX_COUNT; i++) {
        gfxPolyListIndex[vID++] = (i << 2) + 0;
        gfxPolyListIndex[vID++] = (i << 2) + 1;
        gfxPolyListIndex[vID++] = (i << 2) + 2;
        gfxPolyListIndex[vID++] = (i << 2) + 1;
        gfxPolyListIndex[vID++] = (i << 2) + 3;
        gfxPolyListIndex[vID++] = (i << 2) + 2;

        gfxPolyList[i].colour.r = 0xFF;
        gfxPolyList[i].colour.g = 0xFF;
        gfxPolyList[i].colour.b = 0xFF;
        gfxPolyList[i].colour.a = 0xFF;
    }

    for (int i = 0; i < VERTEX3D_COUNT; i++) {
        polyList3D[i].colour.r = 0xFF;
        polyList3D[i].colour.g = 0xFF;
        polyList3D[i].colour.b = 0xFF;
        polyList3D[i].colour.a = 0xFF;
    }
}

void UpdateTextureBufferWithTiles()
{
    int tileIndex = 0;
    if (texBufferMode == 0) {
        // regular 1024 set of tiles
        for (int h = 0; h < 512; h += 16) {
            for (int w = 0; w < 512; w += 16) {
                int dataPos = tileIndex++ << 8;
                int bufPos = w + (h * HW_TEXTURE_SIZE);
                for (int y = 0; y < TILE_SIZE; y++) {
                    for (int x = 0; x < TILE_SIZE; x++) {
                        if (tilesetGFXData[dataPos] > 0)
                            texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                        else
                            texBuffer[bufPos] = 0;
                        bufPos++;
                        dataPos++;
                    }
                    bufPos += HW_TEXTURE_SIZE - TILE_SIZE;
                }
            }
        }
    }
    else {
        // 3D Sky/HParallax version
        for (int h = 0; h < 504; h += 18) {
            for (int w = 0; w < 504; w += 18) {
                int dataPos = tileIndex++ << 8;

                // odd... but sure alright
                if (tileIndex == 783)
                    tileIndex = HW_TEXTURE_SIZE - 1;

                int bufPos = w + (h * HW_TEXTURE_SIZE);
                if (tilesetGFXData[dataPos] > 0)
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                else
                    texBuffer[bufPos] = 0;
                bufPos++;

                for (int l = 0; l < TILE_SIZE - 1; l++) {
                    if (tilesetGFXData[dataPos] > 0)
                        texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    else
                        texBuffer[bufPos] = 0;
                    bufPos++;
                    dataPos++;
                }

                if (tilesetGFXData[dataPos] > 0) {
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    bufPos++;
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                }
                else {
                    texBuffer[bufPos] = 0;
                    bufPos++;
                    texBuffer[bufPos] = 0;
                }
                bufPos++;
                dataPos -= TILE_SIZE - 1;
                bufPos += HW_TEXTURE_SIZE - TILE_SIZE - 2;

                for (int k = 0; k < TILE_SIZE; k++) {
                    if (tilesetGFXData[dataPos] > 0)
                        texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    else
                        texBuffer[bufPos] = 0;
                    bufPos++;
                    for (int l = 0; l < TILE_SIZE - 1; l++) {
                        if (tilesetGFXData[dataPos] > 0)
                            texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                        else
                            texBuffer[bufPos] = 0;
                        bufPos++;
                        dataPos++;
                    }
                    if (tilesetGFXData[dataPos] > 0) {
                        texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                        bufPos++;
                        texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    }
                    else {
                        texBuffer[bufPos] = 0;
                        bufPos++;
                        texBuffer[bufPos] = 0;
                    }
                    bufPos++;
                    dataPos++;
                    bufPos += HW_TEXTURE_SIZE - TILE_SIZE - 2;
                }
                dataPos -= TILE_SIZE;

                if (tilesetGFXData[dataPos] > 0)
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                else
                    texBuffer[bufPos] = 0;
                bufPos++;

                for (int l = 0; l < TILE_SIZE - 1; l++) {
                    if (tilesetGFXData[dataPos] > 0)
                        texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    else
                        texBuffer[bufPos] = 0;
                    bufPos++;
                    dataPos++;
                }

                if (tilesetGFXData[dataPos] > 0) {
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                    bufPos++;
                    texBuffer[bufPos] = fullPalette[texPaletteNum][tilesetGFXData[dataPos]];
                }
                else {
                    texBuffer[bufPos] = 0;
                    bufPos++;
                    texBuffer[bufPos] = 0;
                }
                bufPos++;
                bufPos += HW_TEXTURE_SIZE - TILE_SIZE - 2;
            }
        }
    }

    int bufPos = 0;
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            PACK_RGB888(texBuffer[bufPos], 0xFF, 0xFF, 0xFF);
            texBuffer[bufPos] |= 1;
            bufPos++;
        }
        bufPos += HW_TEXTURE_SIZE - TILE_SIZE;
    }
}
void UpdateTextureBufferWithSortedSprites()
{
    byte sortedSurfaceCount = 0;
    byte sortedSurfaceList[SURFACE_COUNT];

    for (int i = 0; i < SURFACE_COUNT; i++) gfxSurface[i].texStartX = -1;

    // sort surfaces
    for (int i = 0; i < SURFACE_COUNT; i++) {
        int gfxSize  = 0;
        sbyte surfID = -1;
        for (int s = 0; s < SURFACE_COUNT; s++) {
            GFXSurface *surface = &gfxSurface[s];
            if (StrLength(surface->fileName) && surface->texStartX == -1) {
                if (CheckSurfaceSize(surface->width) && CheckSurfaceSize(surface->height)) {
                    if (surface->width + surface->height > gfxSize) {
                        gfxSize = surface->width + surface->height;
                        surfID  = s;
                    }
                }
                else {
                    surface->texStartX = 0;
                }
            }
        }

        if (surfID == -1) {
            i = SURFACE_COUNT;
        }
        else {
            gfxSurface[surfID].texStartX = 0;
            sortedSurfaceList[sortedSurfaceCount++]          = surfID;
        }
    }

    for (int i = 0; i < SURFACE_COUNT; i++) gfxSurface[i].texStartX = -1;

    bool flag = true;
    for (int i = 0; i < sortedSurfaceCount; i++) {
        GFXSurface *sortedSurface = &gfxSurface[sortedSurfaceList[i]];
        sortedSurface->texStartX  = 0;
        sortedSurface->texStartY  = 0;

        int storeTexX = 0;
        int storeTexY = 0;

        bool inLoop          = true;
        while (inLoop) {
            inLoop = false;
            if (sortedSurface->height == HW_TEXTURE_SIZE)
                flag = false;

            if (flag) {
                bool checkSort = true;
                if (sortedSurface->texStartX < 512 && sortedSurface->texStartY < 512) {
                    inLoop = true;

                    sortedSurface->texStartX += sortedSurface->width;
                    if (sortedSurface->texStartX + sortedSurface->width > HW_TEXTURE_SIZE) {
                        sortedSurface->texStartX = 0;
                        sortedSurface->texStartY += sortedSurface->height;
                    }

                    checkSort = i > 0;
                    if (i) {
                        for (int s = 0; i > s; ++s) {
                            GFXSurface *surface = &gfxSurface[sortedSurfaceList[s]];

                            int width  = abs(sortedSurface->texStartX - surface->texStartX);
                            int height = abs(sortedSurface->texStartY - surface->texStartY);
                            if (sortedSurface->width > width && sortedSurface->height > height && surface->width > width && surface->height > height) {
                                checkSort = false;
                                break;
                            }
                        }
                    }

                    if (checkSort) {
                        storeTexX = sortedSurface->texStartX;
                        storeTexY = sortedSurface->texStartY;
                    }
                }

                if (checkSort) {
                    for (int s = 0; s < SURFACE_COUNT; s++) {
                        GFXSurface *surface = &gfxSurface[s];
                        if (surface->texStartX > -1 && s != sortedSurfaceList[i] && sortedSurface->texStartX < surface->texStartX + surface->width
                            && sortedSurface->texStartX >= surface->texStartX && sortedSurface->texStartY < surface->texStartY + surface->height) {
                            inLoop = true;

                            sortedSurface->texStartX += sortedSurface->width;
                            if (sortedSurface->texStartX + sortedSurface->width > HW_TEXTURE_SIZE) {
                                sortedSurface->texStartX = 0;
                                sortedSurface->texStartY += sortedSurface->height;
                            }
                            break;
                        }
                    }
                }
            }
            else {
                if (sortedSurface->width < HW_TEXTURE_SIZE) {
                    bool checkSort = true;
                    if (sortedSurface->texStartX < 16 && sortedSurface->texStartY < 16) {
                        inLoop = true;

                        sortedSurface->texStartX += sortedSurface->width;
                        if (sortedSurface->texStartX + sortedSurface->width > HW_TEXTURE_SIZE) {
                            sortedSurface->texStartX = 0;
                            sortedSurface->texStartY += sortedSurface->height;
                        }

                        checkSort = i > 0;
                        if (i) {
                            for (int s = 0; i > s; ++s) {
                                GFXSurface *surface = &gfxSurface[sortedSurfaceList[s]];

                                int width  = abs(sortedSurface->texStartX - surface->texStartX);
                                int height = abs(sortedSurface->texStartY - surface->texStartY);
                                if (sortedSurface->width > width && sortedSurface->height > height && surface->width > width && surface->height > height) {
                                    checkSort = false;
                                    break;
                                }
                            }
                        }

                        if (checkSort) {
                            storeTexX = sortedSurface->texStartX;
                            storeTexY = sortedSurface->texStartY;
                        }
                    }

                    if (checkSort) {
                        for (int s = 0; s < SURFACE_COUNT; s++) {
                            GFXSurface *surface = &gfxSurface[s];
                            if (surface->texStartX > -1 && s != sortedSurfaceList[i] && sortedSurface->texStartX < surface->texStartX + surface->width
                                && sortedSurface->texStartX >= surface->texStartX && sortedSurface->texStartY < surface->texStartY + surface->height) {
                                inLoop = true;
                                sortedSurface->texStartX += sortedSurface->width;
                                if (sortedSurface->texStartX + sortedSurface->width > HW_TEXTURE_SIZE) {
                                    sortedSurface->texStartX = 0;
                                    sortedSurface->texStartY += sortedSurface->height;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // sega forever hack, basically panic prevention, will allow the game to override the tileset stuff to store more spritesheets (used in the menu)
        if (sortedSurface->texStartX >= HW_TEXTURE_SIZE || sortedSurface->texStartY >= HW_TEXTURE_SIZE) {
            sortedSurface->texStartX = storeTexX;
            sortedSurface->texStartY = storeTexY;
        }

        if (sortedSurface->texStartY + sortedSurface->height <= HW_TEXTURE_SIZE) {
            int gfxPos  = sortedSurface->dataPosition;
            int dataPos = sortedSurface->texStartX + (sortedSurface->texStartY * HW_TEXTURE_SIZE);
            for (int h = 0; h < sortedSurface->height; h++) {
                for (int w = 0; w < sortedSurface->width; w++) {
                    if (graphicData[gfxPos] > 0)
                        texBuffer[dataPos] = fullPalette[texPaletteNum][graphicData[gfxPos]];
                    else
                        texBuffer[dataPos] = 0;
                    dataPos++;
                    gfxPos++;
                }
                dataPos += HW_TEXTURE_SIZE - sortedSurface->width;
            }
        }
    }
}
void UpdateTextureBufferWithSprites()
{
    for (int i = 0; i < SURFACE_COUNT; ++i) {
        if (gfxSurface[i].texStartY + gfxSurface[i].height <= HW_TEXTURE_SIZE && gfxSurface[i].texStartX > -1) {
            int gfxPos = gfxSurface[i].dataPosition;
            int texPos = gfxSurface[i].texStartX + (gfxSurface[i].texStartY * HW_TEXTURE_SIZE);
            for (int y = 0; y < gfxSurface[i].height; y++) {
                for (int x = 0; x < gfxSurface[i].width; x++) {
                    if (graphicData[gfxPos] > 0)
                        texBuffer[texPos] = fullPalette[texPaletteNum][graphicData[gfxPos]];
                    else
                        texBuffer[texPos] = 0;

                    texPos++;
                    gfxPos++;
                }
                texPos += HW_TEXTURE_SIZE - gfxSurface[i].width;
            }
        }
    }
}

void DrawObjectList(int Layer)
{
    int size = drawListEntries[Layer].listSize;
    for (int i = 0; i < size; ++i) {
        objectLoop = drawListEntries[Layer].entityRefs[i];
        int type   = objectEntityList[objectLoop].type;

        if (disableTouchControls && activeStageList == STAGELIST_SPECIAL) {
            if (StrComp(typeNames[type], "TouchControls"))
                type = OBJ_TYPE_BLANKOBJECT;
        }

        if (type) {
            activePlayer = 0;
            if (scriptCode[objectScriptList[type].subDraw.scriptCodePtr] > 0)
                ProcessScript(objectScriptList[type].subDraw.scriptCodePtr, objectScriptList[type].subDraw.jumpTablePtr, SUB_DRAW);
        }
    }
}
void DrawStageGFX()
{
    waterDrawPos = waterLevel - yScrollOffset;

    if (renderType == RENDER_SW) {
        if (waterDrawPos < 0)
            waterDrawPos = 0;

        if (waterDrawPos > SCREEN_YSIZE)
            waterDrawPos = SCREEN_YSIZE;
    }
    else if (renderType == RENDER_HW) {
        gfxVertexSize = 0;
        gfxIndexSize  = 0;

        if (waterDrawPos < -TILE_SIZE)
            waterDrawPos = -TILE_SIZE;
        if (waterDrawPos >= SCREEN_YSIZE)
            waterDrawPos = SCREEN_YSIZE + TILE_SIZE;
    }

    DrawObjectList(0);
    if (activeTileLayers[0] < LAYER_COUNT) {
        switch (stageLayouts[activeTileLayers[0]].type) {
            case LAYER_HSCROLL: DrawHLineScrollLayer(0); break;
            case LAYER_VSCROLL: DrawVLineScrollLayer(0); break;
            case LAYER_3DFLOOR:
                drawStageGFXHQ = false;
                Draw3DFloorLayer(0);
                break;
            case LAYER_3DSKY:
                if (Engine.useHQModes)
                    drawStageGFXHQ = true;
                if (renderType == RENDER_SW)
                    Draw3DSkyLayer(0);
                else
                    Draw3DFloorLayer(0);
                break;
            default: break;
        }
    }

    if (renderType == RENDER_HW) {
        gfxIndexSizeOpaque  = gfxIndexSize;
        gfxVertexSizeOpaque = gfxVertexSize;
    }

    DrawObjectList(1);
    if (activeTileLayers[1] < LAYER_COUNT) {
        switch (stageLayouts[activeTileLayers[1]].type) {
            case LAYER_HSCROLL: DrawHLineScrollLayer(1); break;
            case LAYER_VSCROLL: DrawVLineScrollLayer(1); break;
            case LAYER_3DFLOOR:
                drawStageGFXHQ = false;
                Draw3DFloorLayer(1);
                break;
            case LAYER_3DSKY:
                if (Engine.useHQModes)
                    drawStageGFXHQ = true;
                if (renderType == RENDER_SW)
                    Draw3DSkyLayer(1);
                else
                    Draw3DFloorLayer(1);
                break;
            default: break;
        }
    }

    DrawObjectList(2);
    if (activeTileLayers[2] < LAYER_COUNT) {
        switch (stageLayouts[activeTileLayers[2]].type) {
            case LAYER_HSCROLL: DrawHLineScrollLayer(2); break;
            case LAYER_VSCROLL: DrawVLineScrollLayer(2); break;
            case LAYER_3DFLOOR:
                drawStageGFXHQ = false;
                Draw3DFloorLayer(2);
                break;
            case LAYER_3DSKY:
                if (Engine.useHQModes)
                    drawStageGFXHQ = true;
                if (renderType == RENDER_SW)
                    Draw3DSkyLayer(2);
                else
                    Draw3DFloorLayer(2);
                break;
            default: break;
        }
    }

    DrawObjectList(3);
    DrawObjectList(4);
    if (activeTileLayers[3] < LAYER_COUNT) {
        switch (stageLayouts[activeTileLayers[3]].type) {
            case LAYER_HSCROLL: DrawHLineScrollLayer(3); break;
            case LAYER_VSCROLL: DrawVLineScrollLayer(3); break;
            case LAYER_3DFLOOR:
                drawStageGFXHQ = false;
                Draw3DFloorLayer(3);
                break;
            case LAYER_3DSKY:
                if (Engine.useHQModes)
                    drawStageGFXHQ = true;
                if (renderType == RENDER_HW)
                    Draw3DSkyLayer(3);
                else
                    Draw3DFloorLayer(3);
                break;
            default: break;
        }
    }

    DrawObjectList(5);
#if !RETRO_USE_ORIGINAL_CODE
    // Hacky fix for Tails Object not working properly on non-Origins bytecode
    if (forceUseScripts || GetGlobalVariableByName("NOTIFY_1P_VS_SELECT") != 0)
#endif
        DrawObjectList(7); // Extra Origins draw list (who knows why it comes before 6)
    DrawObjectList(6);

#if !RETRO_USE_ORIGINAL_CODE
    if (drawStageGFXHQ)
        DrawDebugOverlays();
#endif

    if (drawStageGFXHQ && renderType == RENDER_SW) {
        CopyFrameOverlay2x();
        if (fadeMode > 0) {
            DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeA);
            SetFadeHQ(fadeR, fadeG, fadeB, fadeA);
        }
    }
    else if (fadeMode > 0) {
        DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeA);
    }

#if !RETRO_USE_ORIGINAL_CODE
    if (!drawStageGFXHQ)
        DrawDebugOverlays();
#endif
}

#if !RETRO_USE_ORIGINAL_CODE
void DrawDebugOverlays()
{
    if (showHitboxes) {
        for (int i = 0; i < debugHitboxCount; ++i) {
            DebugHitboxInfo *info = &debugHitboxList[i];
            int x                 = info->XPos + (info->left << 16);
            int y                 = info->YPos + (info->top << 16);
            int w                 = abs((info->XPos + (info->right << 16)) - x) >> 16;
            int h                 = abs((info->YPos + (info->bottom << 16)) - y) >> 16;
            x                     = (x >> 16) - xScrollOffset;
            y                     = (y >> 16) - yScrollOffset;

            switch (info->type) {
                case H_TYPE_TOUCH:
                    if (showHitboxes & 1)
                        DrawRectangle(x, y, w, h, info->collision ? 0x80 : 0xFF, info->collision ? 0x80 : 0x00, 0x00, 0x60);
                    break;

                case H_TYPE_BOX:
                    if (showHitboxes & 1) {
                        DrawRectangle(x, y, w, h, 0x00, 0x00, 0xFF, 0x60);
                        if (info->collision & 1) // top
                            DrawRectangle(x, y, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 8) // bottom
                            DrawRectangle(x, y + h, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 2) { // left
                            int sy = y;
                            int sh = h;
                            if (info->collision & 1) {
                                sy++;
                                sh--;
                            }
                            if (info->collision & 8)
                                sh--;
                            DrawRectangle(x, sy, 1, sh, 0xFF, 0xFF, 0x00, 0xC0);
                        }
                        if (info->collision & 4) { // right
                            int sy = y;
                            int sh = h;
                            if (info->collision & 1) {
                                sy++;
                                sh--;
                            }
                            if (info->collision & 8)
                                sh--;
                            DrawRectangle(x + w, sy, 1, sh, 0xFF, 0xFF, 0x00, 0xC0);
                        }
                    }
                    break;

                case H_TYPE_PLAT:
                    if (showHitboxes & 1) {
                        DrawRectangle(x, y, w, h, 0x00, 0xFF, 0x00, 0x60);
                        if (info->collision & 1) // top
                            DrawRectangle(x, y, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 8) // bottom
                            DrawRectangle(x, y + h, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                    }
                    break;

                case H_TYPE_FINGER:
                    if (showHitboxes & 2)
                        DrawRectangle(x + xScrollOffset, y + yScrollOffset, w, h, 0xF0, 0x00, 0xF0, 0x60);
                    break;

                case H_TYPE_HAMMER:
                    if (showHitboxes & 1)
                        DrawRectangle(x, y, w, h, info->collision ? 0xA0 : 0xFF, info->collision ? 0xA0 : 0xFF, 0x00, 0x60);
                    break;
            }
        }
    }

    if (Engine.showPaletteOverlay) {
        for (int p = 0; p < PALETTE_COUNT; ++p) {
            int x = (SCREEN_XSIZE - (0x10 << 3));
            int y = (SCREEN_YSIZE - (0x10 << 2));
            for (int c = 0; c < PALETTE_SIZE; ++c) {
                int g = fullPalette32[p][c].g;
                // HQ mode overrides any magenta px, so slightly change the g channel since it has the most bits to make it "not quite magenta"
                if (drawStageGFXHQ && fullPalette32[p][c].r == 0xFF && fullPalette32[p][c].g == 0x00 && fullPalette32[p][c].b == 0xFF)
                    g += 8;

                DrawRectangle(x + ((c & 0xF) << 1) + ((p % (PALETTE_COUNT / 2)) * (2 * 16)),
                              y + ((c >> 4) << 1) + ((p / (PALETTE_COUNT / 2)) * (2 * 16)), 2, 2, fullPalette32[p][c].r, g, fullPalette32[p][c].b,
                              0xFF);
            }
        }
    }
}
#endif

void DrawHLineScrollLayer(int layerID)
{
    if (renderType == RENDER_SW) {
        TileLayer *layer   = &stageLayouts[activeTileLayers[layerID]];
        int screenwidth16  = (GFX_LINESIZE >> 4) - 1;
        int layerwidth     = layer->xsize;
        int layerheight    = layer->ysize;
        bool aboveMidPoint = layerID >= tLayerMidPoint;

        byte *lineScroll;
        int *deformationData;
        int *deformationDataW;

        int yscrollOffset = 0;
        if (activeTileLayers[layerID]) { // BG Layer
            int yScroll    = yScrollOffset * layer->parallaxFactor >> 8;
            int fullheight = layerheight << 7;
            layer->scrollPos += layer->scrollSpeed;
            if (layer->scrollPos > fullheight << 16)
                layer->scrollPos -= fullheight << 16;
            yscrollOffset    = (yScroll + (layer->scrollPos >> 16)) % fullheight;
            layerheight      = fullheight >> 7;
            lineScroll       = layer->lineScroll;
            deformationData  = &bgDeformationData2[(byte)(yscrollOffset + layer->deformationOffset)];
            deformationDataW = &bgDeformationData3[(byte)(yscrollOffset + waterDrawPos + layer->deformationOffsetW)];
        }
        else { // FG Layer
            lastXSize     = layer->xsize;
            yscrollOffset = yScrollOffset;
            lineScroll    = layer->lineScroll;
            for (int i = 0; i < PARALLAX_COUNT; ++i) hParallax.linePos[i] = xScrollOffset;
            deformationData  = &bgDeformationData0[(byte)(yscrollOffset + layer->deformationOffset)];
            deformationDataW = &bgDeformationData1[(byte)(yscrollOffset + waterDrawPos + layer->deformationOffsetW)];
        }

        if (layer->type == LAYER_HSCROLL) {
            if (lastXSize != layerwidth) {
                int fullLayerwidth = layerwidth << 7;
                for (int i = 0; i < hParallax.entryCount; ++i) {
                    hParallax.linePos[i] = xScrollOffset * hParallax.parallaxFactor[i] >> 8;
                    hParallax.scrollPos[i] += hParallax.scrollSpeed[i];
                    if (hParallax.scrollPos[i] > fullLayerwidth << 16)
                        hParallax.scrollPos[i] -= fullLayerwidth << 16;
                    if (hParallax.scrollPos[i] < 0)
                        hParallax.scrollPos[i] += fullLayerwidth << 16;
                    hParallax.linePos[i] += hParallax.scrollPos[i] >> 16;
                    hParallax.linePos[i] %= fullLayerwidth;
                }
            }
            lastXSize = layerwidth;
        }

        ushort *frameBufferPtr = Engine.frameBuffer;
        byte *lineBuffer       = gfxLineBuffer;
        int tileYPos           = yscrollOffset % (layerheight << 7);
        if (tileYPos < 0)
            tileYPos += layerheight << 7;
        byte *scrollIndex = &lineScroll[tileYPos];
        int tileY16       = tileYPos & 0xF;
        int chunkY        = tileYPos >> 7;
        int tileY         = (tileYPos & 0x7F) >> 4;

        // Draw Above Water (if applicable)
        int drawableLines[2] = { waterDrawPos, SCREEN_YSIZE - waterDrawPos };
        for (int i = 0; i < 2; ++i) {
            while (drawableLines[i]--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int chunkX = hParallax.linePos[*scrollIndex];
                if (i == 0) {
                    int deform = 0;
                    if (hParallax.deform[*scrollIndex])
                        deform = *deformationData;

                    // Fix for SS5 mobile bug
                    if (StrComp(stageList[activeStageList][stageListPosition].name, "5") && activeStageList == STAGELIST_SPECIAL
                        && renderType == RENDER_HW)
                        deform >>= 4;

                    chunkX += deform;
                    ++deformationData;
                }
                else {
                    if (hParallax.deform[*scrollIndex])
                        chunkX += *deformationDataW;
                    ++deformationDataW;
                }
                ++scrollIndex;
                int fullLayerwidth = layerwidth << 7;
                if (chunkX < 0)
                    chunkX += fullLayerwidth;
                if (chunkX >= fullLayerwidth)
                    chunkX -= fullLayerwidth;
                int chunkXPos         = chunkX >> 7;
                int tilePxXPos        = chunkX & 0xF;
                int tileXPxRemain     = TILE_SIZE - tilePxXPos;
                int chunk             = (layer->tiles[(chunkX >> 7) + (chunkY << 8)] << 6) + ((chunkX & 0x7F) >> 4) + 8 * tileY;
                int tileOffsetY       = TILE_SIZE * tileY16;
                int tileOffsetYFlipX  = TILE_SIZE * tileY16 + 0xF;
                int tileOffsetYFlipY  = TILE_SIZE * (0xF - tileY16);
                int tileOffsetYFlipXY = TILE_SIZE * (0xF - tileY16) + 0xF;
                int lineRemain        = GFX_LINESIZE;

                byte *gfxDataPtr  = NULL;
                int tilePxLineCnt = 0;

                // Draw the first tile to the left
                if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                    tilePxLineCnt = TILE_SIZE - tilePxXPos;
                    lineRemain -= tilePxLineCnt;
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE:
                            gfxDataPtr = &tilesetGFXData[tileOffsetY + tiles128x128.gfxDataPos[chunk] + tilePxXPos];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                            }
                            break;
                        case FLIP_X:
                            gfxDataPtr = &tilesetGFXData[tileOffsetYFlipX + tiles128x128.gfxDataPos[chunk] - tilePxXPos];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                            }
                            break;
                        case FLIP_Y:
                            gfxDataPtr = &tilesetGFXData[tileOffsetYFlipY + tiles128x128.gfxDataPos[chunk] + tilePxXPos];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                            }
                            break;
                        case FLIP_XY:
                            gfxDataPtr = &tilesetGFXData[tileOffsetYFlipXY + tiles128x128.gfxDataPos[chunk] - tilePxXPos];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                            }
                            break;
                        default: break;
                    }
                }
                else {
                    frameBufferPtr += tileXPxRemain;
                    lineRemain -= tileXPxRemain;
                }

                // Draw the bulk of the tiles
                int chunkTileX   = ((chunkX & 0x7F) >> 4) + 1;
                int tilesPerLine = screenwidth16;
                while (tilesPerLine--) {
                    if (chunkTileX <= 7) {
                        ++chunk;
                    }
                    else {
                        if (++chunkXPos == layerwidth)
                            chunkXPos = 0;
                        chunkTileX = 0;
                        chunk      = (layer->tiles[chunkXPos + (chunkY << 8)] << 6) + 8 * tileY;
                    }
                    lineRemain -= TILE_SIZE;

                    // Loop Unrolling (faster but messier code)
                    if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                        switch (tiles128x128.direction[chunk]) {
                            case FLIP_NONE:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetY];
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                break;
                            case FLIP_X:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipX];
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                break;
                            case FLIP_Y:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipY];
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                                break;
                            case FLIP_XY:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipXY];
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                                break;
                        }
                    }
                    else {
                        frameBufferPtr += 0x10;
                    }
                    ++chunkTileX;
                }

                // Draw any remaining tiles
                while (lineRemain > 0) {
                    if (chunkTileX++ <= 7) {
                        ++chunk;
                    }
                    else {
                        chunkTileX = 0;
                        if (++chunkXPos == layerwidth)
                            chunkXPos = 0;
                        chunk = (layer->tiles[chunkXPos + (chunkY << 8)] << 6) + 8 * tileY;
                    }

                    tilePxLineCnt = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;
                    lineRemain -= tilePxLineCnt;
                    if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                        switch (tiles128x128.direction[chunk]) {
                            case FLIP_NONE:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetY];
                                while (tilePxLineCnt--) {
                                    if (*gfxDataPtr > 0)
                                        *frameBufferPtr = activePalette[*gfxDataPtr];
                                    ++frameBufferPtr;
                                    ++gfxDataPtr;
                                }
                                break;
                            case FLIP_X:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipX];
                                while (tilePxLineCnt--) {
                                    if (*gfxDataPtr > 0)
                                        *frameBufferPtr = activePalette[*gfxDataPtr];
                                    ++frameBufferPtr;
                                    --gfxDataPtr;
                                }
                                break;
                            case FLIP_Y:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipY];
                                while (tilePxLineCnt--) {
                                    if (*gfxDataPtr > 0)
                                        *frameBufferPtr = activePalette[*gfxDataPtr];
                                    ++frameBufferPtr;
                                    ++gfxDataPtr;
                                }
                                break;
                            case FLIP_XY:
                                gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipXY];
                                while (tilePxLineCnt--) {
                                    if (*gfxDataPtr > 0)
                                        *frameBufferPtr = activePalette[*gfxDataPtr];
                                    ++frameBufferPtr;
                                    --gfxDataPtr;
                                }
                                break;
                            default: break;
                        }
                    }
                    else {
                        frameBufferPtr += tilePxLineCnt;
                    }
                }

                if (++tileY16 > TILE_SIZE - 1) {
                    tileY16 = 0;
                    ++tileY;
                }
                if (tileY > 7) {
                    if (++chunkY == layerheight) {
                        chunkY = 0;
                        scrollIndex -= 0x80 * layerheight;
                    }
                    tileY = 0;
                }
            }
        }
    }
    else if (renderType == RENDER_HW) {
        TileLayer *layer      = &stageLayouts[activeTileLayers[layerID]];
        byte *lineScrollPtr   = NULL;
        int chunkPosX         = 0;
        int chunkTileX        = 0;
        int gfxIndex          = 0;
        int yscrollOffset     = 0;
        int tileGFXPos        = 0;
        int deformX1          = 0;
        int deformX2          = 0;
        byte highPlane        = layerID >= tLayerMidPoint;
        int *deformationData  = NULL;
        int *deformationDataW = NULL;
        int deformOffset      = 0;
        int deformOffsetW     = 0;
        int lineID            = 0;
        int layerWidth        = layer->xsize;
        int layerHeight       = layer->ysize;
        int renderWidth       = (GFX_LINESIZE >> 4) + 3;
        bool flag             = false;

        if (activeTileLayers[layerID]) {
            layer            = &stageLayouts[activeTileLayers[layerID]];
            yscrollOffset    = layer->parallaxFactor * yScrollOffset >> 8;
            layerHeight      = layerHeight << 7;
            layer->scrollPos = layer->scrollPos + layer->scrollSpeed;
            if (layer->scrollPos > layerHeight << 16) {
                layer->scrollPos -= (layerHeight << 16);
            }
            yscrollOffset += (layer->scrollPos >> 16);
            yscrollOffset %= layerHeight;

            layerHeight      = layerHeight >> 7;
            lineScrollPtr    = layer->lineScroll;
            deformOffset     = (byte)(layer->deformationOffset + yscrollOffset);
            deformOffsetW    = (byte)(layer->deformationOffsetW + yscrollOffset);
            deformationData  = bgDeformationData2;
            deformationDataW = bgDeformationData3;
        }
        else {
            layer                = &stageLayouts[0];
            lastXSize            = layerWidth;
            yscrollOffset        = yScrollOffset;
            lineScrollPtr        = layer->lineScroll;
            hParallax.linePos[0] = xScrollOffset;
            deformOffset         = (byte)(stageLayouts[0].deformationOffset + yscrollOffset);
            deformOffsetW        = (byte)(stageLayouts[0].deformationOffsetW + yscrollOffset);
            deformationData      = bgDeformationData0;
            deformationDataW     = bgDeformationData1;
            yscrollOffset %= (layerHeight << 7);
        }

        if (layer->type == LAYER_HSCROLL) {
            if (lastXSize != layerWidth) {
                layerWidth = layerWidth << 7;
                for (int i = 0; i < hParallax.entryCount; i++) {
                    hParallax.linePos[i]   = hParallax.parallaxFactor[i] * xScrollOffset >> 8;
                    hParallax.scrollPos[i] = hParallax.scrollPos[i] + hParallax.scrollSpeed[i];
                    if (hParallax.scrollPos[i] > layerWidth << 16) {
                        hParallax.scrollPos[i] = hParallax.scrollPos[i] - (layerWidth << 16);
                    }
                    hParallax.linePos[i] = hParallax.linePos[i] + (hParallax.scrollPos[i] >> 16);
                    hParallax.linePos[i] = hParallax.linePos[i] % layerWidth;
                }
                layerWidth = layerWidth >> 7;
            }
            lastXSize = layerWidth;
        }

        if (yscrollOffset < 0)
            yscrollOffset += (layerHeight << 7);

        int deformY = yscrollOffset >> 4 << 4;
        lineID += deformY;
        deformOffset += (deformY - yscrollOffset);
        deformOffsetW += (deformY - yscrollOffset);

        if (deformOffset < 0)
            deformOffset += 0x100;
        if (deformOffsetW < 0)
            deformOffsetW += 0x100;

        deformY        = -(yscrollOffset & 15);
        int chunkPosY  = yscrollOffset >> 7;
        int chunkTileY = (yscrollOffset & 127) >> 4;
        waterDrawPos <<= 4;
        deformY <<= 4;
        for (int j = (deformY ? 0x110 : 0x100); j > 0; j -= 16) {
            int parallaxLinePos = hParallax.linePos[lineScrollPtr[lineID]] - 16;
            lineID += 8;

            if (parallaxLinePos == hParallax.linePos[lineScrollPtr[lineID]] - 16) {
                if (hParallax.deform[lineScrollPtr[lineID]]) {
                    deformX1 = deformY < waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                    deformX2 = (deformY + 64) <= waterDrawPos ? deformationData[deformOffset + 8] : deformationDataW[deformOffsetW + 8];
                    flag     = deformX1 != deformX2;
                }
                else {
                    flag = false;
                }
            }
            else {
                flag = true;
            }

            lineID -= 8;
            if (flag) {
                if (parallaxLinePos < 0)
                    parallaxLinePos += layerWidth << 7;
                if (parallaxLinePos >= layerWidth << 7)
                    parallaxLinePos -= layerWidth << 7;

                chunkPosX  = parallaxLinePos >> 7;
                chunkTileX = (parallaxLinePos & 0x7F) >> 4;
                deformX1   = -((parallaxLinePos & 0xF) << 4);
                deformX1 -= 0x100;
                deformX2 = deformX1;
                if (hParallax.deform[lineScrollPtr[lineID]]) {
                    deformX1 -= deformY < waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                    deformOffset += 8;
                    deformOffsetW += 8;
                    deformX2 -= (deformY + 64) <= waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                }
                else {
                    deformOffset += 8;
                    deformOffsetW += 8;
                }
                lineID += 8;

                gfxIndex = (chunkPosX > -1 && chunkPosY > -1) ? (layer->tiles[chunkPosX + (chunkPosY << 8)] << 6) : 0;
                gfxIndex += chunkTileX + (chunkTileY << 3);
                for (int i = renderWidth; i > 0; i--) {
                    if (tiles128x128.visualPlane[gfxIndex] == highPlane && tiles128x128.gfxDataPos[gfxIndex] > 0) {
                        tileGFXPos = 0;
                        switch (tiles128x128.direction[gfxIndex]) {
                            case FLIP_NONE: {
                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] - 8;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_X: {
                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] - 8;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_Y: {
                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] + 8;
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_XY: {
                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] + 8;
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                        }
                    }

                    deformX1 += (CHUNK_SIZE * 2);
                    deformX2 += (CHUNK_SIZE * 2);
                    if (++chunkTileX < 8) {
                        gfxIndex++;
                    }
                    else {
                        if (++chunkPosX == layerWidth)
                            chunkPosX = 0;

                        chunkTileX = 0;
                        gfxIndex   = layer->tiles[chunkPosX + (chunkPosY << 8)] << 6;
                        gfxIndex += chunkTileX + (chunkTileY << 3);
                    }
                }
                deformY += CHUNK_SIZE;
                parallaxLinePos = hParallax.linePos[lineScrollPtr[lineID]] - 16;

                if (parallaxLinePos < 0)
                    parallaxLinePos += layerWidth << 7;
                if (parallaxLinePos >= layerWidth << 7)
                    parallaxLinePos -= layerWidth << 7;

                chunkPosX  = parallaxLinePos >> 7;
                chunkTileX = (parallaxLinePos & 127) >> 4;
                deformX1   = -((parallaxLinePos & 15) << 4);
                deformX1 -= 0x100;
                deformX2 = deformX1;
                if (!hParallax.deform[lineScrollPtr[lineID]]) {
                    deformOffset += 8;
                    deformOffsetW += 8;
                }
                else {
                    deformX1 -= deformY < waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                    deformOffset += 8;
                    deformOffsetW += 8;
                    deformX2 -= (deformY + 64) <= waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                }

                lineID += 8;
                gfxIndex = (chunkPosX > -1 && chunkPosY > -1) ? (layer->tiles[chunkPosX + (chunkPosY << 8)] << 6) : 0;
                gfxIndex += chunkTileX + (chunkTileY << 3);
                for (int i = renderWidth; i > 0; i--) {
                    if (tiles128x128.visualPlane[gfxIndex] == highPlane && tiles128x128.gfxDataPos[gfxIndex] > 0) {
                        tileGFXPos = 0;
                        switch (tiles128x128.direction[gfxIndex]) {
                            case FLIP_NONE: {
                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] + 8;
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_X: {
                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] + 8;
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_Y: {
                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] - 8;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_XY: {
                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + CHUNK_SIZE;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos] - 8;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                        }
                    }

                    deformX1 += (CHUNK_SIZE * 2);
                    deformX2 += (CHUNK_SIZE * 2);

                    if (++chunkTileX < 8) {
                        gfxIndex++;
                    }
                    else {
                        if (++chunkPosX == layerWidth) {
                            chunkPosX = 0;
                        }
                        chunkTileX = 0;
                        gfxIndex   = layer->tiles[chunkPosX + (chunkPosY << 8)] << 6;
                        gfxIndex += chunkTileX + (chunkTileY << 3);
                    }
                }
                deformY += CHUNK_SIZE;
            }
            else {
                if (parallaxLinePos < 0)
                    parallaxLinePos += layerWidth << 7;
                if (parallaxLinePos >= layerWidth << 7)
                    parallaxLinePos -= layerWidth << 7;

                chunkPosX  = parallaxLinePos >> 7;
                chunkTileX = (parallaxLinePos & 0x7F) >> 4;
                deformX1   = -((parallaxLinePos & 0xF) << 4);
                deformX1 -= 0x100;
                deformX2 = deformX1;

                if (hParallax.deform[lineScrollPtr[lineID]]) {
                    deformX1 -= deformY < waterDrawPos ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                    deformOffset += 16;
                    deformOffsetW += 16;
                    deformX2 -= (deformY + CHUNK_SIZE <= waterDrawPos) ? deformationData[deformOffset] : deformationDataW[deformOffsetW];
                }
                else {
                    deformOffset += 16;
                    deformOffsetW += 16;
                }
                lineID += 16;

                gfxIndex = (chunkPosX > -1 && chunkPosY > -1) ? (layer->tiles[chunkPosX + (chunkPosY << 8)] << 6) : 0;
                gfxIndex += chunkTileX + (chunkTileY << 3);
                for (int i = renderWidth; i > 0; i--) {
                    if (tiles128x128.visualPlane[gfxIndex] == highPlane && tiles128x128.gfxDataPos[gfxIndex] > 0) {
                        tileGFXPos = 0;
                        switch (tiles128x128.direction[gfxIndex]) {
                            case FLIP_NONE: {
                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_X: {
                                gfxPolyList[gfxVertexSize].x = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX1;
                                gfxPolyList[gfxVertexSize].y = deformY;
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX2;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_Y: {
                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                            case FLIP_XY: {
                                gfxPolyList[gfxVertexSize].x = deformX2 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x = deformX2;
                                gfxPolyList[gfxVertexSize].y = deformY + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].u = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                tileGFXPos++;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1 + (CHUNK_SIZE * 2);
                                gfxPolyList[gfxVertexSize].y        = deformY;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = tileUVArray[tiles128x128.gfxDataPos[gfxIndex] + tileGFXPos];
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxPolyList[gfxVertexSize].x        = deformX1;
                                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                                gfxVertexSize++;

                                gfxIndexSize += 6;
                                break;
                            }
                        }
                    }

                    deformX1 += (CHUNK_SIZE * 2);
                    deformX2 += (CHUNK_SIZE * 2);
                    if (++chunkTileX < 8) {
                        gfxIndex++;
                    }
                    else {
                        if (++chunkPosX == layerWidth)
                            chunkPosX = 0;

                        chunkTileX = 0;
                        gfxIndex   = layer->tiles[chunkPosX + (chunkPosY << 8)] << 6;
                        gfxIndex += chunkTileX + (chunkTileY << 3);
                    }
                }
                deformY += CHUNK_SIZE * 2;
            }

            if (++chunkTileY > 7) {
                if (++chunkPosY == layerHeight) {
                    chunkPosY = 0;
                    lineID -= (layerHeight << 7);
                }
                chunkTileY = 0;
            }
        }
        waterDrawPos >>= 4;
    }
}
void DrawVLineScrollLayer(int layerID)
{
    if (renderType == RENDER_SW) {
        TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
        if (!layer->xsize || !layer->ysize)
            return;

        int layerwidth     = layer->xsize;
        int layerheight    = layer->ysize;
        bool aboveMidPoint = layerID >= tLayerMidPoint;

        byte *lineScroll;
        int *deformationData;

        int xscrollOffset = 0;
        if (activeTileLayers[layerID]) { // BG Layer
            int xScroll        = xScrollOffset * layer->parallaxFactor >> 8;
            int fullLayerwidth = layerwidth << 7;
            layer->scrollPos += layer->scrollSpeed;
            if (layer->scrollPos > fullLayerwidth << 16)
                layer->scrollPos -= fullLayerwidth << 16;
            xscrollOffset   = (xScroll + (layer->scrollPos >> 16)) % fullLayerwidth;
            layerwidth      = fullLayerwidth >> 7;
            lineScroll      = layer->lineScroll;
            deformationData = &bgDeformationData2[(byte)(xscrollOffset + layer->deformationOffset)];
        }
        else { // FG Layer
            lastYSize            = layer->ysize;
            xscrollOffset        = xScrollOffset;
            lineScroll           = layer->lineScroll;
            vParallax.linePos[0] = yScrollOffset;
            vParallax.deform[0]  = true;
            deformationData      = &bgDeformationData0[(byte)(xScrollOffset + layer->deformationOffset)];
        }

        if (layer->type == LAYER_VSCROLL) {
            if (lastYSize != layerheight) {
                int fullLayerheight = layerheight << 7;
                for (int i = 0; i < vParallax.entryCount; ++i) {
                    vParallax.linePos[i] = yScrollOffset * vParallax.parallaxFactor[i] >> 8;

                    vParallax.scrollPos[i] += vParallax.scrollPos[i] << 16;
                    if (vParallax.scrollPos[i] > fullLayerheight << 16)
                        vParallax.scrollPos[i] -= fullLayerheight << 16;

                    vParallax.linePos[i] += vParallax.scrollPos[i] >> 16;
                    vParallax.linePos[i] %= fullLayerheight;
                }
                layerheight = fullLayerheight >> 7;
            }
            lastYSize = layerheight;
        }

        ushort *frameBufferPtr = Engine.frameBuffer;
        activePalette          = fullPalette[gfxLineBuffer[0]];
        activePalette32        = fullPalette32[gfxLineBuffer[0]];
        int tileXPos           = xscrollOffset % (layerheight << 7);
        if (tileXPos < 0)
            tileXPos += layerheight << 7;
        byte *scrollIndex = &lineScroll[tileXPos];
        int chunkX        = tileXPos >> 7;
        int tileX16       = tileXPos & 0xF;
        int tileX         = (tileXPos & 0x7F) >> 4;

        // Draw Above Water (if applicable)
        int drawableLines = GFX_LINESIZE;
        while (drawableLines--) {
            int chunkY = vParallax.linePos[*scrollIndex];
            if (vParallax.deform[*scrollIndex])
                chunkY += *deformationData;
            ++deformationData;
            ++scrollIndex;

            int fullLayerHeight = layerheight << 7;
            if (chunkY < 0)
                chunkY += fullLayerHeight;
            if (chunkY >= fullLayerHeight)
                chunkY -= fullLayerHeight;

            int chunkYPos         = chunkY >> 7;
            int tileY             = chunkY & 0xF;
            int tileYPxRemain     = TILE_SIZE - tileY;
            int chunk             = (layer->tiles[chunkX + (chunkY >> 7 << 8)] << 6) + tileX + 8 * ((chunkY & 0x7F) >> 4);
            int tileOffsetXFlipX  = 0xF - tileX16;
            int tileOffsetXFlipY  = tileX16 + SCREEN_YSIZE;
            int tileOffsetXFlipXY = 0xFF - tileX16;
            int lineRemain        = SCREEN_YSIZE;

            byte *gfxDataPtr  = NULL;
            int tilePxLineCnt = tileYPxRemain;

            // Draw the first tile to the left
            if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                lineRemain -= tilePxLineCnt;
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE:
                        gfxDataPtr = &tilesetGFXData[TILE_SIZE * tileY + tileX16 + tiles128x128.gfxDataPos[chunk]];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;
                        }
                        break;

                    case FLIP_X:
                        gfxDataPtr = &tilesetGFXData[TILE_SIZE * tileY + tileOffsetXFlipX + tiles128x128.gfxDataPos[chunk]];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;
                        }
                        break;

                    case FLIP_Y:
                        gfxDataPtr = &tilesetGFXData[tileOffsetXFlipY + tiles128x128.gfxDataPos[chunk] - TILE_SIZE * tileY];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;
                        }
                        break;

                    case FLIP_XY:
                        gfxDataPtr = &tilesetGFXData[tileOffsetXFlipXY + tiles128x128.gfxDataPos[chunk] - TILE_SIZE * tileY];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;
                        }
                        break;

                    default: break;
                }
            }
            else {
                frameBufferPtr += GFX_LINESIZE * tileYPxRemain;
                lineRemain -= tilePxLineCnt;
            }

            // Draw the bulk of the tiles
            int chunkTileY   = ((chunkY & 0x7F) >> 4) + 1;
            int tilesPerLine = (SCREEN_YSIZE >> 4) - 1;

            while (tilesPerLine--) {
                if (chunkTileY < 8) {
                    chunk += 8;
                }
                else {
                    if (++chunkYPos == layerheight)
                        chunkYPos = 0;

                    chunkTileY = 0;
                    chunk      = (layer->tiles[chunkX + (chunkYPos << 8)] << 6) + tileX;
                }
                lineRemain -= TILE_SIZE;

                // Loop Unrolling (faster but messier code)
                if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileX16];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            break;

                        case FLIP_X:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipX];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            break;

                        case FLIP_Y:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipY];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            break;

                        case FLIP_XY:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipXY];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            break;
                    }
                }
                else {
                    frameBufferPtr += GFX_LINESIZE * TILE_SIZE;
                }
                ++chunkTileY;
            }

            // Draw any remaining tiles
            while (lineRemain > 0) {
                if (chunkTileY < 8) {
                    chunk += 8;
                }
                else {
                    if (++chunkYPos == layerheight)
                        chunkYPos = 0;

                    chunkTileY = 0;
                    chunk      = (layer->tiles[chunkX + (chunkYPos << 8)] << 6) + tileX;
                }

                tilePxLineCnt = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;
                lineRemain -= tilePxLineCnt;

                if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileX16];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                frameBufferPtr += GFX_LINESIZE;
                                gfxDataPtr += TILE_SIZE;
                            }
                            break;

                        case FLIP_X:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipX];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                frameBufferPtr += GFX_LINESIZE;
                                gfxDataPtr += TILE_SIZE;
                            }
                            break;

                        case FLIP_Y:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipY];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                frameBufferPtr += GFX_LINESIZE;
                                gfxDataPtr -= TILE_SIZE;
                            }
                            break;

                        case FLIP_XY:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipXY];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                frameBufferPtr += GFX_LINESIZE;
                                gfxDataPtr -= TILE_SIZE;
                            }
                            break;

                        default: break;
                    }
                }
                else {
                    frameBufferPtr += GFX_LINESIZE * tilePxLineCnt;
                }
                chunkTileY++;
            }

            if (++tileX16 >= TILE_SIZE) {
                tileX16 = 0;
                ++tileX;
            }

            if (tileX >= 8) {
                if (++chunkX == layerwidth) {
                    chunkX = 0;
                    scrollIndex -= 0x80 * layerwidth;
                }
                tileX = 0;
            }

            frameBufferPtr -= GFX_FBUFFERMINUSONE;
        }
    }
    // Not avaliable in HW Render mode
}
void Draw3DFloorLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;

    if (renderType == RENDER_SW) {
        int layerWidth         = layer->xsize << 7;
        int layerHeight        = layer->ysize << 7;
        int layerYPos          = layer->YPos;
        int layerZPos          = layer->ZPos;
        int sinValue           = sinMLookupTable[layer->angle];
        int cosValue           = cosMLookupTable[layer->angle];
        byte *gfxLineBufferPtr = &gfxLineBuffer[((SCREEN_YSIZE / 2) + 12)];
        ushort *frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
        int layerXPos          = layer->XPos >> 4;
        int ZBuffer            = layerZPos >> 4;
        for (int i = 4; i < ((SCREEN_YSIZE / 2) - 8); ++i) {
            if (!(i & 1)) {
                activePalette   = fullPalette[*gfxLineBufferPtr];
                activePalette32 = fullPalette32[*gfxLineBufferPtr];
                gfxLineBufferPtr++;
            }
            int XBuffer    = layerYPos / (i << 9) * -cosValue >> 8;
            int YBuffer    = sinValue * (layerYPos / (i << 9)) >> 8;
            int XPos       = layerXPos + (3 * sinValue * (layerYPos / (i << 9)) >> 2) - XBuffer * SCREEN_CENTERX;
            int YPos       = ZBuffer + (3 * cosValue * (layerYPos / (i << 9)) >> 2) - YBuffer * SCREEN_CENTERX;
            int lineBuffer = 0;
            while (lineBuffer < GFX_LINESIZE) {
                int tileX = XPos >> 12;
                int tileY = YPos >> 12;
                if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                    int chunk       = tile3DFloorBuffer[(YPos >> 16 << 8) + (XPos >> 16)];
                    byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE: tilePixel += 16 * (tileY & 0xF) + (tileX & 0xF); break;
                        case FLIP_X: tilePixel += 16 * (tileY & 0xF) + 15 - (tileX & 0xF); break;
                        case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - 16 * (tileY & 0xF); break;
                        case FLIP_XY: tilePixel += 15 - (tileX & 0xF) + SCREEN_YSIZE - 16 * (tileY & 0xF); break;
                        default: break;
                    }

                    if (*tilePixel > 0)
                        *frameBufferPtr = activePalette[*tilePixel];
                }
                ++frameBufferPtr;
                ++lineBuffer;
                XPos += XBuffer;
                YPos += YBuffer;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        int tileOffset, tileX, tileY, tileSinBlock, tileCosBlock;
        int sinValue512, cosValue512;
        int layerWidth         = layer->xsize << 7;
        int layerHeight        = layer->ysize << 7;
        ushort *currentTileMap = layer->tiles;
        vertexSize3D           = 0;
        indexSize3D            = 0;

        // low quality render
        polyList3D[vertexSize3D].x        = 0.0f;
        polyList3D[vertexSize3D].y        = 0.0f;
        polyList3D[vertexSize3D].z        = 0.0f;
        polyList3D[vertexSize3D].u        = 512;
        polyList3D[vertexSize3D].v        = 0;
        polyList3D[vertexSize3D].colour.r = 0xFF;
        polyList3D[vertexSize3D].colour.g = 0xFF;
        polyList3D[vertexSize3D].colour.b = 0xFF;
        polyList3D[vertexSize3D].colour.a = 0xFF;
        vertexSize3D++;

        polyList3D[vertexSize3D].x        = 4096.0f;
        polyList3D[vertexSize3D].y        = 0.0f;
        polyList3D[vertexSize3D].z        = 0.0f;
        polyList3D[vertexSize3D].u        = 1024;
        polyList3D[vertexSize3D].v        = 0;
        polyList3D[vertexSize3D].colour.r = 0xFF;
        polyList3D[vertexSize3D].colour.g = 0xFF;
        polyList3D[vertexSize3D].colour.b = 0xFF;
        polyList3D[vertexSize3D].colour.a = 0xFF;
        vertexSize3D++;

        polyList3D[vertexSize3D].x        = 0.0f;
        polyList3D[vertexSize3D].y        = 0.0f;
        polyList3D[vertexSize3D].z        = 4096.0f;
        polyList3D[vertexSize3D].u        = 512;
        polyList3D[vertexSize3D].v        = 512;
        polyList3D[vertexSize3D].colour.r = 0xFF;
        polyList3D[vertexSize3D].colour.g = 0xFF;
        polyList3D[vertexSize3D].colour.b = 0xFF;
        polyList3D[vertexSize3D].colour.a = 0xFF;
        vertexSize3D++;

        polyList3D[vertexSize3D].x        = 4096.0f;
        polyList3D[vertexSize3D].y        = 0.0f;
        polyList3D[vertexSize3D].z        = 4096.0f;
        polyList3D[vertexSize3D].u        = 1024;
        polyList3D[vertexSize3D].v        = 512;
        polyList3D[vertexSize3D].colour.r = 0xFF;
        polyList3D[vertexSize3D].colour.g = 0xFF;
        polyList3D[vertexSize3D].colour.b = 0xFF;
        polyList3D[vertexSize3D].colour.a = 0xFF;
        vertexSize3D++;

        indexSize3D += 6;
        if (hq3DFloorEnabled) {
            sinValue512 = (layer->XPos >> 16) - 0x100;
            sinValue512 += (sin512LookupTable[layer->angle] >> 1);
            sinValue512 = sinValue512 >> 4 << 4;

            cosValue512 = (layer->ZPos >> 16) - 0x100;
            cosValue512 += (cos512LookupTable[layer->angle] >> 1);
            cosValue512 = cosValue512 >> 4 << 4;
            for (int i = 32; i > 0; i--) {
                for (int j = 32; j > 0; j--) {
                    if (sinValue512 > -1 && sinValue512 < layerWidth && cosValue512 > -1 && cosValue512 < layerHeight) {
                        tileX         = sinValue512 >> 7;
                        tileY         = cosValue512 >> 7;
                        tileSinBlock  = (sinValue512 & 127) >> 4;
                        tileCosBlock  = (cosValue512 & 127) >> 4;
                        int tileIndex = currentTileMap[tileX + (tileY << 8)] << 6;
                        tileIndex     = tileIndex + tileSinBlock + (tileCosBlock << 3);
                        if (tiles128x128.gfxDataPos[tileIndex] > 0) {
                            tileOffset = 0;
                            switch (tiles128x128.direction[tileIndex]) {
                                case FLIP_NONE: {
                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_X: {
                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_Y: {
                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_XY: {
                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                            }
                        }
                    }
                    sinValue512 += 16;
                }
                sinValue512 -= 0x200;
                cosValue512 += 16;
            }
        }
        else {
            sinValue512 = (layer->XPos >> 16) - 0xA0;
            sinValue512 += sin512LookupTable[layer->angle] / 3;
            sinValue512 = sinValue512 >> 4 << 4;

            cosValue512 = (layer->ZPos >> 16) - 0xA0;
            cosValue512 += cos512LookupTable[layer->angle] / 3;
            cosValue512 = cosValue512 >> 4 << 4;
            for (int i = 20; i > 0; i--) {
                for (int j = 20; j > 0; j--) {
                    if (sinValue512 > -1 && sinValue512 < layerWidth && cosValue512 > -1 && cosValue512 < layerHeight) {
                        tileX         = sinValue512 >> 7;
                        tileY         = cosValue512 >> 7;
                        tileSinBlock  = (sinValue512 & 127) >> 4;
                        tileCosBlock  = (cosValue512 & 127) >> 4;
                        int tileIndex = currentTileMap[tileX + (tileY << 8)] << 6;
                        tileIndex     = tileIndex + tileSinBlock + (tileCosBlock << 3);
                        if (tiles128x128.gfxDataPos[tileIndex] > 0) {
                            tileOffset = 0;
                            switch (tiles128x128.direction[tileIndex]) {
                                case FLIP_NONE: {
                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_X: {
                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_Y: {
                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                                case FLIP_XY: {
                                    polyList3D[vertexSize3D].x = sinValue512 + 16;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = cosValue512 + 16;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x = sinValue512;
                                    polyList3D[vertexSize3D].y = 0.0f;
                                    polyList3D[vertexSize3D].z = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    tileOffset++;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = cosValue512;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = tileUVArray[tiles128x128.gfxDataPos[tileIndex] + tileOffset];
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    polyList3D[vertexSize3D].x        = polyList3D[vertexSize3D - 2].x;
                                    polyList3D[vertexSize3D].y        = 0.0f;
                                    polyList3D[vertexSize3D].z        = polyList3D[vertexSize3D - 1].z;
                                    polyList3D[vertexSize3D].u        = polyList3D[vertexSize3D - 2].u;
                                    polyList3D[vertexSize3D].v        = polyList3D[vertexSize3D - 1].v;
                                    polyList3D[vertexSize3D].colour.r = 0xFF;
                                    polyList3D[vertexSize3D].colour.g = 0xFF;
                                    polyList3D[vertexSize3D].colour.b = 0xFF;
                                    polyList3D[vertexSize3D].colour.a = 0xFF;
                                    vertexSize3D++;

                                    indexSize3D += 6;
                                    break;
                                }
                            }
                        }
                    }
                    sinValue512 += 16;
                }
                sinValue512 -= 0x140;
                cosValue512 += 16;
            }
        }
        floor3DXPos     = (layer->XPos >> 8) * -(1.0f / 256.0f);
        floor3DYPos     = (layer->YPos >> 8) * (1.0f / 256.0f);
        floor3DZPos     = (layer->ZPos >> 8) * -(1.0f / 256.0f);
        floor3DAngle    = layer->angle / 512.0f * -360.0f;
        render3DEnabled = true;
    }
}
void Draw3DSkyLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;

    if (renderType == RENDER_SW) {
        int layerWidth         = layer->xsize << 7;
        int layerHeight        = layer->ysize << 7;
        int layerYPos          = layer->YPos;
        int sinValue           = sinMLookupTable[layer->angle & 0x1FF];
        int cosValue           = cosMLookupTable[layer->angle & 0x1FF];
        ushort *frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
        ushort *bufferPtr      = Engine.frameBuffer2x;
        if (!drawStageGFXHQ)
            bufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
        byte *gfxLineBufferPtr = &gfxLineBuffer[((SCREEN_YSIZE / 2) + 12)];
        int layerXPos          = layer->XPos >> 4;
        int layerZPos          = layer->ZPos >> 4;
        for (int i = TILE_SIZE / 2; i < SCREEN_YSIZE - TILE_SIZE; ++i) {
            if (!(i & 1)) {
                activePalette   = fullPalette[*gfxLineBufferPtr];
                activePalette32 = fullPalette32[*gfxLineBufferPtr];
                gfxLineBufferPtr++;
            }
            int xBuffer    = layerYPos / (i << 8) * -cosValue >> 9;
            int yBuffer    = sinValue * (layerYPos / (i << 8)) >> 9;
            int XPos       = layerXPos + (3 * sinValue * (layerYPos / (i << 8)) >> 2) - xBuffer * GFX_LINESIZE;
            int YPos       = layerZPos + (3 * cosValue * (layerYPos / (i << 8)) >> 2) - yBuffer * GFX_LINESIZE;
            int lineBuffer = 0;
            while (lineBuffer < GFX_LINESIZE * 2) {
                int tileX = XPos >> 12;
                int tileY = YPos >> 12;
                if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                    int chunk       = tile3DFloorBuffer[(YPos >> 16 << 8) + (XPos >> 16)];
                    byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE: tilePixel += TILE_SIZE * (tileY & 0xF) + (tileX & 0xF); break;
                        case FLIP_X: tilePixel += TILE_SIZE * (tileY & 0xF) + 0xF - (tileX & 0xF); break;
                        case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                        case FLIP_XY: tilePixel += 0xF - (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                        default: break;
                    }

                    if (*tilePixel > 0)
                        *bufferPtr = activePalette[*tilePixel];
                    else if (drawStageGFXHQ)
                        *bufferPtr = *frameBufferPtr;
                }
                else if (drawStageGFXHQ) {
                    *bufferPtr = *frameBufferPtr;
                }

                if (lineBuffer & 1)
                    ++frameBufferPtr;

                if (drawStageGFXHQ)
                    bufferPtr++;
                else if (lineBuffer & 1)
                    ++bufferPtr;

                lineBuffer++;
                XPos += xBuffer;
                YPos += yBuffer;
            }

            if (!(i & 1))
                frameBufferPtr -= GFX_LINESIZE;

            if (!(i & 1) && !drawStageGFXHQ)
                bufferPtr -= GFX_LINESIZE;
        }

        if (drawStageGFXHQ) {
            frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
            int cnt        = ((SCREEN_YSIZE / 2) - 12) * GFX_LINESIZE;
            while (cnt--) *frameBufferPtr++ = 0xF81F; // Magenta
        }
    }

    // Not avaliable in HW Render mode
}

void DrawRectangle(int XPos, int YPos, int width, int height, int R, int G, int B, int A)
{
    if (A > 0xFF)
        A = 0xFF;
    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            width += XPos;
            XPos = 0;
        }

        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0 || A <= 0)
            return;
        int pitch              = GFX_LINESIZE - width;
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
        ushort clr             = 0;
        PACK_RGB888(clr, R, G, B);

        if (A == 0xFF) {
            int h = height;
            while (h--) {
                int w = width;
                while (w--) {
                    *frameBufferPtr = clr;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
            }
        }
        else {
            ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - A)];
            ushort *pixelBlend   = &blendLookupTable[0x20 * A];

            int h = height;
            while (h--) {
                int w = width;
                while (w--) {
                    int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(clr & 0xF800) >> 11]) << 11;
                    int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(clr & 0x7E0) >> 6]) << 6;
                    int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[clr & 0x1F];

                    *frameBufferPtr = R | G | B;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        if (gfxVertexSize < VERTEX_COUNT) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = R;
            gfxPolyList[gfxVertexSize].colour.g = G;
            gfxPolyList[gfxVertexSize].colour.b = B;
            gfxPolyList[gfxVertexSize].colour.a = A;
            gfxPolyList[gfxVertexSize].u        = 0;
            gfxPolyList[gfxVertexSize].v        = 0;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = R;
            gfxPolyList[gfxVertexSize].colour.g = G;
            gfxPolyList[gfxVertexSize].colour.b = B;
            gfxPolyList[gfxVertexSize].colour.a = A;
            gfxPolyList[gfxVertexSize].u        = 0;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = R;
            gfxPolyList[gfxVertexSize].colour.g = G;
            gfxPolyList[gfxVertexSize].colour.b = B;
            gfxPolyList[gfxVertexSize].colour.a = A;
            gfxPolyList[gfxVertexSize].u        = 0;
            gfxPolyList[gfxVertexSize].v        = 0;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = R;
            gfxPolyList[gfxVertexSize].colour.g = G;
            gfxPolyList[gfxVertexSize].colour.b = B;
            gfxPolyList[gfxVertexSize].colour.a = A;
            gfxPolyList[gfxVertexSize].u        = 0;
            gfxPolyList[gfxVertexSize].v        = 0;
            gfxVertexSize++;
            gfxIndexSize += 6;
        }
    }
}

void SetFadeHQ(int R, int G, int B, int A)
{
    if (A <= 0)
        return;
    if (A > 0xFF)
        A = 0xFF;
    if (renderType == RENDER_SW) {
        int pitch              = GFX_LINESIZE_DOUBLE;
        ushort *frameBufferPtr = Engine.frameBuffer2x;
        ushort clr             = 0;
        PACK_RGB888(clr, R, G, B);
        if (A == 0xFF) {
            int h = SCREEN_YSIZE;
            while (h--) {
                int w = pitch;
                while (w--) {
                    *frameBufferPtr = clr;
                    ++frameBufferPtr;
                }
            }
        }
        else {
            ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - A)];
            ushort *pixelBlend   = &blendLookupTable[0x20 * A];

            int h = SCREEN_YSIZE;
            while (h--) {
                int w = pitch;
                while (w--) {
                    int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(clr & 0xF800) >> 11]) << 11;
                    int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(clr & 0x7E0) >> 6]) << 6;
                    int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[clr & 0x1F];

                    *frameBufferPtr = R | G | B;
                    ++frameBufferPtr;
                }
            }
        }
    }

    // Not Avaliable in HW mode
}

void DrawTintRectangle(int XPos, int YPos, int width, int height)
{
    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            width += XPos;
            XPos = 0;
        }

        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0)
            return;

        int yOffset = GFX_LINESIZE - width;
        for (ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];; frameBufferPtr += yOffset) {
            height--;
            if (!height)
                break;

            int w = width;
            while (w--) {
                *frameBufferPtr = tintLookupTable[*frameBufferPtr];
                ++frameBufferPtr;
            }
        }
    }

    // Not avaliable in HW Render mode
}
void DrawScaledTintMask(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                        int sheetID)
{
    if (renderType == RENDER_SW) {
        int roundedYPos = 0;
        int roundedXPos = 0;
        int truescaleX  = 4 * scaleX;
        int truescaleY  = 4 * scaleY;
        int widthM1     = width - 1;
        int trueXPos    = XPos - (truescaleX * pivotX >> 11);
        width           = truescaleX * width >> 11;
        int trueYPos    = YPos - (truescaleY * pivotY >> 11);
        height          = truescaleY * height >> 11;
        int finalscaleX = (signed int)(float)((float)(2048.0 / (float)truescaleX) * 2048.0);
        int finalscaleY = (signed int)(float)((float)(2048.0 / (float)truescaleY) * 2048.0);
        if (width + trueXPos > GFX_LINESIZE) {
            width = GFX_LINESIZE - trueXPos;
        }

        if (direction) {
            if (trueXPos < 0) {
                widthM1 -= trueXPos * -finalscaleX >> 11;
                roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
                width += trueXPos;
                trueXPos = 0;
            }
        }
        else if (trueXPos < 0) {
            sprX += trueXPos * -finalscaleX >> 11;
            roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
            width += trueXPos;
            trueXPos = 0;
        }

        if (height + trueYPos > SCREEN_YSIZE) {
            height = SCREEN_YSIZE - trueYPos;
        }
        if (trueYPos < 0) {
            sprY += trueYPos * -finalscaleY >> 11;
            roundedYPos = (ushort)trueYPos * -(short)finalscaleY & 0x7FF;
            height += trueYPos;
            trueYPos = 0;
        }

        if (width <= 0 || height <= 0)
            return;

        GFXSurface *surface = &gfxSurface[sheetID];
        int pitch           = GFX_LINESIZE - width;
        int gfxwidth        = surface->width;
        // byte *lineBuffer       = &gfxLineBuffer[trueYPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[trueXPos + GFX_LINESIZE * trueYPos];
        if (direction == FLIP_X) {
            byte *gfxDataPtr = &gfxData[widthM1];
            int gfxPitch     = 0;
            while (height--) {
                int roundXPos = roundedXPos;
                int w         = width;
                while (w--) {
                    if (*gfxDataPtr > 0)
                        *frameBufferPtr = tintLookupTable[*frameBufferPtr];
                    int offsetX = finalscaleX + roundXPos;
                    gfxDataPtr -= offsetX >> 11;
                    gfxPitch += offsetX >> 11;
                    roundXPos = offsetX & 0x7FF;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                int offsetY = finalscaleY + roundedYPos;
                gfxDataPtr += gfxPitch + (offsetY >> 11) * gfxwidth;
                roundedYPos = offsetY & 0x7FF;
                gfxPitch    = 0;
            }
        }
        else {
            int gfxPitch = 0;
            int h        = height;
            while (h--) {
                int roundXPos = roundedXPos;
                int w         = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = tintLookupTable[*frameBufferPtr];
                    int offsetX = finalscaleX + roundXPos;
                    gfxData += offsetX >> 11;
                    gfxPitch += offsetX >> 11;
                    roundXPos = offsetX & 0x7FF;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                int offsetY = finalscaleY + roundedYPos;
                gfxData += (offsetY >> 11) * gfxwidth - gfxPitch;
                roundedYPos = offsetY & 0x7FF;
                gfxPitch    = 0;
            }
        }
    }

    // Not avaliable in HW Render mode
}

void DrawSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int sheetID)
{
    if (disableTouchControls) {
        if (StrComp(gfxSurface[sheetID].fileName, "Data/Sprites/Global/DPad.gif"))
            return;
    }

    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0)
            return;

        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxPitch           = surface->width - width;
        byte *lineBuffer       = &gfxLineBuffer[YPos];
        byte *gfxDataPtr       = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxDataPtr > 0)
                    *frameBufferPtr = activePalette[*gfxDataPtr];
                ++gfxDataPtr;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxDataPtr += gfxPitch;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}

void DrawSpriteFlipped(int XPos, int YPos, int width, int height, int sprX, int sprY, int direction, int sheetID)
{
    if (renderType == RENDER_SW) {
        int widthFlip  = width;
        int heightFlip = height;

        if (width + XPos > GFX_LINESIZE) {
            width = GFX_LINESIZE - XPos;
        }
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            widthFlip += XPos + XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE) {
            height = SCREEN_YSIZE - YPos;
        }
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            heightFlip += YPos + YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0)
            return;

        GFXSurface *surface = &gfxSurface[sheetID];
        int pitch;
        int gfxPitch;
        byte *lineBuffer;
        byte *gfxData;
        ushort *frameBufferPtr;
        switch (direction) {
            case FLIP_NONE:
                pitch          = GFX_LINESIZE - width;
                gfxPitch       = surface->width - width;
                lineBuffer     = &gfxLineBuffer[YPos];
                gfxData        = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
                frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

                while (height--) {
                    activePalette   = fullPalette[*lineBuffer];
                    activePalette32 = fullPalette32[*lineBuffer];
                    lineBuffer++;
                    int w = width;
                    while (w--) {
                        if (*gfxData > 0)
                            *frameBufferPtr = activePalette[*gfxData];
                        ++gfxData;
                        ++frameBufferPtr;
                    }
                    frameBufferPtr += pitch;
                    gfxData += gfxPitch;
                }
                break;
            case FLIP_X:
                pitch          = GFX_LINESIZE - width;
                gfxPitch       = width + surface->width;
                lineBuffer     = &gfxLineBuffer[YPos];
                gfxData        = &graphicData[widthFlip - 1 + sprX + surface->width * sprY + surface->dataPosition];
                frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
                while (height--) {
                    activePalette   = fullPalette[*lineBuffer];
                    activePalette32 = fullPalette32[*lineBuffer];
                    lineBuffer++;
                    int w = width;
                    while (w--) {
                        if (*gfxData > 0)
                            *frameBufferPtr = activePalette[*gfxData];
                        --gfxData;
                        ++frameBufferPtr;
                    }
                    frameBufferPtr += pitch;
                    gfxData += gfxPitch;
                }
                break;
            case FLIP_Y:
                pitch          = GFX_LINESIZE - width;
                gfxPitch       = width + surface->width;
                lineBuffer     = &gfxLineBuffer[YPos];
                gfxData        = &gfxLineBuffer[YPos];
                gfxData        = &graphicData[sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
                frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
                while (height--) {
                    activePalette   = fullPalette[*lineBuffer];
                    activePalette32 = fullPalette32[*lineBuffer];
                    lineBuffer++;
                    int w = width;
                    while (w--) {
                        if (*gfxData > 0)
                            *frameBufferPtr = activePalette[*gfxData];
                        ++gfxData;
                        ++frameBufferPtr;
                    }
                    frameBufferPtr += pitch;
                    gfxData -= gfxPitch;
                }
                break;
            case FLIP_XY:
                pitch          = GFX_LINESIZE - width;
                gfxPitch       = surface->width - width;
                lineBuffer     = &gfxLineBuffer[YPos];
                gfxData        = &graphicData[widthFlip - 1 + sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
                frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
                while (height--) {
                    activePalette   = fullPalette[*lineBuffer];
                    activePalette32 = fullPalette32[*lineBuffer];
                    lineBuffer++;
                    int w = width;
                    while (w--) {
                        if (*gfxData > 0)
                            *frameBufferPtr = activePalette[*gfxData];
                        --gfxData;
                        ++frameBufferPtr;
                    }
                    frameBufferPtr += pitch;
                    gfxData -= gfxPitch;
                }
                break;
            default: break;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            switch (direction) {
                case FLIP_NONE:
                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                    gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;
                    break;
                case FLIP_X:
                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                    gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;
                    break;
                case FLIP_Y:
                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                    gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;
                    break;
                case FLIP_XY:
                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
                    gfxPolyList[gfxVertexSize].y        = YPos << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = XPos << 4;
                    gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                    gfxVertexSize++;

                    gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                    gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                    gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                    gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                    gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                    gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                    gfxVertexSize++;
                    break;
            }
            gfxIndexSize += 6;
        }
    }
}
void DrawSpriteScaled(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                      int sheetID)
{
    if (renderType == RENDER_SW) {
        int roundedYPos = 0;
        int roundedXPos = 0;
        int truescaleX  = 4 * scaleX;
        int truescaleY  = 4 * scaleY;
        int widthM1     = width - 1;
        int trueXPos    = XPos - (truescaleX * pivotX >> 11);
        width           = truescaleX * width >> 11;
        int trueYPos    = YPos - (truescaleY * pivotY >> 11);
        height          = truescaleY * height >> 11;
        int finalscaleX = (signed int)(float)((float)(2048.0 / (float)truescaleX) * 2048.0);
        int finalscaleY = (signed int)(float)((float)(2048.0 / (float)truescaleY) * 2048.0);
        if (width + trueXPos > GFX_LINESIZE) {
            width = GFX_LINESIZE - trueXPos;
        }

        if (direction) {
            if (trueXPos < 0) {
                widthM1 -= trueXPos * -finalscaleX >> 11;
                roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
                width += trueXPos;
                trueXPos = 0;
            }
        }
        else if (trueXPos < 0) {
            sprX += trueXPos * -finalscaleX >> 11;
            roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
            width += trueXPos;
            trueXPos = 0;
        }

        if (height + trueYPos > SCREEN_YSIZE) {
            height = SCREEN_YSIZE - trueYPos;
        }
        if (trueYPos < 0) {
            sprY += trueYPos * -finalscaleY >> 11;
            roundedYPos = (ushort)trueYPos * -(short)finalscaleY & 0x7FF;
            height += trueYPos;
            trueYPos = 0;
        }

        if (width <= 0 || height <= 0)
            return;

        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxwidth           = surface->width;
        byte *lineBuffer       = &gfxLineBuffer[trueYPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[trueXPos + GFX_LINESIZE * trueYPos];
        if (direction == FLIP_X) {
            byte *gfxDataPtr = &gfxData[widthM1];
            int gfxPitch     = 0;
            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int roundXPos = roundedXPos;
                int w         = width;
                while (w--) {
                    if (*gfxDataPtr > 0)
                        *frameBufferPtr = activePalette[*gfxDataPtr];
                    int offsetX = finalscaleX + roundXPos;
                    gfxDataPtr -= offsetX >> 11;
                    gfxPitch += offsetX >> 11;
                    roundXPos = offsetX & 0x7FF;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                int offsetY = finalscaleY + roundedYPos;
                gfxDataPtr += gfxPitch + (offsetY >> 11) * gfxwidth;
                roundedYPos = offsetY & 0x7FF;
                gfxPitch    = 0;
            }
        }
        else {
            int gfxPitch = 0;
            int h        = height;
            while (h--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int roundXPos = roundedXPos;
                int w         = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    int offsetX = finalscaleX + roundXPos;
                    gfxData += offsetX >> 11;
                    gfxPitch += offsetX >> 11;
                    roundXPos = offsetX & 0x7FF;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                int offsetY = finalscaleY + roundedYPos;
                gfxData += (offsetY >> 11) * gfxwidth - gfxPitch;
                roundedYPos = offsetY & 0x7FF;
                gfxPitch    = 0;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        if (gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            scaleX <<= 2;
            scaleY <<= 2;
            XPos -= pivotX * scaleX >> 11;
            scaleX = width * scaleX >> 11;
            YPos -= pivotY * scaleY >> 11;
            scaleY              = height * scaleY >> 11;
            GFXSurface *surface = &gfxSurface[sheetID];
            if (surface->texStartX > -1) {
                gfxPolyList[gfxVertexSize].x        = XPos << 4;
                gfxPolyList[gfxVertexSize].y        = YPos << 4;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = (XPos + scaleX) << 4;
                gfxPolyList[gfxVertexSize].y        = YPos << 4;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = XPos << 4;
                gfxPolyList[gfxVertexSize].y        = (YPos + scaleY) << 4;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;
                gfxIndexSize += 6;
            }
        }
    }
}

void DrawScaledChar(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                    int sheetID)
{
    // Not avaliable in SW Render mode

    if (renderType == RENDER_HW) {
        if (gfxVertexSize < VERTEX_COUNT && XPos > -8192 && XPos < 13951 && YPos > -1024 && YPos < 4864) {
            XPos -= pivotX * scaleX >> 5;
            scaleX = width * scaleX >> 5;
            YPos -= pivotY * scaleY >> 5;
            scaleY = height * scaleY >> 5;
            if (gfxSurface[sheetID].texStartX > -1) {
                gfxPolyList[gfxVertexSize].x        = XPos;
                gfxPolyList[gfxVertexSize].y        = YPos;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxSurface[sheetID].texStartX + sprX;
                gfxPolyList[gfxVertexSize].v        = gfxSurface[sheetID].texStartY + sprY;
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = XPos + scaleX;
                gfxPolyList[gfxVertexSize].y        = YPos;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxSurface[sheetID].texStartX + sprX + width;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = XPos;
                gfxPolyList[gfxVertexSize].y        = YPos + scaleY;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxSurface[sheetID].texStartY + sprY + height;
                gfxVertexSize++;

                gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
                gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                gfxIndexSize += 6;
            }
        }
    }
}

void DrawSpriteRotated(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                       int sheetID)
{
    if (renderType == RENDER_SW) {
        int sprXPos    = (pivotX + sprX) << 9;
        int sprYPos    = (pivotY + sprY) << 9;
        int fullwidth  = width + sprX;
        int fullheight = height + sprY;
        int angle      = rotation & 0x1FF;
        if (angle < 0)
            angle += 0x200;
        if (angle)
            angle = 0x200 - angle;
        int sine   = sin512LookupTable[angle];
        int cosine = cos512LookupTable[angle];
        int XPositions[4];
        int YPositions[4];

        if (direction == FLIP_X) {
            XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX + 2)) >> 9);
            YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX + 2)) >> 9);
            XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX - width - 2)) >> 9);
            YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX - width - 2)) >> 9);
            XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (pivotX + 2)) >> 9);
            YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (pivotX + 2)) >> 9);
            int a         = pivotX - width - 2;
            int b         = height - pivotY + 2;
            XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
            YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
        }
        else {
            XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (-pivotX - 2)) >> 9);
            YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (-pivotX - 2)) >> 9);
            XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (width - pivotX + 2)) >> 9);
            YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (width - pivotX + 2)) >> 9);
            XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (-pivotX - 2)) >> 9);
            YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (-pivotX - 2)) >> 9);
            int a         = width - pivotX + 2;
            int b         = height - pivotY + 2;
            XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
            YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
        }

        int left = GFX_LINESIZE;
        for (int i = 0; i < 4; ++i) {
            if (XPositions[i] < left)
                left = XPositions[i];
        }
        if (left < 0)
            left = 0;

        int right = 0;
        for (int i = 0; i < 4; ++i) {
            if (XPositions[i] > right)
                right = XPositions[i];
        }
        if (right > GFX_LINESIZE)
            right = GFX_LINESIZE;
        int maxX = right - left;

        int top = SCREEN_YSIZE;
        for (int i = 0; i < 4; ++i) {
            if (YPositions[i] < top)
                top = YPositions[i];
        }
        if (top < 0)
            top = 0;

        int bottom = 0;
        for (int i = 0; i < 4; ++i) {
            if (YPositions[i] > bottom)
                bottom = YPositions[i];
        }
        if (bottom > SCREEN_YSIZE)
            bottom = SCREEN_YSIZE;
        int maxY = bottom - top;

        if (maxX <= 0 || maxY <= 0)
            return;

        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - maxX;
        int lineSize           = surface->widthShifted;
        ushort *frameBufferPtr = &Engine.frameBuffer[left + GFX_LINESIZE * top];
        byte *lineBuffer       = &gfxLineBuffer[top];
        int startX             = left - XPos;
        int startY             = top - YPos;
        int shiftPivot         = (sprX << 9) - 1;
        fullwidth <<= 9;
        int shiftheight = (sprY << 9) - 1;
        fullheight <<= 9;
        byte *gfxData = &graphicData[surface->dataPosition];
        if (cosine < 0 || sine < 0)
            sprYPos += sine + cosine;

        if (direction == FLIP_X) {
            int drawX = sprXPos - (cosine * startX - sine * startY) - 0x100;
            int drawY = cosine * startY + sprYPos + sine * startX;
            while (maxY--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int finalX = drawX;
                int finalY = drawY;
                int w      = maxX;
                while (w--) {
                    if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                        byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                        if (index > 0)
                            *frameBufferPtr = activePalette[index];
                    }
                    ++frameBufferPtr;
                    finalX -= cosine;
                    finalY += sine;
                }
                drawX += sine;
                drawY += cosine;
                frameBufferPtr += pitch;
            }
        }
        else {
            int drawX = sprXPos + cosine * startX - sine * startY;
            int drawY = cosine * startY + sprYPos + sine * startX;
            while (maxY--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int finalX = drawX;
                int finalY = drawY;
                int w      = maxX;
                while (w--) {
                    if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                        byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                        if (index > 0)
                            *frameBufferPtr = activePalette[index];
                    }
                    ++frameBufferPtr;
                    finalX += cosine;
                    finalY += sine;
                }
                drawX -= sine;
                drawY += cosine;
                frameBufferPtr += pitch;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        XPos <<= 4;
        YPos <<= 4;
        rotation -= rotation >> 9 << 9;
        if (rotation < 0) {
            rotation += 0x200;
        }
        if (rotation != 0) {
            rotation = 0x200 - rotation;
        }
        int sin = sin512LookupTable[rotation];
        int cos = cos512LookupTable[rotation];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -8192 && XPos < 13952 && YPos > -8192 && YPos < 12032) {
            if (direction == FLIP_NONE) {
                int x                               = -pivotX;
                int y                               = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                gfxVertexSize++;

                x                                   = width - pivotX;
                y                                   = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                x                                   = -pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                gfxVertexSize++;

                x                                   = width - pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;
                gfxIndexSize += 6;
            }
            else {
                int x                               = pivotX;
                int y                               = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                gfxVertexSize++;

                x                                   = pivotX - width;
                y                                   = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                x                                   = pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                gfxVertexSize++;

                x                                   = pivotX - width;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;
                gfxIndexSize += 6;
            }
        }
    }
}

void DrawSpriteRotozoom(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation, int scale,
                        int sheetID)
{
    if (scale == 0)
        return;

    if (renderType == RENDER_SW) {
        int sprXPos    = (pivotX + sprX) << 9;
        int sprYPos    = (pivotY + sprY) << 9;
        int fullwidth  = width + sprX;
        int fullheight = height + sprY;
        int angle      = rotation & 0x1FF;
        if (angle < 0)
            angle += 0x200;
        if (angle)
            angle = 0x200 - angle;
        int sine   = scale * sin512LookupTable[angle] >> 9;
        int cosine = scale * cos512LookupTable[angle] >> 9;
        int XPositions[4];
        int YPositions[4];

        if (direction == FLIP_X) {
            XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX + 2)) >> 9);
            YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX + 2)) >> 9);
            XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX - width - 2)) >> 9);
            YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX - width - 2)) >> 9);
            XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (pivotX + 2)) >> 9);
            YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (pivotX + 2)) >> 9);
            int a         = pivotX - width - 2;
            int b         = height - pivotY + 2;
            XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
            YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
        }
        else {
            XPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (-pivotX - 2)) >> 9);
            YPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (-pivotX - 2)) >> 9);
            XPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (width - pivotX + 2)) >> 9);
            YPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (width - pivotX + 2)) >> 9);
            XPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (-pivotX - 2)) >> 9);
            YPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (-pivotX - 2)) >> 9);
            int a         = width - pivotX + 2;
            int b         = height - pivotY + 2;
            XPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
            YPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
        }
        int truescale = (signed int)(float)((float)(512.0 / (float)scale) * 512.0);
        sine          = truescale * sin512LookupTable[angle] >> 9;
        cosine        = truescale * cos512LookupTable[angle] >> 9;

        int left = GFX_LINESIZE;
        for (int i = 0; i < 4; ++i) {
            if (XPositions[i] < left)
                left = XPositions[i];
        }
        if (left < 0)
            left = 0;

        int right = 0;
        for (int i = 0; i < 4; ++i) {
            if (XPositions[i] > right)
                right = XPositions[i];
        }
        if (right > GFX_LINESIZE)
            right = GFX_LINESIZE;
        int maxX = right - left;

        int top = SCREEN_YSIZE;
        for (int i = 0; i < 4; ++i) {
            if (YPositions[i] < top)
                top = YPositions[i];
        }
        if (top < 0)
            top = 0;

        int bottom = 0;
        for (int i = 0; i < 4; ++i) {
            if (YPositions[i] > bottom)
                bottom = YPositions[i];
        }
        if (bottom > SCREEN_YSIZE)
            bottom = SCREEN_YSIZE;
        int maxY = bottom - top;

        if (maxX <= 0 || maxY <= 0)
            return;

        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - maxX;
        int lineSize           = surface->widthShifted;
        ushort *frameBufferPtr = &Engine.frameBuffer[left + GFX_LINESIZE * top];
        byte *lineBuffer       = &gfxLineBuffer[top];
        int startX             = left - XPos;
        int startY             = top - YPos;
        int shiftPivot         = (sprX << 9) - 1;
        fullwidth <<= 9;
        int shiftheight = (sprY << 9) - 1;
        fullheight <<= 9;
        byte *gfxData = &graphicData[surface->dataPosition];
        if (cosine < 0 || sine < 0)
            sprYPos += sine + cosine;

        if (direction == FLIP_X) {
            int drawX = sprXPos - (cosine * startX - sine * startY) - (truescale >> 1);
            int drawY = cosine * startY + sprYPos + sine * startX;
            while (maxY--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int finalX = drawX;
                int finalY = drawY;
                int w      = maxX;
                while (w--) {
                    if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                        byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                        if (index > 0)
                            *frameBufferPtr = activePalette[index];
                    }
                    ++frameBufferPtr;
                    finalX -= cosine;
                    finalY += sine;
                }
                drawX += sine;
                drawY += cosine;
                frameBufferPtr += pitch;
            }
        }
        else {
            int drawX = sprXPos + cosine * startX - sine * startY;
            int drawY = cosine * startY + sprYPos + sine * startX;
            while (maxY--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int finalX = drawX;
                int finalY = drawY;
                int w      = maxX;
                while (w--) {
                    if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                        byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                        if (index > 0)
                            *frameBufferPtr = activePalette[index];
                    }
                    ++frameBufferPtr;
                    finalX += cosine;
                    finalY += sine;
                }
                drawX -= sine;
                drawY += cosine;
                frameBufferPtr += pitch;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        XPos <<= 4;
        YPos <<= 4;
        rotation -= rotation >> 9 << 9;
        if (rotation < 0)
            rotation += 0x200;
        if (rotation != 0)
            rotation = 0x200 - rotation;

        int sin = sin512LookupTable[rotation] * scale >> 9;
        int cos = cos512LookupTable[rotation] * scale >> 9;
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -8192 && XPos < 13952 && YPos > -8192 && YPos < 12032) {
            if (direction == FLIP_NONE) {
                int x                               = -pivotX;
                int y                               = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                gfxVertexSize++;

                x                                   = width - pivotX;
                y                                   = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                x                                   = -pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                gfxVertexSize++;

                x                                   = width - pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;
                gfxIndexSize += 6;
            }
            else {
                int x                               = pivotX;
                int y                               = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
                gfxVertexSize++;

                x                                   = pivotX - width;
                y                                   = -pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;

                x                                   = pivotX;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
                gfxVertexSize++;

                x                                   = pivotX - width;
                y                                   = height - pivotY;
                gfxPolyList[gfxVertexSize].x        = XPos + ((x * cos + y * sin) >> 5);
                gfxPolyList[gfxVertexSize].y        = YPos + ((y * cos - x * sin) >> 5);
                gfxPolyList[gfxVertexSize].colour.r = 0xFF;
                gfxPolyList[gfxVertexSize].colour.g = 0xFF;
                gfxPolyList[gfxVertexSize].colour.b = 0xFF;
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
                gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
                gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
                gfxVertexSize++;
                gfxIndexSize += 6;
            }
        }
    }
}

void DrawBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int sheetID)
{
    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0)
            return;

        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxPitch           = surface->width - width;
        byte *lineBuffer       = &gfxLineBuffer[YPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxData > 0)
                    *frameBufferPtr = ((activePalette[*gfxData] & 0xF7DE) >> 1) + ((*frameBufferPtr & 0xF7DE) >> 1);
                ++gfxData;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxData += gfxPitch;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0x80;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0x80;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0x80;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0x80;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}
void DrawAlphaBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (disableTouchControls) {
        if (StrComp(gfxSurface[sheetID].fileName, "Data/Sprites/Global/DPad.gif"))
            return;
    }

    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0 || alpha <= 0)
            return;

        if (alpha > 0xFF)
            alpha = 0xFF;
        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxPitch           = surface->width - width;
        byte *lineBuffer       = &gfxLineBuffer[YPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
        if (alpha == 0xFF) {
            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    ++gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData += gfxPitch;
            }
        }
        else {
            ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - alpha)];
            ushort *pixelBlend   = &blendLookupTable[0x20 * alpha];

            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0) {
                        ushort colour = activePalette[*gfxData];

                        int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(colour & 0xF800) >> 11]) << 11;
                        int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(colour & 0x7E0) >> 6]) << 6;
                        int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[colour & 0x1F];

                        *frameBufferPtr = R | G | B;
                    }
                    ++gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData += gfxPitch;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}
void DrawAdditiveBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0 || alpha <= 0)
            return;

        if (alpha > 0xFF)
            alpha = 0xFF;

        ushort *blendTablePtr  = &blendLookupTable[0x20 * alpha];
        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxPitch           = surface->width - width;
        byte *lineBuffer       = &gfxLineBuffer[YPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxData > 0) {
                    ushort colour = activePalette[*gfxData];

                    int R = minVal((blendTablePtr[(colour & 0xF800) >> 11] << 11) + (*frameBufferPtr & 0xF800), 0xF800);
                    int G = minVal((blendTablePtr[(colour & 0x7E0) >> 6] << 6) + (*frameBufferPtr & 0x7E0), 0x7E0);
                    int B = minVal(blendTablePtr[colour & 0x1F] + (*frameBufferPtr & 0x1F), 0x1F);

                    *frameBufferPtr = R | G | B;
                }
                ++gfxData;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxData += gfxPitch;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}
void DrawSubtractiveBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (renderType == RENDER_SW) {
        if (width + XPos > GFX_LINESIZE)
            width = GFX_LINESIZE - XPos;
        if (XPos < 0) {
            sprX -= XPos;
            width += XPos;
            XPos = 0;
        }
        if (height + YPos > SCREEN_YSIZE)
            height = SCREEN_YSIZE - YPos;
        if (YPos < 0) {
            sprY -= YPos;
            height += YPos;
            YPos = 0;
        }
        if (width <= 0 || height <= 0 || alpha <= 0)
            return;

        if (alpha > 0xFF)
            alpha = 0xFF;

        ushort *subBlendTable  = &subtractLookupTable[0x20 * alpha];
        GFXSurface *surface    = &gfxSurface[sheetID];
        int pitch              = GFX_LINESIZE - width;
        int gfxPitch           = surface->width - width;
        byte *lineBuffer       = &gfxLineBuffer[YPos];
        byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
        ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxData > 0) {
                    ushort colour = activePalette[*gfxData];

                    int R = maxVal((*frameBufferPtr & 0xF800) - (subBlendTable[(colour & 0xF800) >> 11] << 11), 0);
                    int G = maxVal((*frameBufferPtr & 0x7E0) - (subBlendTable[(colour & 0x7E0) >> 6] << 6), 0);
                    int B = maxVal((*frameBufferPtr & 0x1F) - subBlendTable[colour & 0x1F], 0);

                    *frameBufferPtr = R | G | B;
                }
                ++gfxData;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxData += gfxPitch;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (surface->texStartX > -1 && gfxVertexSize < VERTEX_COUNT && XPos > -512 && XPos < 872 && YPos > -512 && YPos < 752) {
            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = (XPos + width) << 4;
            gfxPolyList[gfxVertexSize].y        = YPos << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + sprX + width);
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = XPos << 4;
            gfxPolyList[gfxVertexSize].y        = (YPos + height) << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + sprY + height);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = gfxPolyList[gfxVertexSize - 2].x;
            gfxPolyList[gfxVertexSize].y        = gfxPolyList[gfxVertexSize - 1].y;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = alpha;
            gfxPolyList[gfxVertexSize].u        = gfxPolyList[gfxVertexSize - 2].u;
            gfxPolyList[gfxVertexSize].v        = gfxPolyList[gfxVertexSize - 1].v;
            gfxVertexSize++;
            gfxIndexSize += 6;
        }
    }
}

void DrawObjectAnimation(void *objScr, void *ent, int XPos, int YPos)
{
    ObjectScript *objectScript = (ObjectScript *)objScr;
    Entity *entity             = (Entity *)ent;
    SpriteAnimation *sprAnim   = &animationList[objectScript->animFile->aniListOffset + entity->animation];
    SpriteFrame *frame         = &animFrames[sprAnim->frameListOffset + entity->frame];
    int rotation               = 0;

    switch (sprAnim->rotationStyle) {
        case ROTSTYLE_NONE:
            switch (entity->direction) {
                case FLIP_NONE:
                    DrawSpriteFlipped(frame->pivotX + XPos, frame->pivotY + YPos, frame->width, frame->height, frame->sprX, frame->sprY, FLIP_NONE,
                                      frame->sheetID);
                    break;
                case FLIP_X:
                    DrawSpriteFlipped(XPos - frame->width - frame->pivotX, frame->pivotY + YPos, frame->width, frame->height, frame->sprX,
                                      frame->sprY, FLIP_X, frame->sheetID);
                    break;
                case FLIP_Y:
                    DrawSpriteFlipped(frame->pivotX + XPos, YPos - frame->height - frame->pivotY, frame->width, frame->height, frame->sprX,
                                      frame->sprY, FLIP_Y, frame->sheetID);
                    break;
                case FLIP_XY:
                    DrawSpriteFlipped(XPos - frame->width - frame->pivotX, YPos - frame->height - frame->pivotY, frame->width, frame->height,
                                      frame->sprX, frame->sprY, FLIP_XY, frame->sheetID);
                    break;
                default: break;
            }
            break;
        case ROTSTYLE_FULL:
            DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width, frame->height,
                              entity->rotation, frame->sheetID);
            break;
        case ROTSTYLE_45DEG:
            if (entity->rotation >= 0x100)
                DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
                                  frame->height, 0x200 - ((532 - entity->rotation) >> 6 << 6), frame->sheetID);
            else
                DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
                                  frame->height, (entity->rotation + 20) >> 6 << 6, frame->sheetID);
            break;
        case ROTSTYLE_STATICFRAMES: {
            if (entity->rotation >= 0x100)
                rotation = 8 - ((532 - entity->rotation) >> 6);
            else
                rotation = (entity->rotation + 20) >> 6;
            int frameID = entity->frame;
            switch (rotation) {
                case 0: // 0 deg
                case 8: // 360 deg
                    rotation = 0x00;
                    break;
                case 1: // 45 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0;
                    else
                        rotation = 0x80;
                    break;
                case 2: // 90 deg
                    rotation = 0x80;
                    break;
                case 3: // 135 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0x80;
                    else
                        rotation = 0x100;
                    break;
                case 4: // 180 deg
                    rotation = 0x100;
                    break;
                case 5: // 225 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0x100;
                    else
                        rotation = 384;
                    break;
                case 6: // 270 deg
                    rotation = 384;
                    break;
                case 7: // 315 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 384;
                    else
                        rotation = 0;
                    break;
                default: break;
            }

            frame = &animFrames[sprAnim->frameListOffset + frameID];
            DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width, frame->height,
                              rotation, frame->sheetID);
            // DrawSpriteRotozoom(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
            // frame->height,
            //                  rotation, entity->scale, frame->sheetID);
            break;
        }
        default: break;
    }
}

void DrawFace(void *v, uint colour)
{
    Vertex *verts = (Vertex *)v;

    if (renderType == RENDER_SW) {
        int alpha = (colour & 0x7F000000) >> 23;
        if (alpha < 1)
            return;
        if (alpha > 0xFF)
            alpha = 0xFF;
        if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
            return;
        if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
            return;
        if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
            return;
        if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
            return;
        if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
            return;
        if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
            return;

        int vertexA = 0;
        int vertexB = 1;
        int vertexC = 2;
        int vertexD = 3;
        if (verts[1].y < verts[0].y) {
            vertexA = 1;
            vertexB = 0;
        }
        if (verts[2].y < verts[vertexA].y) {
            int temp = vertexA;
            vertexA  = 2;
            vertexC  = temp;
        }
        if (verts[3].y < verts[vertexA].y) {
            int temp = vertexA;
            vertexA  = 3;
            vertexD  = temp;
        }
        if (verts[vertexC].y < verts[vertexB].y) {
            int temp = vertexB;
            vertexB  = vertexC;
            vertexC  = temp;
        }
        if (verts[vertexD].y < verts[vertexB].y) {
            int temp = vertexB;
            vertexB  = vertexD;
            vertexD  = temp;
        }
        if (verts[vertexD].y < verts[vertexC].y) {
            int temp = vertexC;
            vertexC  = vertexD;
            vertexD  = temp;
        }

        int faceTop    = verts[vertexA].y;
        int faceBottom = verts[vertexD].y;
        if (faceTop < 0)
            faceTop = 0;
        if (faceBottom > SCREEN_YSIZE)
            faceBottom = SCREEN_YSIZE;
        for (int i = faceTop; i < faceBottom; ++i) {
            faceLineStart[i] = 100000;
            faceLineEnd[i]   = -100000;
        }

        ProcessScanEdge(&verts[vertexA], &verts[vertexB]);
        ProcessScanEdge(&verts[vertexA], &verts[vertexC]);
        ProcessScanEdge(&verts[vertexA], &verts[vertexD]);
        ProcessScanEdge(&verts[vertexB], &verts[vertexC]);
        ProcessScanEdge(&verts[vertexC], &verts[vertexD]);
        ProcessScanEdge(&verts[vertexB], &verts[vertexD]);

        ushort colour16 = 0;
        PACK_RGB888(colour16, ((colour >> 16) & 0xFF), ((colour >> 8) & 0xFF), ((colour >> 0) & 0xFF));

        ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
        if (alpha == 255) {
            while (faceTop < faceBottom) {
                int startX = faceLineStart[faceTop];
                int endX   = faceLineEnd[faceTop];
                if (startX >= GFX_LINESIZE || endX <= 0) {
                    frameBufferPtr += GFX_LINESIZE;
                }
                else {
                    if (startX < 0)
                        startX = 0;
                    if (endX > GFX_LINESIZE - 1)
                        endX = GFX_LINESIZE - 1;
                    ushort *fbPtr = &frameBufferPtr[startX];
                    frameBufferPtr += GFX_LINESIZE;
                    int vertexwidth = endX - startX + 1;
                    while (vertexwidth--) {
                        *fbPtr = colour16;
                        ++fbPtr;
                    }
                }
                ++faceTop;
            }
        }
        else {
            ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - alpha)];
            ushort *pixelBlend   = &blendLookupTable[0x20 * alpha];

            while (faceTop < faceBottom) {
                int startX = faceLineStart[faceTop];
                int endX   = faceLineEnd[faceTop];
                if (startX >= GFX_LINESIZE || endX <= 0) {
                    frameBufferPtr += GFX_LINESIZE;
                }
                else {
                    if (startX < 0)
                        startX = 0;
                    if (endX > GFX_LINESIZE - 1)
                        endX = GFX_LINESIZE - 1;
                    ushort *fbPtr = &frameBufferPtr[startX];
                    frameBufferPtr += GFX_LINESIZE;
                    int vertexwidth = endX - startX + 1;
                    while (vertexwidth--) {
                        int R = (fbufferBlend[(*fbPtr & 0xF800) >> 11] + pixelBlend[(colour16 & 0xF800) >> 11]) << 11;
                        int G = (fbufferBlend[(*fbPtr & 0x7E0) >> 6] + pixelBlend[(colour16 & 0x7E0) >> 6]) << 6;
                        int B = fbufferBlend[*fbPtr & 0x1F] + pixelBlend[colour16 & 0x1F];

                        *fbPtr = R | G | B;
                        ++fbPtr;
                    }
                }
                ++faceTop;
            }
        }
    }
    else if (renderType == RENDER_HW) {
        if (gfxVertexSize < VERTEX_COUNT) {
            gfxPolyList[gfxVertexSize].x        = verts[0].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[0].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = (byte)((uint)(colour >> 16) & 0xFF);
            gfxPolyList[gfxVertexSize].colour.g = (byte)((uint)(colour >> 8) & 0xFF);
            gfxPolyList[gfxVertexSize].colour.b = (byte)((uint)colour & 0xFF);
            colour                              = (colour & 0x7F000000) >> 23;

            if (colour == 0xFE)
                gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            else
                gfxPolyList[gfxVertexSize].colour.a = colour;

            gfxPolyList[gfxVertexSize].u = 2;
            gfxPolyList[gfxVertexSize].v = 2;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[1].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[1].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = gfxPolyList[gfxVertexSize - 1].colour.r;
            gfxPolyList[gfxVertexSize].colour.g = gfxPolyList[gfxVertexSize - 1].colour.g;
            gfxPolyList[gfxVertexSize].colour.b = gfxPolyList[gfxVertexSize - 1].colour.b;
            gfxPolyList[gfxVertexSize].colour.a = gfxPolyList[gfxVertexSize - 1].colour.a;
            gfxPolyList[gfxVertexSize].u        = 2;
            gfxPolyList[gfxVertexSize].v        = 2;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[2].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[2].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = gfxPolyList[gfxVertexSize - 1].colour.r;
            gfxPolyList[gfxVertexSize].colour.g = gfxPolyList[gfxVertexSize - 1].colour.g;
            gfxPolyList[gfxVertexSize].colour.b = gfxPolyList[gfxVertexSize - 1].colour.b;
            gfxPolyList[gfxVertexSize].colour.a = gfxPolyList[gfxVertexSize - 1].colour.a;
            gfxPolyList[gfxVertexSize].u        = 2;
            gfxPolyList[gfxVertexSize].v        = 2;
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[3].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[3].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = gfxPolyList[gfxVertexSize - 1].colour.r;
            gfxPolyList[gfxVertexSize].colour.g = gfxPolyList[gfxVertexSize - 1].colour.g;
            gfxPolyList[gfxVertexSize].colour.b = gfxPolyList[gfxVertexSize - 1].colour.b;
            gfxPolyList[gfxVertexSize].colour.a = gfxPolyList[gfxVertexSize - 1].colour.a;
            gfxPolyList[gfxVertexSize].u        = 2;
            gfxPolyList[gfxVertexSize].v        = 2;
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}
void DrawTexturedFace(void *v, byte sheetID)
{
    Vertex *verts = (Vertex *)v;

    if (renderType == RENDER_SW) {
        if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
            return;
        if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
            return;
        if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
            return;
        if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
            return;
        if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
            return;
        if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
            return;

        int vertexA = 0;
        int vertexB = 1;
        int vertexC = 2;
        int vertexD = 3;
        if (verts[1].y < verts[0].y) {
            vertexA = 1;
            vertexB = 0;
        }
        if (verts[2].y < verts[vertexA].y) {
            int temp = vertexA;
            vertexA  = 2;
            vertexC  = temp;
        }
        if (verts[3].y < verts[vertexA].y) {
            int temp = vertexA;
            vertexA  = 3;
            vertexD  = temp;
        }
        if (verts[vertexC].y < verts[vertexB].y) {
            int temp = vertexB;
            vertexB  = vertexC;
            vertexC  = temp;
        }
        if (verts[vertexD].y < verts[vertexB].y) {
            int temp = vertexB;
            vertexB  = vertexD;
            vertexD  = temp;
        }
        if (verts[vertexD].y < verts[vertexC].y) {
            int temp = vertexC;
            vertexC  = vertexD;
            vertexD  = temp;
        }

        int faceTop    = verts[vertexA].y;
        int faceBottom = verts[vertexD].y;
        if (faceTop < 0)
            faceTop = 0;
        if (faceBottom > SCREEN_YSIZE)
            faceBottom = SCREEN_YSIZE;
        for (int i = faceTop; i < faceBottom; ++i) {
            faceLineStart[i] = 100000;
            faceLineEnd[i]   = -100000;
        }

        ProcessScanEdgeUV(&verts[vertexA], &verts[vertexB]);
        ProcessScanEdgeUV(&verts[vertexA], &verts[vertexC]);
        ProcessScanEdgeUV(&verts[vertexA], &verts[vertexD]);
        ProcessScanEdgeUV(&verts[vertexB], &verts[vertexC]);
        ProcessScanEdgeUV(&verts[vertexC], &verts[vertexD]);
        ProcessScanEdgeUV(&verts[vertexB], &verts[vertexD]);

        ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
        byte *sheetPtr         = &graphicData[gfxSurface[sheetID].dataPosition];
        int shiftwidth         = gfxSurface[sheetID].widthShifted;
        byte *lineBuffer       = &gfxLineBuffer[faceTop];
        while (faceTop < faceBottom) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int startX = faceLineStart[faceTop];
            int endX   = faceLineEnd[faceTop];
            int UPos   = faceLineStartU[faceTop];
            int VPos   = faceLineStartV[faceTop];
            if (startX >= GFX_LINESIZE || endX <= 0) {
                frameBufferPtr += GFX_LINESIZE;
            }
            else {
                int posDifference = endX - startX;
                int bufferedUPos  = 0;
                int bufferedVPos  = 0;
                if (endX == startX) {
                    bufferedUPos = 0;
                    bufferedVPos = 0;
                }
                else {
                    bufferedUPos = (faceLineEndU[faceTop] - UPos) / posDifference;
                    bufferedVPos = (faceLineEndV[faceTop] - VPos) / posDifference;
                }
                if (endX > GFX_LINESIZE_MINUSONE)
                    posDifference = GFX_LINESIZE_MINUSONE - startX;

                if (startX < 0) {
                    posDifference += startX;
                    UPos -= startX * bufferedUPos;
                    VPos -= startX * bufferedVPos;
                    startX = 0;
                }
                ushort *fbPtr = &frameBufferPtr[startX];
                frameBufferPtr += GFX_LINESIZE;
                int counter = posDifference + 1;
                while (counter--) {
                    if (UPos < 0)
                        UPos = 0;
                    if (VPos < 0)
                        VPos = 0;
                    ushort index = sheetPtr[(VPos >> 16 << shiftwidth) + (UPos >> 16)];
                    if (index > 0)
                        *fbPtr = activePalette[index];
                    fbPtr++;
                    UPos += bufferedUPos;
                    VPos += bufferedVPos;
                }
            }
            ++faceTop;
        }
    }
    else if (renderType == RENDER_HW) {
        GFXSurface *surface = &gfxSurface[sheetID];
        if (gfxVertexSize < VERTEX_COUNT) {
            gfxPolyList[gfxVertexSize].x        = verts[0].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[0].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + verts[0].u);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + verts[0].v);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[1].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[1].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + verts[1].u);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + verts[1].v);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[2].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[2].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + verts[2].u);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + verts[2].v);
            gfxVertexSize++;

            gfxPolyList[gfxVertexSize].x        = verts[3].x << 4;
            gfxPolyList[gfxVertexSize].y        = verts[3].y << 4;
            gfxPolyList[gfxVertexSize].colour.r = 0xFF;
            gfxPolyList[gfxVertexSize].colour.g = 0xFF;
            gfxPolyList[gfxVertexSize].colour.b = 0xFF;
            gfxPolyList[gfxVertexSize].colour.a = 0xFF;
            gfxPolyList[gfxVertexSize].u        = (surface->texStartX + verts[3].u);
            gfxPolyList[gfxVertexSize].v        = (surface->texStartY + verts[3].v);
            gfxVertexSize++;

            gfxIndexSize += 6;
        }
    }
}

void DrawBitmapText(void *menu, int XPos, int YPos, int scale, int spacing, int rowStart, int rowCount)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int Y           = YPos << 9;
    if (rowCount < 0)
        rowCount = tMenu->rowCount;
    if (rowStart + rowCount > tMenu->rowCount)
        rowCount = tMenu->rowCount - rowStart;

    while (rowCount > 0) {
        int X = XPos << 9;
        for (int i = 0; i < tMenu->entrySize[rowStart]; ++i) {
            ushort c             = tMenu->textData[tMenu->entryStart[rowStart] + i];
            FontCharacter *fChar = &fontCharacterList[c];
            if (renderType == RENDER_SW) {
                DrawSpriteScaled(FLIP_NONE, X >> 9, Y >> 9, -fChar->pivotX, -fChar->pivotY, scale, scale, fChar->width, fChar->height, fChar->srcX,
                                 fChar->srcY, textMenuSurfaceNo);
            }
            else if (renderType == RENDER_HW) {
                DrawScaledChar(FLIP_NONE, X >> 5, Y >> 5, -fChar->pivotX, -fChar->pivotY, scale, scale, fChar->width, fChar->height, fChar->srcX,
                               fChar->srcY, textMenuSurfaceNo);
            }
            X += fChar->xAdvance * scale;
        }
        Y += spacing * scale;
        rowStart++;
        rowCount--;
    }
}

void DrawTextMenuEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        DrawSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3) + textHighlight,
                   textMenuSurfaceNo);
        id++;
    }
}
void DrawStageTextEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        if (i == tMenu->entrySize[rowID] - 1) {
            DrawSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3), textMenuSurfaceNo);
        }
        else {
            DrawSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3) + textHighlight,
                       textMenuSurfaceNo);
        }
        id++;
    }
}
void DrawBlendedTextMenuEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        DrawBlendedSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3) + textHighlight,
                          textMenuSurfaceNo);
        id++;
    }
}
void DrawTextMenu(void *menu, int XPos, int YPos)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int cnt         = 0;
    if (tMenu->visibleRowCount > 0) {
        cnt = (int)(tMenu->visibleRowCount + tMenu->visibleRowOffset);
    }
    else {
        tMenu->visibleRowOffset = 0;
        cnt                     = (int)tMenu->rowCount;
    }

    if (tMenu->selectionCount == 3) {
        tMenu->selection2 = -1;
        for (int i = 0; i < tMenu->selection1 + 1; ++i) {
            if (tMenu->entryHighlight[i] == 1) {
                tMenu->selection2 = i;
            }
        }
    }

    switch (tMenu->alignment) {
        case 0:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);
                        break;

                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);
                        break;

                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, XPos, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        case 1:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                int entryX = XPos - (tMenu->entrySize[i] << 3);
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, entryX, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        case 2:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                int entryX = XPos - (tMenu->entrySize[i] >> 1 << 3);
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, entryX, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        default: break;
    }
}
