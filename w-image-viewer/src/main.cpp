#include "pch.h"
#include "include\global.h"
#include "window.h"
#include "include\helpers.h"
#include "include\ensure.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    g_config.read();
    g_config.read_slideshow();
    std::wstring startup_file;
    if (lpCmdLine[0] != L'\0') {
        int argc = 0;
        auto argv = CommandLineToArgvW(lpCmdLine, &argc);
        if (argv && argc > 0) startup_file = argv[0];
        LocalFree(argv);
    }
    auto window = std::make_unique<Window>(hInstance, nCmdShow, startup_file);

    return window->message_loop();
}
