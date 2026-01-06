//
// Created by Administrator on 2026/1/7.
//

#include <jni.h>
#include <thread>
#include <EGL/egl.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "Logger/Logger.h"
#include "dobby.h"
#include "utils.h"
#include "xdl.h"
#include "Font.h"
#include "Customize.h"

static uint64_t Il2CppBase{};
static bool initImGui = false;
static float screenWidth = -1, screenHeight = -1;
static int glWidth = 0, glHeight = 0;

void (*orig_initializeMotionEvent)(void *a1, void *a2, void *a3);
void initializeMotionEvent(void *a1, void *a2, void *a3){
    orig_initializeMotionEvent(a1,a2,a3);
    if (initImGui) {
        ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)a1,{screenWidth / glWidth, screenHeight / glHeight});
    }
}

int32_t (*orig_ANativeWindow_getWidth)(ANativeWindow* window);
int32_t _ANativeWindow_getWidth(ANativeWindow* window) {
    screenWidth = orig_ANativeWindow_getWidth(window);
    return orig_ANativeWindow_getWidth(window);
}

int32_t (*orig_ANativeWindow_getHeight)(ANativeWindow* window);
int32_t _ANativeWindow_getHeight(ANativeWindow* window) {
    screenHeight = orig_ANativeWindow_getHeight(window);
    return orig_ANativeWindow_getHeight(window);
}

//初始化ImGui
void setUpImGui(){
    ImGui::CreateContext();
    ImGuiIO & io = ImGui::GetIO();
    io.IniFilename = NULL;
    ImGui::StyleColorsClassic();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImFontConfig font_cfg;
    font_cfg.SizePixels = 16.0f;
    io.Fonts->AddFontFromMemoryTTF((void *)OPPOSans_H, OPPOSans_H_size, 32.0f, NULL,io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontDefault(&font_cfg);
    ImGui::GetStyle().ScaleAllSizes(3.0f);
    ImGui::GetStyle().WindowMinSize = ImVec2(screenHeight*0.75f, screenWidth*0.20f);	// 设置窗口高宽
    LOGI("[+] setUpImGui done!");
    initImGui = true;
}

EGLBoolean(*orig_eglSwapBuffers) (EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    eglQuerySurface(dpy, surface, EGL_WIDTH, &glWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);

    if (glWidth<=0 || glHeight<=0)
    {
         LOGE("EGL 失败");
        return orig_eglSwapBuffers(dpy, surface);
    }

    if (!initImGui)
        setUpImGui();

    LOGD("screenWidth:%.f, screenHeight:%.f", screenWidth, screenHeight);
    LOGD("glWidth:%d, glHeight:%d", glWidth, glHeight);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(glWidth, glHeight);
    ImGui::NewFrame();

    static bool Iswitch = false;
    static bool button = false;

    ImGui::SetNextWindowSize(ImVec2(500, 500));
    ImGui::Begin("Tomatosauce9");

    ImGui::Text("这是一个文本!");
    if(ImGui::Button("这是一个按钮")){
        //按下按钮...
        button = !button;
    }
    if(button){
        //ImGui Demo视窗
        ImGui::ShowDemoWindow();
    }
    ImGui::Checkbox("这是一个开关", &Iswitch);
    ImGui::Text("Iswitch状态 : %s!", Iswitch ? "开" : "关");

    ImGui::End();


    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return orig_eglSwapBuffers(dpy, surface);
}


void *hack_thread(void*) {
    //检查il2cpp.so是否加载 -- 这个可以根据你所需而更改我当下拿unity小游戏测试的
    do {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        LOGD("等待libil2cpp.so加载");
    } while (!isLibraryLoaded("libil2cpp.so"));

    //获取libil2cpp.so模块地址
    Il2CppBase = findLibrary("libil2cpp.so");
    LOGI("Il2CppBase: %lx", Il2CppBase);

    //进行EGLhook前置操作
    //加载so的
    void *handle_egl = xdl_open("libEGL.so", XDL_DEFAULT);
    void *handle_input = xdl_open("libinput.so", XDL_DEFAULT);

    if(handle_egl == nullptr || handle_input == nullptr){
        LOGE("[-] 加载失败!");
        LOGE("[-] handle_egl:%lx, handle_input:%lx", handle_egl, handle_input);
        return nullptr;
    }

    //透过符号获取函数地址
    void *xdl_sym_egl = xdl_sym(handle_egl, "eglSwapBuffers", nullptr);
    void *xdl_sym_input = xdl_sym(handle_input, "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE", nullptr);

    if(xdl_sym_egl == nullptr || xdl_sym_input == nullptr){
        LOGE("[-] 获取失败!");
        LOGE("[-] xdl_sym_egl:%lx, xdl_sym_input:%lx", xdl_sym_egl, xdl_sym_input);
        return nullptr;
    }

    //使用hook框架对函数hook
    if(DobbyHook(xdl_sym_egl, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers) == RT_FAILED)
        LOGE("[-] hook_eglSwapBuffers 失败!");
    if(DobbyHook(xdl_sym_input, (void*)initializeMotionEvent, (void**)&orig_initializeMotionEvent) == RT_FAILED)
        LOGE("[-] initializeMotionEvent 失败!");

    if(DobbyHook((void *) DobbySymbolResolver("/system/lib/libandroid.so", "ANativeWindow_getWidth"), (void *) _ANativeWindow_getWidth, (void **) &orig_ANativeWindow_getWidth) == RT_FAILED)
        LOGE("[-] _ANativeWindow_getWidth 失败!");
    if(DobbyHook((void *) DobbySymbolResolver("/system/lib/libandroid.so", "ANativeWindow_getHeight"), (void *) _ANativeWindow_getHeight, (void **) &orig_ANativeWindow_getHeight) == RT_FAILED)
        LOGE("[-] _ANativeWindow_getHeight 失败!");

    LOGI("[+] hack done!");
    return nullptr;
}

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void *key) {

    LOGI("[+] 进入JNI_OnLoad");

    JNIEnv *env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
    //        LOGD("JavaEnv: %p.", env);
    }

    pthread_t hacks;
    pthread_create(&hacks, NULL, hack_thread, NULL);

    return JNI_VERSION_1_6;
}