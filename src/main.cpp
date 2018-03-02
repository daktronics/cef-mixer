#include "platform.h"
#include "util.h"

#include "d3d11.h"
#include "composition.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
				name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
				processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HWND create_window(HINSTANCE, LPCWSTR, int width, int height);
LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);

//
// simple RIAA for CoInitialize/CoUninitialize
//
class ComInitializer
{
public:
	ComInitializer() {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	}
	~ComInitializer() { CoUninitialize(); }
};


int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	// if cef_initialize returns >= 0; then we ran as a child
	// process and we're done here
	auto const exit_code = cef_initialize(instance);
	if (exit_code >= 0) {
		return exit_code;
	}

	std::string url;
	int width = 0;
	int height = 0;

	// read options from the command-line
	int args;
	LPWSTR* arg_list = CommandLineToArgvW(GetCommandLineW(), &args);
	if (arg_list)
	{
		for (int n = 1; n < args; ++n)
		{
			auto option = to_utf8(arg_list[n]);
			if (option.substr(0, 2) != "--")
			{
				url = option;			
			}
			else
			{
				option = option.substr(2);
				auto const eq = option.find('=');
				if (eq != std::string::npos) 
				{
					auto const key = option.substr(0, eq);
					auto const value = option.substr(eq+1);
					if (key == "width") {
						width = to_int(value, 0);
					}
					else if (key == "height") {
						height = to_int(value, 0);
					}
				}
			}
		}
	}

	// default to webgl aquarium demo
	if (url.empty()) {
		url = "https://webglsamples.org/aquarium/aquarium.html";
	}
	if (width <= 0) {
		width = 1280;
	}
	if (height <= 0) {
		height = 720;
	}

	// this demo uses WIC to load images .. so we need COM
	ComInitializer com_init;

	// create a window with our specific size
	auto const window = create_window(
		instance, L"CEF Accelerated-OSR Mixer", width, height);
	if (!IsWindow(window)) {
		assert(0);
		cef_uninitialize();
		return 0;
	}

	// create a D3D11 rendering device
	auto device = d3d11::create_device();
	if (!device) {
		assert(0);
		cef_uninitialize();
		return 0;
	}

	// create a D3D11 swapchain for the window
	auto swapchain = device->create_swapchain(window);
	if (!swapchain) {
		assert(0);
		cef_uninitialize();
		return 0;
	}

	{
		// create a composition to represent our 2D-scene
		auto const composition = std::make_shared<Composition>(device);

		// create a html layer
		auto const html = create_html_layer(device, url, width, height);
		if (html)
		{
			composition->add_layer(html);
			html->move(Anchor::TopLeft, 0.0f, 0.0f, 1.0f, 1.0f);
		}

		// create a image layer
		auto const img = create_image_layer(device, "c:\\balloon.jpg");
		if (img)
		{
			//composition->add_layer(img);
			//img->move(0.5f, 0.5f, 0.5f, 0.5f);
		}

		// make the window visible now that we have D3D11 components ready
		ShowWindow(window, SW_NORMAL);

		auto fps_start = time_now();
		uint32_t frame = 0;

		// main message pump for our application
		MSG msg = {};
		while (msg.message != WM_QUIT)
		{
			//cef_do_message_work();

			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				// clear the render-target
				swapchain->clear(0.0f, 0.0f, 1.0f, 1.0f);

				// render our scene
				composition->render();

				// present to window
				swapchain->present(1);

				frame++;
				auto const now = time_now();
				if ((now - fps_start) > 1000000)
				{
					auto const fps = frame / double((now - fps_start) / 1000000.0);
					log_message("compositor: fps: %3.2f\n", fps);
					frame = 0;
					fps_start = time_now();
				}

				//Sleep(2);
			}
		}
	}

	cef_uninitialize();
	return 0;
}

HWND create_window(HINSTANCE instance, LPCWSTR title, int width, int height)
{
	LPCWSTR class_name = L"_main_window_";

	WNDCLASSEXW wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = wnd_proc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOWTEXT + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = class_name;
	wcex.hIconSm = nullptr;
	if (!RegisterClassExW(&wcex)) {
		return nullptr;
	}

	auto const hwnd = CreateWindow(class_name,
						title,
						WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 
						nullptr, 
						nullptr, 
						instance, 
						nullptr);

	// AdjustWindowRect can do something similar
	RECT rc_outer, rc_inner;
	GetWindowRect(hwnd, &rc_outer);
	GetClientRect(hwnd, &rc_inner);

	SetWindowPos(hwnd, nullptr, 0, 0, 
			width + ((rc_outer.right - rc_outer.left) - (rc_inner.right - rc_inner.left)),
			height + ((rc_outer.bottom - rc_outer.top) - (rc_inner.bottom - rc_inner.top)),
			SWP_NOMOVE | SWP_NOZORDER);

	return hwnd;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				BeginPaint(hwnd, &ps);
				EndPaint(hwnd, &ps);
			}
			break;

		case WM_SIZE:
			// todo: resize the swapchain here
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default: 
			return DefWindowProc(hwnd, msg, wparam, lparam);
	}
	return 0;
}