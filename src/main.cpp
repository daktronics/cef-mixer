#include "platform.h"
#include "util.h"

#include "d3d11.h"
#include "composition.h"

#include "resource.h"

#include <fstream>
#include <sstream>

//
// if we're running on a system with hybrid graphics ... 
// try to force the selection of the high-performance gpu
//
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

class Window;

// globals .. yuck
bool show_devtools_ = false;
std::vector<Window*> windows_;


class Window
{
private:
	HINSTANCE const instance_;
	HWND hwnd_;
	std::shared_ptr<d3d11::Device> const device_;
	std::shared_ptr<d3d11::SwapChain> swapchain_;
	std::shared_ptr<Composition> const composition_;
	int sync_interval_;
	bool resize_;
	std::string const json_;
	
public:

	Window(
		HINSTANCE instance,
		std::shared_ptr<d3d11::Device> const& device, 
		std::shared_ptr<Composition> const& comp,
		std::string const& json)
		: instance_(instance)
		, device_(device)
		, composition_(comp) 
		, sync_interval_(1)
		, resize_(false)
		, json_(json)
	{
	}

	HWND hwnd() const {
		return hwnd_;
	}

	static Window* open(
		HINSTANCE instance, std::string const& json, int32_t width, int32_t height)
	{
		// create a D3D11 rendering device
		auto device = d3d11::create_device();
		if (!device) {
			return nullptr;
		}

		// create a composition to represent our 2D-scene
		auto const comp = create_composition(device, json);
		if (!comp) {
			return nullptr;
		}

		LPCWSTR class_name = L"_main_window_";

		WNDCLASSEXW wcex = {};
		wcex.cbSize = sizeof(WNDCLASSEX);
		if (!GetClassInfoEx(instance, class_name, &wcex))
		{
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
		}
		
		auto const self = new Window(instance, device, comp, json);

		std::string title("CEF OSR Mixer - ");
		title.append(cef_version());
		title.append(" - [gpu: ");
		title.append(device->adapter_name());
		title.append("]");		

		auto const hwnd = CreateWindow(class_name,
			to_utf16(title).c_str(),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
			nullptr,
			nullptr,
			instance,
			(LPVOID)self);

		if (IsWindow(hwnd))
		{
			// AdjustWindowRect can do something similar
			RECT rc_outer, rc_inner;
			GetWindowRect(hwnd, &rc_outer);
			GetClientRect(hwnd, &rc_inner);

			SetWindowPos(hwnd, nullptr, 0, 0,
				width + ((rc_outer.right - rc_outer.left) - (rc_inner.right - rc_inner.left)),
				height + ((rc_outer.bottom - rc_outer.top) - (rc_inner.bottom - rc_inner.top)),
				SWP_NOMOVE | SWP_NOZORDER);

			windows_.push_back(self);

			// make the window visible now that we have D3D11 components ready
			self->show();

			return self;
		}

		return nullptr;
	}

	void show() {
		ShowWindow(hwnd(), SW_SHOWNORMAL);
	}

	void tick(double t)
	{
		// update composition + layers based on time
		composition_->tick(t);
	}

	void render()
	{
		auto ctx = device_->immedidate_context();
		if (!ctx || !swapchain_) {
			return;
		}
		
		swapchain_->bind(ctx);

		// is there a request to resize ... if so, resize
		// both the swapchain and the composition
		if (resize_)
		{
			RECT rc;
			GetClientRect(hwnd(), &rc);
			auto const width = rc.right - rc.left;
			auto const height = rc.bottom - rc.top;
			if (width && height)
			{
				resize_ = false;
				composition_->resize(sync_interval_ != 0, width, height);
				swapchain_->resize(width, height);
			}
		}

		// clear the render-target
		swapchain_->clear(0.0f, 0.0f, 1.0f, 1.0f);

		// render our scene
		composition_->render(ctx);

		// present to window
		swapchain_->present(sync_interval_);
	}

private:

	static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
	{
		Window* self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (!self)
		{
			if (message == WM_NCCREATE)
			{
				self = reinterpret_cast<Window*>(((LPCREATESTRUCT)lp)->lpCreateParams);
				if (self) {
					self->hwnd_ = hwnd;
				}
				SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
			}
		}

		LRESULT lr = 0L;
		if (self)
		{
			if (message != WM_NCDESTROY)
			{
				if (self->wnd_proc_(message, wp, lp, lr)) {
					return lr;
				}
			}
			else
			{
				SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(0));
				delete self;
			}		
		}

		lr = DefWindowProc(hwnd, message, wp, lp);
		return lr;
	}

	bool wnd_proc_(UINT message, WPARAM wp, LPARAM lp, LRESULT result)
	{
		switch (message)
		{
			case WM_CREATE:
				on_create();
				break;

			case WM_PAINT:
			{
				PAINTSTRUCT ps;
				BeginPaint(hwnd(), &ps);
				EndPaint(hwnd(), &ps);
			}
			break;

			case WM_COMMAND:
				switch (LOWORD(wp))
				{
					case ID_WINDOW_NEW:
						on_new_window();
						break;
					case ID_WINDOW_VSYNC:
						sync_interval_ = sync_interval_ ? 0 : 1;
						resize_ = true;
						break;
					case ID_VIEW_DEVTOOLS:
						//show_devtools_ = true;
						break;
					default: break;
				}
				break;

			case WM_LBUTTONDOWN: 
				on_mouse_click(MouseButton::Left, false, lp);
				break;
			case WM_LBUTTONUP: 
				on_mouse_click(MouseButton::Left, true, lp);
				break;
			case WM_RBUTTONDOWN: 
				on_mouse_click(MouseButton::Right, false, lp);
				break;
			case WM_RBUTTONUP: 
				on_mouse_click(MouseButton::Right, true, lp);
				break;

			case WM_MOUSEMOVE: on_mouse_move(false, lp);
				break;

			case WM_SIZE:
				// signal that we want a resize of output
				resize_ = true;
				break;

			case WM_DESTROY:
				PostQuitMessage(0);
				break;

			default: break;
		}

		return 0;
	}

	void on_create()
	{
		// create a D3D11 swapchain for the window
		swapchain_ = device_->create_swapchain(hwnd());
	}

	void on_new_window()
	{
		RECT rc;
		GetClientRect(hwnd(), &rc);
		Window::open(instance_, json_, rc.right - rc.left, rc.bottom - rc.top);
	}

	//
	// forward a Windows WM_XXX mouse Up/Down notification to the layers
	//
	void on_mouse_click(MouseButton button, bool up, LPARAM lp)
	{
		auto const x = ((int)(short)LOWORD(lp));
		auto const y = ((int)(short)HIWORD(lp));
		if (composition_) {
			composition_->mouse_click(button, up, x, y);
		}
	}

	//
	// forward a Windows WM_XXX mouse move notification to the layers
	//
	void on_mouse_move(bool leave, LPARAM lp)
	{
		auto const x = ((int)(short)LOWORD(lp));
		auto const y = ((int)(short)HIWORD(lp));
		if (composition_) {
			composition_->mouse_move(leave, x, y);
		}
	}

};


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
	int grid_x = 1;
	int grid_y = 1;

	bool view_source = false;

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

				std::string key, value;
				auto const eq = option.find('=');
				if (eq != std::string::npos)
				{
					key = option.substr(0, eq);
					value = option.substr(eq + 1);
				}
				else {
					key = option;
				}

				if (key == "width") {
					width = to_int(value, 0);
				}
				else if (key == "height") {
					height = to_int(value, 0);
				}
				else if (key == "grid") {

					// split on x (eg. 2x3)
					auto const c = value.find('x');
					if (c != std::string::npos) {
						grid_x = to_int(value.substr(0, c), 0);
						grid_y = to_int(value.substr(c + 1), 0);
					}
					else {
						grid_x = grid_y = to_int(value, 0);
					}
				}
				else if (key == "view-source") {
					view_source = true;
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
	
	std::string json;

	// if the url given on the command line is actually a local file ... 
	// assume it is a .json file describing our layers
	auto const json_file = locate_media(url);
	if (json_file)
	{
		std::ifstream fin(*json_file);
		if (fin.is_open()) {
			json.assign((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
		}
	}
	
	// if no JSON yet, then use command-line params to
	// generate a default JSON layer description
	if (json.empty())
	{
		std::stringstream builder;
		builder << "{" << std::endl;
		builder << "  \"width\":" << width << "," << std::endl;
		builder << "  \"height\":" << height << "," << std::endl;
		builder << "  \"layers\":[" << std::endl;

		if (grid_x > 0 && grid_y > 0)
		{
			//
			// if grid=2x2 then a 2x2 grid of html views will be added
			//
			// +-------+-------+
			// |       |       |
			// +-------+-------+
			// |       |       |
			// +-------+-------+
			//

			// create a grid of html layer(s) depending on our --grid option
			// (easy way to test several active views)		
			float cx = 1.0f / grid_x;
			float cy = 1.0f / grid_y;
			for (int x = 0; x < grid_x; ++x)
			{
				for (int y = 0; y < grid_y; ++y)
				{
					builder << "    {" << std::endl;
					builder << "      \"type\":\"web\"," << std::endl;
					builder << "      \"src\":\"" << url << "\"," << std::endl;
					builder << "      \"left\":" << (x * cx) << "," << std::endl;
					builder << "      \"top\":" << (y * cy) << "," << std::endl;
					builder << "      \"width\":" << cx << "," << std::endl;
					builder << "      \"height\":" << cy << "," << std::endl;
					builder << "      \"want_input\":true," << std::endl;
					builder << "      \"view_source\":" << (view_source ? "true" : "false");					
					builder << "    }," << std::endl;
				}
			}
		}

		// add an image overlay layer
		builder << "    { \"type\":\"image\", \"src\":\"resource/overlay.png\" }," << std::endl;

		// add a HUD layer to show stats
		auto const hud_file = locate_media("resource/hud.html");
		if (hud_file) {
			// (we need to convert to file:/// url for CEF
			auto const hud_url = to_file_url(*hud_file);
			if (!hud_url.empty()) 
			{
				builder << "    { \"type\":\"web\"," << std::endl
					     << "      \"src\":\"" << hud_url << "\"," << std::endl
					     << "      \"top\":0.95," << std::endl
					     << "      \"height\":0.05," << std::endl
						  << "      \"view_source\":" << (view_source ? "true" : "false")
					     << "    }" << std::endl;
			}
		}

		builder << "  ]" << std::endl << "}";
		json = builder.str();
	}

	// create the first top-level window 
	// (additional can be opened with Ctrl+W)
	auto const window = Window::open(instance, json, width, height);
	if (!window) 
	{
		assert(0);
		cef_uninitialize();
		return 0;
	}
	
	// load keyboard accelerators
	HACCEL accel_table = 
		LoadAccelerators(instance, MAKEINTRESOURCE(IDR_APPLICATION));

	auto const start_time = time_now();

	// main message pump for our application
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (!TranslateAccelerator(msg.hwnd, accel_table, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else
		{
			auto const t = (time_now() - start_time) / 1000000.0;
			for (auto const& w : windows_) 
			{
				w->tick(t);
				w->render();
			}
		}
	}

	// force layers to be destroyed
	//composition_.reset();

	cef_uninitialize();
	return 0;
}