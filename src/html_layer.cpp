#include "composition.h"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iomanip>

#include "util.h"

using namespace std;

class HtmlApp : public CefApp, public CefBrowserProcessHandler 
{
public:
	HtmlApp() {
	}

	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
	{
		return this;
	}

	void OnBeforeCommandLineProcessing(
		CefString const& /*process_type*/,
		CefRefPtr<CefCommandLine> command_line) override
	{
		command_line->AppendSwitch("disable-gpu-vsync");
		//command_line->AppendSwitch("enable-begin-frame-scheduling");
	}

	virtual void OnContextInitialized() override
	{
	}

private:
	IMPLEMENT_REFCOUNTING(HtmlApp);
};

CefRefPtr<HtmlApp> app_;


class FrameBuffer
{
public:
	FrameBuffer(std::shared_ptr<d3d11::Device> const& device)
		: device_(device)
		, abort_(false)
		, swap_count_(0)
	{
	}

	void abort()
	{
		abort_ = true;
		swapped_.notify_one();
	}

	//
	// called in response to Cef's OnPaint notification
	//
	void on_paint(void* shared_handle)
	{
		{
			unique_lock<mutex> lock(lock_);
			swapped_.wait(lock, [&]() {
				return (abort_ || (swap_count_.load() == 0));
			});
		}

		if (!abort_)
		{
			lock_guard<mutex> guard(lock_);
			
			// did the shared texture change?
			if (shared_buffer_)
			{
				if (shared_handle != shared_buffer_->share_handle()) 
				{
					shared_buffer_.reset();
					back_buffer_.reset();
					front_buffer_.reset();
				}
			}

			// open the shared texture and create our back + 
			// front buffers (front and back textures are not shared)
			if (!shared_buffer_) 
			{
				shared_buffer_ = device_->open_shared_texture((void*)shared_handle);
				if (shared_buffer_)
				{
					auto const width = shared_buffer_->width();
					auto const height = shared_buffer_->height();
					auto const format = shared_buffer_->format();
					back_buffer_ = device_->create_texture(width, height, format, nullptr, 0);
					front_buffer_ = device_->create_texture(width, height, format, nullptr, 0);
				}
			}
		}

		swap_count_ = 1;
	}

	//
	// if there is a pending update from Cef, this method will
	// process it and signal that Cef can continue.
	//
	// this method will return the texture that should be considered 
	// the current frame (front)
	// 
	shared_ptr<d3d11::Texture2D> swap()
	{
		decltype(front_buffer_) front;
		decltype(back_buffer_) back;
		decltype(shared_buffer_) shared;

		{
			lock_guard<mutex> guard(lock_);
			front = front_buffer_;
			if (swap_count_)
			{
				auto tmp = front_buffer_;
				back_buffer_ = tmp;
				front_buffer_ = front = back_buffer_;
				shared = shared_buffer_;
				back = back_buffer_;
			}
		}
		
		// issue copy when necessary
		if (back && shared)
		{
			if (shared->lock_key(0))
			{
				back->copy_from(shared);
				shared->unlock_key(0);
			}
			swap_count_ = 0;
			swapped_.notify_one();
		}

		return front;
	}

private:

	mutex lock_;
	atomic_bool abort_;
	atomic_int32_t swap_count_;
	condition_variable swapped_;
	shared_ptr<d3d11::Texture2D> shared_buffer_;
	shared_ptr<d3d11::Texture2D> back_buffer_;
	shared_ptr<d3d11::Texture2D> front_buffer_;
	std::shared_ptr<d3d11::Device> const device_;
};



class HtmlView : public CefClient,
	public CefRenderHandler,
	public CefLifeSpanHandler
{
public:
	HtmlView(
			std::shared_ptr<d3d11::Device> const& device, 
			int width, 
			int height)
		: _width(width)
		, _height(height)
		, frame_buffer_(make_shared<FrameBuffer>(device))
	{
		_frame = 0;
		_fps_start = 0ull;
	}

	~HtmlView()
	{
		close();
	}

	void close()
	{
		// break out of a pending wait
		frame_buffer_->abort();

		decltype(_browser) browser;
		{
			lock_guard<mutex> guard(_lock);
			browser = _browser;
			_browser = nullptr;
		}

		if (browser.get()) {
			browser->GetHost()->CloseBrowser(true);
		}
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override 
	{ 
		return this; 
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override 
	{ 
		return this; 
	}

	void OnPaint(
		CefRefPtr<CefBrowser> /*browser*/,
		PaintElementType type,
		const RectList& dirtyRects,
		const void* buffer,
		int width,
		int height) override
	{
	}

	bool CanUseAcceleratedPaint(
		CefRefPtr<CefBrowser> /*browser*/) override
	{
		return true;
	}

	void OnAcceleratedPaint(
		CefRefPtr<CefBrowser> /*browser*/,
		PaintElementType type,
		const RectList& dirtyRects,
		void* buffer) override
	{
		_frame++;

		auto const now = time_now();
		if (!_fps_start) {
			_fps_start = now;
		}

		if ((now - _fps_start) > 1000000) 
		{
			auto const fps = _frame / double((now - _fps_start) / 1000000.0);

			log_message("html: OnPaint fps: %3.2f\n", fps);
			
			_frame = 0;
			_fps_start = time_now();
		}

		if (frame_buffer_) {
			frame_buffer_->on_paint((void*)buffer);
		}
	}

	bool GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) override
	{
		rect.Set(0, 0, _width, _height);
		return true;
	}

	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
	{
		if (!CefCurrentlyOn(TID_UI))
		{
			assert(0);
			return;
		}

		{
			lock_guard<mutex> guard(_lock);
			if (!_browser.get()) {
				_browser = browser;
			}
		}
	}

	shared_ptr<d3d11::Texture2D> texture()
	{
		return frame_buffer_->swap();
	}

private:
	IMPLEMENT_REFCOUNTING(HtmlView);

	int _width;
	int _height;

	uint32_t _frame;
	uint64_t _fps_start;

	shared_ptr<FrameBuffer> frame_buffer_;

	mutex _lock;
	CefRefPtr<CefBrowser> _browser;
};


class HtmlLayer : public Layer
{
public:
	HtmlLayer(
		std::shared_ptr<d3d11::Device> const& device,
		CefRefPtr<HtmlView> const& view)
		: Layer(device, true)
		, _view(view)
	{
	}

	~HtmlLayer()
	{
		if (_view) {
			_view->close();
		}
	}

	void render() override
	{
		if (_view) 
		{
			// simply use the base class method to draw our texture
			render_texture(_view->texture());
		}
	}

private:

	CefRefPtr<HtmlView> const _view;
	shared_ptr<d3d11::Texture2D> const _texture;
};

//
// use CEF to load and render a web page within a layer
//
shared_ptr<Layer> create_html_layer(
	std::shared_ptr<d3d11::Device> const& device,
	string const& url,
	int width, 
	int height)
{
	CefWindowInfo winfo;
	winfo.windowless_rendering_enabled = true;
	winfo.parent_window = nullptr;

	CefBrowserSettings settings;
	settings.windowless_frame_rate = 120;

	CefRefPtr<HtmlView> view(new HtmlView(device, width, height));

	CefBrowserHost::CreateBrowser(
			winfo, 
			view, 
			url, 
			settings, 
			nullptr);

	return make_shared<HtmlLayer>(device, view);
}

int cef_initialize(HINSTANCE instance)
{
	CefEnableHighDPISupport();

	CefMainArgs main_args(instance);

	app_ = CefRefPtr<HtmlApp>(new HtmlApp());

	int exit_code = CefExecuteProcess(main_args, app_, nullptr);
	if (exit_code >= 0) {
		return exit_code;
	}

	//MessageBox(0, L"Attach Debugger", L"CEF OSR", MB_OK);

	CefSettings settings;
	settings.no_sandbox = true;
	settings.multi_threaded_message_loop = true;

#if defined(NDEBUG)
	settings.single_process = false;
#else
	settings.single_process = true;
#endif

	app_ = CefRefPtr<HtmlApp>(new HtmlApp());

	CefInitialize(main_args, settings, app_, nullptr);
	
	return -1;
}

void cef_uninitialize()
{
	CefShutdown();
}

void cef_do_message_work()
{
	if (app_.get()) {
		CefDoMessageLoopWork();
	}
}

