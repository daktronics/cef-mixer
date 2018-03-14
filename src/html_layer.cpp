#include "composition.h"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
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
	}

	virtual void OnContextInitialized() override
	{
	}

private:

	IMPLEMENT_REFCOUNTING(HtmlApp);
};


class FrameBuffer
{
public:
	FrameBuffer(std::shared_ptr<d3d11::Device> const& device)
		: device_(device)
		, abort_(false)
		, swap_count_(0)
		, sync_key_(0)
	{
	}

	void abort()
	{
		abort_ = true;
		swapped_.notify_one();
	}

	//
	// called in response to Cef's OnAcceleratedPaint notification
	//
	void on_paint(void* shared_handle, uint64_t sync_key)
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

		sync_key_ = sync_key;
		swap_count_ = 1;
	}

	//
	// if there is a pending update from Cef, this method will
	// process it and signal that Cef can continue.
	//
	// this method will return the texture that should be considered 
	// the current frame (front)
	// 
	shared_ptr<d3d11::Texture2D> swap(shared_ptr<d3d11::Context> const& ctx)
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
			if (shared->lock_key(sync_key_))
			{
				d3d11::ScopedBinder<d3d11::Texture2D> texture_binder(ctx, back);
				back->copy_from(shared);

				shared->unlock_key(sync_key_);
			}
		}

		swap_count_ = 0;
		swapped_.notify_one();

		return front;
	}

private:

	mutex lock_;
	atomic_bool abort_;
	atomic_int32_t swap_count_;
	condition_variable swapped_;
	uint64_t sync_key_;
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
	HtmlView(std::shared_ptr<d3d11::Device> const& device, 
			int width, 
			int height)
		: width_(width)
		, height_(height)
		, frame_buffer_(make_shared<FrameBuffer>(device))
	{
		frame_ = 0;
		fps_start_ = 0ull;
	}

	~HtmlView() {
		close();
	}

	void close()
	{
		// break out of a pending wait
		frame_buffer_->abort();

		// get thread-safe reference
		decltype(browser_) browser;
		{
			lock_guard<mutex> guard(lock_);
			browser = browser_;
			browser_ = nullptr;
		}

		if (browser.get()) {
			browser->GetHost()->CloseBrowser(true);
		}

		log_message("html view is closed\n");
	}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { 
		return this; 
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { 
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
		// this application doesn't support software rasterizing
	}

	void OnAcceleratedPaint(
		CefRefPtr<CefBrowser> /*browser*/,
		PaintElementType type,
		const RectList& dirtyRects,
		void* share_handle, 
		uint64 sync_key) override
	{
		if (type == PET_POPUP) {
			return;
		}

		frame_++;

		auto const now = time_now();
		if (!fps_start_) {
			fps_start_ = now;
		}

		if ((now - fps_start_) > 1000000)
		{
			auto const fps = frame_ / double((now - fps_start_) / 1000000.0);

			log_message("html: OnPaint fps: %3.2f\n", fps);
			
			frame_ = 0;
			fps_start_ = time_now();
		}

		if (frame_buffer_) {
			frame_buffer_->on_paint((void*)share_handle, sync_key);
		}
	}

	bool GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) override
	{
		rect.Set(0, 0, width_, height_);
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
			lock_guard<mutex> guard(lock_);
			if (!browser_.get()) {
				browser_ = browser;
			}
		}
	}

	bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		const CefString& target_url,
		const CefString& target_frame_name,
		WindowOpenDisposition target_disposition,
		bool user_gesture,
		const CefPopupFeatures& popupFeatures,
		CefWindowInfo& windowInfo,
		CefRefPtr<CefClient>& client,
		CefBrowserSettings& settings,
		bool* no_javascript_access) override 
	{
		// todo: add support for accelerated popups in this app
		//windowInfo.SetAsWindowless(nullptr);
		//windowInfo.shared_textures_enabled = true;

		return false;
	}

	shared_ptr<d3d11::Texture2D> texture(shared_ptr<d3d11::Context> const& ctx)
	{
		return frame_buffer_->swap(ctx);
	}

private:
	IMPLEMENT_REFCOUNTING(HtmlView);

	int width_;
	int height_;
	uint32_t frame_;
	uint64_t fps_start_;
	shared_ptr<FrameBuffer> frame_buffer_;
	mutex lock_;
	CefRefPtr<CefBrowser> browser_;
};


class HtmlLayer : public Layer
{
public:
	HtmlLayer(
		std::shared_ptr<d3d11::Device> const& device,
		CefRefPtr<HtmlView> const& view)
		: Layer(device, true)
		, view_(view)
	{
	}

	~HtmlLayer()
	{
		if (view_) {
			view_->close();
		}
	}

	void render(shared_ptr<d3d11::Context> const& ctx) override
	{
		if (view_) 
		{
			// simply use the base class method to draw our texture
			render_texture(ctx, view_->texture(ctx));
		}
	}

private:

	CefRefPtr<HtmlView> const view_;
};

//
// Lifetime management for CEF components.  
//
// Manages the CEF message loop and CefInitialize/CefShutdown
//
class CefModule
{
public:
	CefModule(HINSTANCE mod) : module_(mod) {
		ready_ = false;
	}

	static void startup(HINSTANCE);
	static void shutdown();

private:

	//
	// simple CefTask we'll post to our message-pump 
	// thread to stop it (required to break out of CefRunMessageLoop)
	//
	class QuitTask : public CefTask
	{
	public:
		QuitTask() { }
		void Execute() override {
			CefQuitMessageLoop();
		}
		IMPLEMENT_REFCOUNTING(QuitTask);
	};

	void message_loop();

	condition_variable signal_;
	atomic_bool ready_;
	mutex lock_;
	HINSTANCE const module_;
	shared_ptr<thread> thread_;
	static shared_ptr<CefModule> instance_;
};

std::shared_ptr<CefModule> CefModule::instance_;

void CefModule::startup(HINSTANCE mod)
{
	assert(!instance_.get());
	instance_ = make_shared<CefModule>(mod);
	instance_->thread_ = make_shared<thread>(
		bind(&CefModule::message_loop, instance_.get()));

	{ // wait for message loop to initialize

		unique_lock<mutex> lock(instance_->lock_);
		weak_ptr<CefModule> weak_self(instance_);
		instance_->signal_.wait(lock, [weak_self]() {
			auto const mod = weak_self.lock();
			if (mod) {
				return mod->ready_.load();
			}
			return true;
		});
	}

	log_message("cef module is ready\n");
}

void CefModule::shutdown()
{
	assert(instance_.get());
	if (instance_)
	{
		if (instance_->thread_)
		{
			CefRefPtr<CefTask> task(new QuitTask());
			CefPostTask(TID_UI, task.get());
			instance_->thread_->join();
			instance_->thread_.reset();
		}
		instance_.reset();
	}
}

void CefModule::message_loop()
{
	log_message("cef initializing ... \n");

	CefSettings settings;
	settings.no_sandbox = true;
	settings.multi_threaded_message_loop = false;
	settings.windowless_rendering_enabled = true;

#if defined(NDEBUG)
	settings.single_process = false;
#else
	// ~RenderProcessHostImpl() complains about DCHECK(is_self_deleted_)
	// when we run single process mode ... I haven't figured out how to resolve yet
	//settings.single_process = true;
#endif

	CefRefPtr<HtmlApp> app(new HtmlApp());

	CefMainArgs main_args(module_);
	CefInitialize(main_args, settings, app, nullptr);

	log_message("cef is initialized.\n");

	// signal cef is initialized and ready
	ready_ = true;
	signal_.notify_one();
	
	CefRunMessageLoop();

	log_message("cef shutting down ... \n");

	CefShutdown();
	log_message("cef is shutdown\n");
}

//
// use CEF to load and render a web page within a layer
//
shared_ptr<Layer> create_html_layer(
	std::shared_ptr<d3d11::Device> const& device,
	string const& url,
	int width, 
	int height)
{
	CefWindowInfo window_info;
	window_info.SetAsWindowless(nullptr);
	window_info.shared_textures_enabled = true;

	CefBrowserSettings settings;

	// this value is meaningless when using shared textures
	settings.windowless_frame_rate = 120;

	CefRefPtr<HtmlView> view(new HtmlView(device, width, height));

	CefBrowserHost::CreateBrowser(
			window_info,
			view, 
			url, 
			settings, 
			nullptr);

	return make_shared<HtmlLayer>(device, view);
}

//
// public method to setup CEF for this application
//
int cef_initialize(HINSTANCE instance)
{
	CefEnableHighDPISupport();

	CefMainArgs main_args(instance);

	int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
	if (exit_code >= 0) {
		return exit_code;
	}

	//MessageBox(0, L"Attach Debugger", L"CEF OSR", MB_OK);

	CefModule::startup(instance);
	return -1;
}

//
// public method to tear-down CEF ... call before your main() function exits
//
void cef_uninitialize()
{
	CefModule::shutdown();
}

