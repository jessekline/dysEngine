/* Main entry point for windows */

#include <dsound.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <xinput.h>

#define internal static
#define local_persist static
#define global_variable static

#define PI32 3.14159265359f

typedef struct Win32_offscreen_buffer {
    BITMAPINFO info;
    void* memory;
    int width;
    int height;
    int pitch;
    int bytesPerPixel;
} offscreenBuffer_t;

typedef struct Win32_window_dimension {
    int width;
    int height;
} windowDimension_t;

typedef struct Win32_sound_output {
    int sameplesPerSecond;
    float hz;
    uint32_t runningSampleIndex;
    int wavePeriod;
    int bytesPerSample;
    int secondaryBufferSize;
    int toneVolume;
    float tSine;
    int latencySampleCount;
} soundOutput_t;

typedef int32_t boolInt;

/* Globals */
global_variable bool globalRunning;
global_variable offscreenBuffer_t globalBackbuffer;
global_variable LPDIRECTSOUNDBUFFER globalSecondaryBuffer;

/* Dynamically Loaded Functions */
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_get_state* XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_set_state* XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name)                                                                  \
    HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND* ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);
// NOTE: The following isn't needed because DSCreate should only be called once ever
// and I don't want the stub function accessible to anybody
/* DIRECT_SOUND_CREATE(DirectSoundCreateStub) { return DSERR_GENERIC; }
global_variable direct_sound_create* DirectSoundCreate_ = DirectSoundCreateStub;
#define DirectSoundCreate DirectSoundCreate_ */

internal void
Win32_LoadXInput(void)
{
    HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
    if (!XInputLibrary) {
        HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }

    if (XInputLibrary) {
        XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state*)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal void
Win32_InitDSound(HWND window, int32_t sameplesPerSecond, int32_t bufferSize)
{
    // Load the library
    HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");

    if (DSoundLibrary) {

        // Get a DirectSound object
        direct_sound_create* DirectSoundCreate =
          (direct_sound_create*)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

        LPDIRECTSOUND DirectSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0))) {

            WAVEFORMATEX waveFormat    = {};
            waveFormat.wFormatTag      = WAVE_FORMAT_PCM;
            waveFormat.nChannels       = 2;
            waveFormat.nSamplesPerSec  = sameplesPerSecond;
            waveFormat.wBitsPerSample  = 16;
            waveFormat.nBlockAlign     = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
            waveFormat.nAvgBytesPerSec = waveFormat.nBlockAlign * waveFormat.nSamplesPerSec;
            waveFormat.cbSize          = 0;

            if (SUCCEEDED(DirectSound->SetCooperativeLevel(window, DSSCL_PRIORITY))) {

                DSBUFFERDESC bufferDescription = {};
                bufferDescription.dwSize       = sizeof(bufferDescription);
                bufferDescription.dwFlags      = DSBCAPS_PRIMARYBUFFER;

                // Create a primary buffer
                LPDIRECTSOUNDBUFFER primaryBuffer;
                if (SUCCEEDED(
                      DirectSound->CreateSoundBuffer(&bufferDescription, &primaryBuffer, 0))) {

                    if (SUCCEEDED(primaryBuffer->SetFormat(&waveFormat))) {
                        OutputDebugStringA("Created Primary Buffer!\n");

                    } else {
                        // TODO: Logging
                    }
                } else {
                    // TODO: Logging
                }

            } else {
                // TODO: Logging
            }

            // Create a secondary buffer (write to this one)
            DSBUFFERDESC bufferDescription  = {};
            bufferDescription.dwSize        = sizeof(bufferDescription);
            bufferDescription.dwFlags       = 0;
            bufferDescription.dwBufferBytes = bufferSize;
            bufferDescription.lpwfxFormat   = &waveFormat;

            if (SUCCEEDED(
                  DirectSound->CreateSoundBuffer(&bufferDescription, &globalSecondaryBuffer, 0))) {
                OutputDebugStringA("Created Secondary Buffer!\n");
            } else {
                // TODO: Logging
            }

        } else {
            // TODO: Logging
        }

    } else {
        // TODO: Logging
    }
}

/* Helper function to calculate the dimensions of a window */
internal windowDimension_t
Win32_GetWindowDimension(HWND window)
{

    windowDimension_t result;

    RECT clientRect;
    GetClientRect(window, &clientRect);
    result.width  = clientRect.right - clientRect.left;
    result.height = clientRect.bottom - clientRect.top;

    return result;
}

/* A function for fun, though it might be good for bg stuff */
internal void
Win32_RenderGradient(offscreenBuffer_t* buffer, int xOffset, int yOffset)
{

    uint8_t* row = (uint8_t*)buffer->memory;

    /* Write pixels to the window
    Pixel in memory: BB GG RR xx */
    for (int y = 0; y < buffer->height; y++) {
        uint32_t* pixel = (uint32_t*)row;
        for (int x = 0; x < buffer->width / 2; x++) {

            uint8_t red   = ((uint8_t)(y + yOffset) ^ (uint8_t)(x + xOffset)) % 255;
            uint8_t green = 0;
            uint8_t blue  = 0;

            *pixel++ = (red << 16 | green << 8 | blue);
        }
        for (int x = buffer->width / 2; x < buffer->width; x++) {

            uint8_t red   = 0;
            uint8_t green = ((uint8_t)(y + yOffset) ^ (uint8_t)(x + xOffset)) % 255;
            uint8_t blue  = 0;

            *pixel++ = (red << 16 | green << 8 | blue);
        }
        row += buffer->pitch;
    }
}

/* Allocate memory for bitmap */
internal void
Win32_InitBuffer(offscreenBuffer_t* buffer, int width, int height)
{

    if (buffer->memory) {
        VirtualFree(buffer->memory, 0, MEM_RELEASE);
    }

    buffer->width         = width;
    buffer->height        = height;
    buffer->bytesPerPixel = 4;

    buffer->info.bmiHeader.biSize        = sizeof(buffer->info.bmiHeader);
    buffer->info.bmiHeader.biWidth       = buffer->width;
    buffer->info.bmiHeader.biHeight      = -buffer->height;
    buffer->info.bmiHeader.biPlanes      = 1;
    buffer->info.bmiHeader.biBitCount    = 32;
    buffer->info.bmiHeader.biCompression = BI_RGB;

    int bitmapMemorySize = buffer->bytesPerPixel * buffer->width * buffer->height;
    buffer->memory       = VirtualAlloc(0, bitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);

    // TODO: Probably want to clear screen here

    buffer->pitch = width * buffer->bytesPerPixel;
}

/* Draw the buffer to the window */
internal void
Win32_DisplayBufferInWindow(HDC deviceContext, int windowWidth, int windowHeight,
                            offscreenBuffer_t* buffer, int x, int y)
{

    StretchDIBits(deviceContext, 0, 0, windowWidth, windowHeight, 0, 0, buffer->width,
                  buffer->height, buffer->memory, &buffer->info, DIB_RGB_COLORS, SRCCOPY);
}

/* Message handler for OS messages */
LRESULT CALLBACK
Win32_MainWindowCallback(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{

    LRESULT result = 0;

    switch (message) {

        case WM_SIZE: {
        } break;

        case WM_CLOSE: {
            // TODO: Send message to user
            globalRunning = false;
        } break;

        case WM_ACTIVATEAPP: {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        case WM_DESTROY: {
            // TODO: Handle as an error?
            globalRunning = false;
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP: {

            uint32_t vKCode = wParam;
            bool wasDown    = ((lParam & (1 << 30)) != 0);
            bool isDown     = ((lParam & (1 << 31)) == 0);

            if (wasDown != isDown) {

                switch (vKCode) {

                    case 'W': {
                    } break;
                    case 'A': {
                    } break;
                    case 'S': {
                    } break;
                    case 'D': {
                    } break;
                    case 'Q': {
                    } break;
                    case 'E': {
                    } break;
                    case VK_UP: {
                    } break;
                    case VK_DOWN: {
                    } break;
                    case VK_LEFT: {
                    } break;
                    case VK_RIGHT: {
                    } break;
                    case VK_ESCAPE: {
                    } break;
                    case VK_SPACE: {
                    } break;
                    default: {
                        OutputDebugStringA("Not a valid key\n");
                    }
                }
            }

            boolInt altKeyWasDown = (lParam & (1 << 29));
            if ((vKCode == VK_F4) && altKeyWasDown) {
                globalRunning = false;
            }

        } break;

        /* Uses GDI (windows graphics API) to blit to window */
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC deviceContext = BeginPaint(window, &paint);
            int x             = paint.rcPaint.left;
            int y             = paint.rcPaint.top;

            windowDimension_t dimension = Win32_GetWindowDimension(window);
            Win32_DisplayBufferInWindow(deviceContext, dimension.width, dimension.height,
                                        &globalBackbuffer, x, y);
            EndPaint(window, &paint);
        } break;

        default: {
            // OutputDebugStringA("default\n");
            result = DefWindowProc(window, message, wParam, lParam);
        } break;
    }

    return result;
}

// TODO: Figure out why the tone changes pitch if left alone for a few seconds
void
Win32_FillSoundBuffer(soundOutput_t* soundOutput, DWORD byteToLock, DWORD bytesToWrite)
{
    VOID* region1;
    DWORD region1Size;
    VOID* region2;
    DWORD region2Size;

    if (SUCCEEDED(globalSecondaryBuffer->Lock(byteToLock, bytesToWrite, &region1, &region1Size,
                                              &region2, &region2Size, 0))) {
        // TODO: assert that region1size/region2size are valid
        DWORD region1SampleCount = region1Size / soundOutput->bytesPerSample;
        int16_t* sampleOut       = (int16_t*)region1;
        for (DWORD sampleIndex = 0; sampleIndex < region1SampleCount; sampleIndex++) {
            float sineValue     = sinf(soundOutput->tSine);
            int16_t sampleValue = (int16_t)(sineValue * soundOutput->toneVolume);
            *sampleOut++        = sampleValue;
            *sampleOut++        = sampleValue;

            soundOutput->tSine += (2.0f * PI32 * 1.0f) / (float)soundOutput->wavePeriod;
            ++soundOutput->runningSampleIndex;
        }

        DWORD region2SampleCount = region2Size / soundOutput->bytesPerSample;
        sampleOut                = (int16_t*)region2;
        for (DWORD sampleIndex = 0; sampleIndex < region2SampleCount; sampleIndex++) {
            float sineValue     = sinf(soundOutput->tSine);
            int16_t sampleValue = (int16_t)(sineValue * soundOutput->toneVolume);
            *sampleOut++        = sampleValue;
            *sampleOut++        = sampleValue;

            soundOutput->tSine += (2.0f * PI32 * 1.0f) / (float)soundOutput->wavePeriod;
            ++soundOutput->runningSampleIndex;
        }

        globalSecondaryBuffer->Unlock(region1, region1Size, region2, region2Size);
    }
}

/* Entry point of application */
int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR commandLine, int showCode)
{

    Win32_LoadXInput();

    WNDCLASSA myWindowClass = {};

    Win32_InitBuffer(&globalBackbuffer, 1920, 1080);

    myWindowClass.style       = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    myWindowClass.lpfnWndProc = Win32_MainWindowCallback;
    myWindowClass.hInstance   = instance;
    //    myWindow.hIcon = ;
    myWindowClass.lpszClassName = "gameWindowClass";

    /* Registering a window class applies its properties */
    if (RegisterClassA(&myWindowClass)) {
        HWND window = CreateWindowExA(
          0, myWindowClass.lpszClassName, "Test Window", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, instance, 0);

        if (window) {
            HDC deviceContext = GetDC(window);

            globalRunning = true;
            int xOffset   = 0;
            int yOffset   = 0;

            soundOutput_t soundOutput = {};

            soundOutput.sameplesPerSecond = 48000;
            soundOutput.hz                = 256;
            soundOutput.toneVolume        = 16000;
            soundOutput.wavePeriod        = soundOutput.sameplesPerSecond / soundOutput.hz;
            soundOutput.bytesPerSample    = sizeof(int16_t) * 2;
            soundOutput.secondaryBufferSize =
              soundOutput.sameplesPerSecond * soundOutput.bytesPerSample;
            soundOutput.latencySampleCount = soundOutput.sameplesPerSecond / 15;

            Win32_InitDSound(window, soundOutput.sameplesPerSecond,
                             soundOutput.secondaryBufferSize);
            Win32_FillSoundBuffer(&soundOutput, 0,
                                  soundOutput.latencySampleCount * soundOutput.bytesPerSample);
            globalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            while (globalRunning) {

                MSG message;

                /* Loop through messages from OS to my window */
                while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {

                    if (message.message == WM_QUIT) {
                        globalRunning = false;
                    }
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }

                // Input polling
                for (DWORD controllerIndex = 0; controllerIndex < XUSER_MAX_COUNT;
                     controllerIndex++) {

                    XINPUT_STATE controllerState;
                    if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS) {
                        XINPUT_GAMEPAD* pad = &controllerState.Gamepad;

                        bool c_Up        = (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool c_Down      = (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool c_Left      = (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool c_Right     = (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                        bool c_Start     = (pad->wButtons & XINPUT_GAMEPAD_START);
                        bool c_Back      = (pad->wButtons & XINPUT_GAMEPAD_BACK);
                        bool c_LThumb    = (pad->wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
                        bool c_RThumb    = (pad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
                        bool c_LShoulder = (pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                        bool c_RShoulder = (pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        bool c_A         = (pad->wButtons & XINPUT_GAMEPAD_A);
                        bool c_B         = (pad->wButtons & XINPUT_GAMEPAD_B);
                        bool c_X         = (pad->wButtons & XINPUT_GAMEPAD_X);
                        bool c_Y         = (pad->wButtons & XINPUT_GAMEPAD_Y);

                        int16_t moveX = pad->sThumbLX;
                        int16_t moveY = pad->sThumbLY;
                        int16_t camX  = pad->sThumbRX;
                        int16_t camY  = pad->sThumbRY;

                        xOffset += moveX / 4096;
                        yOffset -= moveY / 4096;

                        soundOutput.hz         = 512 + (int)(256.0f * ((float)moveX / 30000.0f));
                        soundOutput.wavePeriod = soundOutput.sameplesPerSecond / soundOutput.hz;

                        if (c_Up) {
                            yOffset -= 10;
                        }
                        if (c_Down) {
                            yOffset += 10;
                        }
                        if (c_Left) {
                            xOffset -= 10;
                        }
                        if (c_Right) {
                            xOffset += 10;
                        }

                    } else {
                        // Could be an error or could mean there aren't 4 controllers
                    }
                }

                Win32_RenderGradient(&globalBackbuffer, xOffset, yOffset);

                // DirectSound output test
                DWORD playCursor;
                DWORD writeCursor;
                if (SUCCEEDED(
                      globalSecondaryBuffer->GetCurrentPosition(&playCursor, &writeCursor))) {

                    DWORD byteToLock =
                      (soundOutput.runningSampleIndex * soundOutput.bytesPerSample) %
                      soundOutput.secondaryBufferSize;

                    DWORD TargetCursor = ((playCursor + (soundOutput.latencySampleCount *
                                                         soundOutput.bytesPerSample)) %
                                          soundOutput.secondaryBufferSize);
                    DWORD bytesToWrite;

                    if (byteToLock > TargetCursor) {
                        bytesToWrite = (soundOutput.secondaryBufferSize - byteToLock);
                        bytesToWrite += TargetCursor;
                    } else {
                        bytesToWrite = TargetCursor - byteToLock;
                    }

                    Win32_FillSoundBuffer(&soundOutput, byteToLock, bytesToWrite);
                }

                windowDimension_t dimension = Win32_GetWindowDimension(window);
                Win32_DisplayBufferInWindow(deviceContext, dimension.width, dimension.height,
                                            &globalBackbuffer, 0, 0);
            }

        } else {
            // TODO: logging
        }

    } else {
        // TODO: logging
    }

    return 0;
}