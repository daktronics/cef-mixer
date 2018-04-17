#include "composition.h"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_version.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <sstream>
#include <iomanip>
#include <functional>
#include <map>

#include "util.h"

using namespace std;

//
// helper function to convert a 
// CefDictionaryValue to a CefV8Object
//
CefRefPtr<CefV8Value> to_v8object(CefRefPtr<CefDictionaryValue> const& dictionary)
{
	auto const obj = CefV8Value::CreateObject(nullptr, nullptr);
	if (dictionary) 
	{
		auto const attrib = V8_PROPERTY_ATTRIBUTE_READONLY;
		CefDictionaryValue::KeyList keys;
		dictionary->GetKeys(keys);
		for (auto const& k : keys)
		{
			auto const type = dictionary->GetType(k);
			switch (type)
			{
				case VTYPE_BOOL: obj->SetValue(k,
					CefV8Value::CreateBool(dictionary->GetBool(k)), attrib);
					break;
				case VTYPE_INT: obj->SetValue(k,
					CefV8Value::CreateInt(dictionary->GetInt(k)), attrib);
					break;
				case VTYPE_DOUBLE: obj->SetValue(k, 
					CefV8Value::CreateDouble(dictionary->GetDouble(k)), attrib);
					break;
				case VTYPE_STRING: obj->SetValue(k,
					CefV8Value::CreateString(dictionary->GetString(k)), attrib);
					break;

				default: break;
			}
		}
	}
	return obj;
}

//
// V8 handler for our 'mixer' object available to javascript
// running in a page within this application
//
class MixerHandler : 
			public CefV8Accessor
{
public:
	MixerHandler(
		CefRefPtr<CefBrowser> const& browser,
		CefRefPtr<CefV8Context> const& context)
		: browser_(browser)
		, context_(context)
	{
		auto window = context->GetGlobal();
		auto const obj = CefV8Value::CreateObject(this, nullptr);
		obj->SetValue("requestStats", V8_ACCESS_CONTROL_DEFAULT, V8_PROPERTY_ATTRIBUTE_NONE);
		window->SetValue("mixer", obj, V8_PROPERTY_ATTRIBUTE_NONE);
	}

	void update(CefRefPtr<CefDictionaryValue> const& dictionary)
	{
		if (!request_stats_) {
			return;
		}

		context_->Enter();
		CefV8ValueList values;
		values.push_back(to_v8object(dictionary));
		request_stats_->ExecuteFunction(request_stats_, values);
		context_->Exit();
	}

	bool Get(const CefString& name,
		const CefRefPtr<CefV8Value> object,
		CefRefPtr<CefV8Value>& retval,
		CefString& /*exception*/) override {

		if (name == "requestStats" && request_stats_ != nullptr) {
			retval = request_stats_;
			return true;
		}

		// Value does not exist.
		return false;
	}

	bool Set(const CefString& name,
		const CefRefPtr<CefV8Value> object,
		const CefRefPtr<CefV8Value> value,
		CefString& /*exception*/) override
	{
		if (name == "requestStats") {
			request_stats_ = value;

			// notify the browser process that we want stats
			auto message = CefProcessMessage::Create("mixer-request-stats");
			if (message != nullptr && browser_ != nullptr) {
				browser_->SendProcessMessage(PID_BROWSER, message);
			}
			return true;
		}
		return false;
	}

	IMPLEMENT_REFCOUNTING(MixerHandler);

private:

	CefRefPtr<CefBrowser> const browser_;
	CefRefPtr<CefV8Context> const context_;
	CefRefPtr<CefV8Value> request_stats_;
};


class HtmlApp : public CefApp, 
					public CefBrowserProcessHandler,
	            public CefRenderProcessHandler
{
public:
	HtmlApp() {
	}

	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
		return this;
	}

	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
		return this;
	}

	void OnBeforeCommandLineProcessing(
		CefString const& /*process_type*/,
		CefRefPtr<CefCommandLine> command_line) override
	{
		// disable creation of a GPUCache/ folder on disk
		command_line->AppendSwitch("disable-gpu-shader-disk-cache");
		
		// un-comment to show the built-in Chromium fps meter
		//command_line->AppendSwitch("show-fps-counter");		

		//command_line->AppendSwitch("disable-gpu-vsync");

		// Most systems would not need to use this switch - but on older hardware, 
		// Chromium may still choose to disable D3D11 for gpu workarounds.
		// Accelerated OSR will not at all with D3D11 disabled, so we force it on.
		//
		// See the discussion on this issue:
		// https://github.com/daktronics/cef-mixer/issues/10
		//
		command_line->AppendSwitchWithValue("use-angle", "d3d11");

		// tell Chromium to autoplay <video> elements without 
		// requiring the muted attribute or user interaction
		command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
	}

	virtual void OnContextInitialized() override {
	}

	//
	// CefRenderProcessHandler::OnContextCreated
	//
	// Adds our custom 'mixer' object to the javascript context running
	// in the render process
	//
	void OnContextCreated(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefV8Context> context) override
	{
		mixer_handler_ = new MixerHandler(browser, context);
	}

	//
	// CefRenderProcessHandler::OnBrowserDestroyed
	//
	void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override
	{
		mixer_handler_ = nullptr;
	}

	//
	// CefRenderProcessHandler::OnProcessMessageReceived
	//
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
		CefProcessId /*source_process*/,
		CefRefPtr<CefProcessMessage> message) 
	{
		auto const name = message->GetName().ToString();
		if (name == "mixer-update-stats")
		{
			if (mixer_handler_ != nullptr)
			{
				// we expect just 1 param that is a dictionary of stat values
				auto const args = message->GetArgumentList();
				auto const size = args->GetSize();
				if (size > 0) {
					auto const dict = args->GetDictionary(0);
					if (dict && dict->GetSize() > 0) {
						mixer_handler_->update(dict);
					}
				}
			}
			return true;
		}
		return false;
	}

private:

	IMPLEMENT_REFCOUNTING(HtmlApp);

	CefRefPtr<MixerHandler> mixer_handler_;
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

					log_message("creating texture buffers - %dx%d\n", width, height);

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
			if (shared->lock_key(sync_key_, 100))
			{
				d3d11::ScopedBinder<d3d11::Texture2D> texture_binder(ctx, back);
				back->copy_from(shared);
				shared->unlock_key(sync_key_);
			}
			else {
				log_message("could not lock texture\n");
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
		int width, int height, bool send_begin_Frame)
		: width_(width)
		, height_(height)
		, frame_buffer_(make_shared<FrameBuffer>(device))
		, needs_stats_update_(false)
		, send_begin_frame_(send_begin_Frame)
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

	bool OnProcessMessageReceived(
		CefRefPtr<CefBrowser> /*browser*/,
		CefProcessId /*source_process*/,
		CefRefPtr<CefProcessMessage> message) override
	{
		auto name = message->GetName().ToString();
		if (name == "mixer-request-stats")
		{
			// just flag that we need to deliver stats updates
			// to the render process via a message
			needs_stats_update_ = true;
			return true;
		}
		return false;
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

			log_message("html: OnAcceleratedPaint fps: %3.2f\n", fps);
			
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

	void tick(shared_ptr<Composition> const& composition, double t)
	{
		decltype(browser_) browser;
		{
			lock_guard<mutex> guard(lock_);
			browser = browser_;
		}

		// the javascript might be interested in our 
		// rendering statistics (e.g. HUD) ...
		if (needs_stats_update_) {
			update_stats(browser, composition);
		}

		// optionally issue a BeginFrame request
		if (send_begin_frame_ && browser) {		
			browser->GetHost()->SendExternalBeginFrame();
		}
	}

	void update_stats(CefRefPtr<CefBrowser> const& browser, 
			shared_ptr<Composition> const& composition)
	{
		if (!browser || !composition) {
			return;
		}

		auto message = CefProcessMessage::Create("mixer-update-stats");
		auto args = message->GetArgumentList();
		
		// create a dictionary to hold all of individual statistic values
		// it will get converted by the Render process into a V8 Object
		// that gets passed to the script running in the page
		auto dict = CefDictionaryValue::Create();
		dict->SetInt("width", composition->width());
		dict->SetInt("height", composition->height());
		dict->SetDouble("fps", composition->fps());
		dict->SetDouble("time", composition->time());
		dict->SetBool("vsync", composition->is_vsync());

		args->SetDictionary(0, dict);

		browser->SendProcessMessage(PID_RENDERER, message);
	}

	void resize(int width, int height)
	{
		// only signal change if necessary
		if (width != width_ || height != height_)
		{
			decltype(browser_) browser;
			{
				lock_guard<mutex> guard(lock_);
				browser = browser_;
			}

			width_ = width;
			height_ = height;

			if (browser)
			{
				browser->GetHost()->WasResized();
				log_message("html resize - %dx%d\n", width, height);
			}
		}
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
	bool needs_stats_update_;
	bool send_begin_frame_;
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

	void tick(double t) override
	{
		auto const comp = composition();
		if (comp)
		{
			// The bounding box for this layer is in normalized coordinates,
			// the html view needs to know pixel size...so we convert from normalized
			// to pixels based on the composition dimensions (which are in pixels).
			//
			// Note: it is safe to call resize() on the view repeatedly since
			// it will ignore the call if the requested size is the same

			auto const rect = bounds();
			auto const width = static_cast<int>(rect.width * comp->width());
			auto const height = static_cast<int>(rect.height * comp->height());

			if (view_)
			{
				view_->resize(width, height);
				view_->tick(comp, t);
			}
		}

		Layer::tick(t);
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
	settings.single_process = true;
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
shared_ptr<Layer> create_web_layer(
	std::shared_ptr<d3d11::Device> const& device,
	string const& url,
	int width, int height)
{
	CefWindowInfo window_info;
	window_info.SetAsWindowless(nullptr);

	// we want to use OnAcceleratedPaint
	window_info.shared_texture_enabled = true;

	// We can set the shared_texture_sync_key to 0 so Chromium will 
	// setup the shared texture with a keyed mutex.  
	// The Keyed Mutex is a work-in-progress and it is disabled by default
	// (currently this application is NOT using keyed mutexes)
	//window_info.shared_texture_sync_key = 0;

	// we are going to issue calls to SendExternalBeginFrame
	// and CEF will not use its internal BeginFrameTimer in this case
	window_info.external_begin_frame_enabled = true;

	CefBrowserSettings settings;

	// Set the maximum rate that the HTML content will render at
	//
	// NOTE: this value is NOT capped to 60 by CEF when using shared textures and
	// it is completely ignored when using SendExternalBeginFrame
	//
	// For testing, this application uses 120Hz to show that the 60Hz limit is ignored
	// (set window_info.external_begin_frame_enabled above to false to test)
	//
	settings.windowless_frame_rate = 120;

	CefRefPtr<HtmlView> view(new HtmlView(
			device, width, height, window_info.external_begin_frame_enabled));

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

	{ // check first if we need to run as a worker process

		CefRefPtr<HtmlApp> app(new HtmlApp());
		CefMainArgs main_args(instance);
		int exit_code = CefExecuteProcess(main_args, app, nullptr);
		if (exit_code >= 0) {
			return exit_code;
		}
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

//
// return the CEF + Chromium version
//
string cef_version()
{
	ostringstream ver;
	ver << "CEF: " << 
		CEF_VERSION << " (Chromium: "
		<< CHROME_VERSION_MAJOR << "."
		<< CHROME_VERSION_MINOR << "."
		<< CHROME_VERSION_BUILD << "."
		<< CHROME_VERSION_PATCH << ")";
	return ver.str();
}

