#include "d3d11.h"
#include "util.h"

#include <d3dcompiler.h>
#include <directxmath.h>

using namespace std;

namespace d3d11 {

	struct SimpleVertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 tex;
	};

	Context::Context(ID3D11DeviceContext* ctx)
		: ctx_(to_com_ptr(ctx))
	{
	}

	void Context::flush()
	{
		ctx_->Flush();
	}


	SwapChain::SwapChain(
				IDXGISwapChain* swapchain, 
				ID3D11RenderTargetView* rtv,
				ID3D11SamplerState* sampler,
				ID3D11BlendState* blender)
		: sampler_(to_com_ptr(sampler))
		, blender_(to_com_ptr(blender))
		, swapchain_(to_com_ptr(swapchain))
		, rtv_(to_com_ptr(rtv))
	{
	}

	void SwapChain::bind(shared_ptr<Context> const& ctx)
	{
		ctx_ = ctx;
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);

		ID3D11RenderTargetView* rtv[1] = { rtv_.get() };
		d3d11_ctx->OMSetRenderTargets(1, rtv, nullptr);

		// set default blending state
		if (blender_)
		{
			float factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			d3d11_ctx->OMSetBlendState(blender_.get(), factor, 0xffffffff);
		}

		// set default sampler state
		if (sampler_)
		{
			ID3D11SamplerState* samplers[1] = { sampler_.get() };
			d3d11_ctx->PSSetSamplers(0, 1, samplers);
		}
	}

	void SwapChain::unbind()
	{
		ctx_.reset();
	}

	void SwapChain::clear(float red, float green, float blue, float alpha)
	{
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);
		assert(d3d11_ctx);

		FLOAT color[4] = { red, green, blue, alpha };
		d3d11_ctx->ClearRenderTargetView(rtv_.get(), color);
	}

	void SwapChain::present(int sync_interval)
	{
		swapchain_->Present(sync_interval, 0);
	}

	void SwapChain::resize(int width, int height)
	{
		if (width <= 0 || height <= 0) {
			return;
		}

		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);
		assert(d3d11_ctx);

		d3d11_ctx->OMSetRenderTargets(0, 0, 0);
		rtv_.reset();

		DXGI_SWAP_CHAIN_DESC desc;
		swapchain_->GetDesc(&desc);
		auto hr = swapchain_->ResizeBuffers(0, width, height, desc.BufferDesc.Format, desc.Flags);
		if (FAILED(hr)) {
			log_message("failed to resize swapchain (%dx%d)\n", width, height);
			return;
		}

		ID3D11Texture2D* buffer = nullptr;
		hr = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buffer);
		if (FAILED(hr)) {
			log_message("failed to resize swapchain (%dx%d)\n", width, height);
			return;
		}
		
		ID3D11Device* dev = nullptr;
		d3d11_ctx->GetDevice(&dev);
		if (dev)
		{
			D3D11_RENDER_TARGET_VIEW_DESC vdesc = {};
			vdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			vdesc.Texture2D.MipSlice = 0;
			vdesc.Format = desc.BufferDesc.Format;
				
			ID3D11RenderTargetView* view = nullptr;
			hr = dev->CreateRenderTargetView(buffer, &vdesc, &view);
			if (SUCCEEDED(hr))
			{
				rtv_ = to_com_ptr(view);
				d3d11_ctx->OMSetRenderTargets(1, &view, nullptr);
			}
			dev->Release();
		}
		buffer->Release();

		D3D11_VIEWPORT vp;
		vp.Width = static_cast<float>(width);
		vp.Height = static_cast<float>(height);
		vp.MinDepth = D3D11_MIN_DEPTH;
		vp.MaxDepth = D3D11_MAX_DEPTH;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		d3d11_ctx->RSSetViewports(1, &vp);
	}


	Effect::Effect(
		ID3D11VertexShader* vsh,
		ID3D11PixelShader* psh,
		ID3D11InputLayout* layout)
		: vsh_(to_com_ptr(vsh))
		, psh_(to_com_ptr(psh))
		, layout_(to_com_ptr(layout))
	{
	}

	void Effect::bind(shared_ptr<Context> const& ctx)
	{
		ctx_ = ctx;
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);

		d3d11_ctx->IASetInputLayout(layout_.get());
		d3d11_ctx->VSSetShader(vsh_.get(), nullptr, 0);
		d3d11_ctx->PSSetShader(psh_.get(), nullptr, 0);
	}

	void Effect::unbind()
	{
	}
	
	Geometry::Geometry(
			D3D_PRIMITIVE_TOPOLOGY primitive,
			uint32_t vertices,
			uint32_t stride,
			ID3D11Buffer* buffer)
		: primitive_(primitive)
		, vertices_(vertices)
		, stride_(stride)
		, buffer_(to_com_ptr(buffer))
	{
	}

	void Geometry::bind(shared_ptr<Context> const& ctx)
	{
		ctx_ = ctx;
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);

		// todo: handle offset
		uint32_t offset = 0;

		ID3D11Buffer* buffers[1] = { buffer_.get() };
		d3d11_ctx->IASetVertexBuffers(0, 1, buffers, &stride_, &offset);
		d3d11_ctx->IASetPrimitiveTopology(primitive_);
	}

	void Geometry::unbind()
	{
	}

	void Geometry::draw()
	{
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);
		assert(d3d11_ctx);

		// todo: handle offset
		d3d11_ctx->Draw(vertices_, 0);
	}

	
	Texture2D::Texture2D(
		ID3D11Texture2D* tex,
		ID3D11ShaderResourceView* srv)
		: texture_(to_com_ptr(tex))
		, srv_(to_com_ptr(srv))
	{
		share_handle_ = nullptr;

		IDXGIResource* res = nullptr;
		if (SUCCEEDED(texture_->QueryInterface(
			__uuidof(IDXGIResource), reinterpret_cast<void**>(&res))))
		{
			res->GetSharedHandle(&share_handle_);
			res->Release();
		}

		// are we using a keyed mutex?
		IDXGIKeyedMutex* mutex = nullptr;
		if (SUCCEEDED(texture_->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex))) {
			keyed_mutex_ = to_com_ptr(mutex);
		}
	}

	uint32_t Texture2D::width() const
	{
		D3D11_TEXTURE2D_DESC desc;
		texture_->GetDesc(&desc);
		return desc.Width;
	}

	uint32_t Texture2D::height() const
	{
		D3D11_TEXTURE2D_DESC desc;
		texture_->GetDesc(&desc);
		return desc.Height;
	}

	DXGI_FORMAT Texture2D::format() const
	{
		D3D11_TEXTURE2D_DESC desc;
		texture_->GetDesc(&desc);
		return desc.Format;
	}

	bool Texture2D::has_mutex() const
	{
		return (keyed_mutex_.get() != nullptr);
	}

	bool Texture2D::lock_key(uint64_t key, uint32_t timeout_ms)
	{
		if (keyed_mutex_) {
			auto const hr = keyed_mutex_->AcquireSync(key, timeout_ms);
			return SUCCEEDED(hr);
		}
		return true;
	}

	void Texture2D::unlock_key(uint64_t key)
	{
		if (keyed_mutex_) {
			keyed_mutex_->ReleaseSync(key);
		}
	}

	void Texture2D::bind(shared_ptr<Context> const& ctx)
	{
		ctx_ = ctx;
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);
		if (srv_)
		{
			ID3D11ShaderResourceView* views[1] = { srv_.get() };
			d3d11_ctx->PSSetShaderResources(0, 1, views);
		}
	}

	void Texture2D::unbind()
	{
	}

	void* Texture2D::share_handle() const
	{
		return share_handle_;
	}

	void Texture2D::copy_from(shared_ptr<Texture2D> const& other)
	{
		ID3D11DeviceContext* d3d11_ctx = (ID3D11DeviceContext*)(*ctx_);
		assert(d3d11_ctx);
		if (other) {
			d3d11_ctx->CopyResource(texture_.get(), other->texture_.get());
		}
	}


	Device::Device(ID3D11Device* pdev, ID3D11DeviceContext* pctx)
		: device_(to_com_ptr(pdev))
		, ctx_(make_shared<Context>(pctx))
	{
		_lib_compiler = LoadLibrary(L"d3dcompiler_47.dll");
	}

	string Device::adapter_name() const
	{
		IDXGIDevice* dxgi_dev = nullptr;
		auto hr = device_->QueryInterface(__uuidof(dxgi_dev), (void**)&dxgi_dev);
		if (SUCCEEDED(hr))
		{
			IDXGIAdapter* dxgi_adapt = nullptr;
			hr = dxgi_dev->GetAdapter(&dxgi_adapt);
			dxgi_dev->Release();
			if (SUCCEEDED(hr))
			{
				DXGI_ADAPTER_DESC desc;
				hr = dxgi_adapt->GetDesc(&desc);
				dxgi_adapt->Release();
				if (SUCCEEDED(hr)) {
					return to_utf8(desc.Description);
				}
			}
		}

		return "n/a";
	}

	shared_ptr<Context> Device::immedidate_context()
	{
		return ctx_;
	}

	shared_ptr<SwapChain> Device::create_swapchain(HWND window, int width, int height)
	{
		HRESULT hr;
		IDXGIFactory1* dxgi_factory = nullptr;

		// default size to the window size unless specified
		RECT rc_bounds;
		GetClientRect(window, &rc_bounds);
		if (width <= 0) {
			width = rc_bounds.right - rc_bounds.left;
		}
		if (height <= 0) {
			height = rc_bounds.bottom - rc_bounds.top;
		}
		
		{
			IDXGIDevice* dxgi_dev = nullptr;
			hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev));
			if (FAILED(hr)) {
				return nullptr;
			}
			
			IDXGIAdapter* adapter = nullptr;
			hr = dxgi_dev->GetAdapter(&adapter);
			dxgi_dev->Release();
			if (FAILED(hr)) {
				return nullptr;
			}
			
			hr = adapter->GetParent(
				__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgi_factory));
			adapter->Release();
		}

		if (!dxgi_factory) {
			return nullptr;
		}

		IDXGISwapChain* swapchain = nullptr;

		// Create swap chain
		IDXGIFactory2* dxgi_factory2 = nullptr;
		hr = dxgi_factory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgi_factory2));
		if (dxgi_factory2)
		{
			DXGI_SWAP_CHAIN_DESC1 sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.Width = width;
			sd.Height = height;
			sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.BufferCount = 1;

			IDXGISwapChain1* swapchain1 = nullptr;
			hr = dxgi_factory2->CreateSwapChainForHwnd(
							device_.get(), window, &sd, nullptr, nullptr, &swapchain1);
			if (SUCCEEDED(hr))
			{
				hr = swapchain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&swapchain));
				swapchain1->Release();
			}

			dxgi_factory2->Release();
		}
		else
		{
			// DirectX 11.0 systems
			DXGI_SWAP_CHAIN_DESC sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.BufferCount = 1;
			sd.BufferDesc.Width = width;
			sd.BufferDesc.Height = height;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferDesc.RefreshRate.Numerator = 60;
			sd.BufferDesc.RefreshRate.Denominator = 1;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = window;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.Windowed = TRUE;

			hr = dxgi_factory->CreateSwapChain(device_.get(), &sd, &swapchain);
		}

		// we don't handle full-screen swapchains so we block the ALT+ENTER shortcut
		dxgi_factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
		dxgi_factory->Release();

		if (!swapchain) {
			return nullptr;
		}
		
		ID3D11Texture2D* back_buffer = nullptr;
		hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
		if (FAILED(hr)) 
		{
			swapchain->Release();
			return nullptr;
		}

		ID3D11RenderTargetView* rtv = nullptr;
		hr = device_->CreateRenderTargetView(back_buffer, nullptr, &rtv);
		back_buffer->Release();
		if (FAILED(hr)) {
			swapchain->Release();
			return nullptr;
		}

		auto const ctx = (ID3D11DeviceContext*)(*ctx_);

		ctx->OMSetRenderTargets(1, &rtv, nullptr);

		// Setup the viewport
		D3D11_VIEWPORT vp;
		vp.Width = (FLOAT)width;
		vp.Height = (FLOAT)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		ctx->RSSetViewports(1, &vp);

		// create a default sampler to use
		ID3D11SamplerState* sampler = nullptr;
		{
			D3D11_SAMPLER_DESC desc = {};
			desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MinLOD = 0.0f;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			device_->CreateSamplerState(&desc, &sampler);
		}

		// create a default blend state to use (pre-multiplied alpha)
		ID3D11BlendState* blender = nullptr;
		{
			D3D11_BLEND_DESC desc;
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			auto const count = sizeof(desc.RenderTarget) / sizeof(desc.RenderTarget[0]);
			for (size_t n = 0; n < count; ++n)
			{
				desc.RenderTarget[n].BlendEnable = TRUE;
				desc.RenderTarget[n].SrcBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[n].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
				desc.RenderTarget[n].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[n].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
				desc.RenderTarget[n].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[n].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[n].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			}
			device_->CreateBlendState(&desc, &blender);
		}

		return make_shared<SwapChain>(swapchain, rtv, sampler, blender);
	}
	
	shared_ptr<Geometry> Device::create_quad(
			float x, float y, float width, float height, bool flip)
	{
		x = (x * 2.0f) - 1.0f;
		y = 1.0f - (y * 2.0f);
		width = width * 2.0f;
		height = height * 2.0f;
		float z = 1.0f;

		SimpleVertex vertices[] = {

			{ DirectX::XMFLOAT3(x, y, z), DirectX::XMFLOAT2(0.0f, 0.0f) },
			{ DirectX::XMFLOAT3(x + width, y, z), DirectX::XMFLOAT2(1.0f, 0.0f) },
			{ DirectX::XMFLOAT3(x, y - height, z), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(x + width, y - height, z), DirectX::XMFLOAT2(1.0f, 1.0f) }
		};

		if (flip) 
		{
			DirectX::XMFLOAT2 tmp(vertices[2].tex);
			vertices[2].tex = vertices[0].tex;
			vertices[0].tex = tmp;

			tmp = vertices[3].tex;
			vertices[3].tex = vertices[1].tex;
			vertices[1].tex = tmp;
		}

		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ByteWidth = sizeof(SimpleVertex) * 4;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA srd = {};
		srd.pSysMem = vertices;

		ID3D11Buffer* buffer = nullptr;
		auto const hr = device_->CreateBuffer(&desc, &srd, &buffer);
		if (SUCCEEDED(hr)) {
			return make_shared<Geometry>(
				D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4, static_cast<uint32_t>(sizeof(SimpleVertex)), buffer);
		}

		return nullptr;
	}

	shared_ptr<Texture2D> Device::open_shared_texture(void* handle)
	{
		ID3D11Texture2D* tex = nullptr;
		auto hr = device_->OpenSharedResource(
				handle, __uuidof(ID3D11Texture2D), (void**)(&tex));
		if (FAILED(hr)) {
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC td;
		tex->GetDesc(&td);

		ID3D11ShaderResourceView* srv = nullptr;

		if (td.BindFlags & D3D11_BIND_SHADER_RESOURCE)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
			srv_desc.Format = td.Format;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = 1;

			hr = device_->CreateShaderResourceView(tex, &srv_desc, &srv);
			if (FAILED(hr))
			{
				tex->Release();
				return nullptr;
			}
		}
		
		return make_shared<Texture2D>(tex, srv);
	}

	shared_ptr<Texture2D> Device::create_texture(
			int width,
			int height,
			DXGI_FORMAT format,
			const void* data,
			size_t row_stride)
	{
		D3D11_TEXTURE2D_DESC td;
		td.ArraySize = 1;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.CPUAccessFlags = 0;
		td.Format = format;
		td.Width = width;
		td.Height = height;
		td.MipLevels = 1;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.Usage = D3D11_USAGE_DEFAULT;

		D3D11_SUBRESOURCE_DATA srd;
		srd.pSysMem = data;
		srd.SysMemPitch = static_cast<uint32_t>(row_stride);
		srd.SysMemSlicePitch = 0;

		ID3D11Texture2D* tex = nullptr;
		auto hr = device_->CreateTexture2D(&td, data ? &srd : nullptr, &tex);
		if (FAILED(hr)) {
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
		srv_desc.Format = td.Format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* srv = nullptr;
		hr = device_->CreateShaderResourceView(tex, &srv_desc, &srv);
		if (FAILED(hr))
		{
			tex->Release();
			return nullptr;
		}

		return make_shared<Texture2D>(tex, srv);
	}


	shared_ptr<ID3DBlob> Device::compile_shader(
							string const& source_code,
							string const& entry_point,
							string const& model)
	{
		if (!_lib_compiler) {
			return nullptr;
		}

		typedef HRESULT(WINAPI* PFN_D3DCOMPILE)(
			LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
			ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

		auto const fnc_compile = reinterpret_cast<PFN_D3DCOMPILE>(
			GetProcAddress(_lib_compiler, "D3DCompile"));
		if (!fnc_compile) {
			return nullptr;
		}

		DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;

#if defined(NDEBUG)
		//flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
		//flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
#else
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		ID3DBlob* blob = nullptr;
		ID3DBlob* blob_err = nullptr;

		auto const psrc = source_code.c_str();
		auto const len = source_code.size() + 1;
		
		auto const hr = fnc_compile(
			psrc, len, nullptr, nullptr, nullptr,
			entry_point.c_str(),
			model.c_str(),
			flags, 
			0, 
			&blob, 
			&blob_err);

		if (FAILED(hr))
		{
			if (blob_err)
			{
				// TODO: log the error
				blob_err->Release();
			}
			return nullptr;
		}

		if (blob_err) {
			blob_err->Release();
		}

		return shared_ptr<ID3DBlob>(blob, [](ID3DBlob* p) { if (p) p->Release(); });
	}


	//
	// create some basic shaders so we can draw a textured-quad
	//
	shared_ptr<Effect> Device::create_default_effect()
	{
		auto const vsh =
R"--(struct VS_INPUT
{
	float4 pos : POSITION;
	float2 tex : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	output.pos = input.pos;
	output.tex = input.tex;
	return output;
})--";

		auto const psh =
R"--(Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target
{
	return tex0.Sample(samp0, input.tex);
})--";

		return create_effect(
				vsh, 
				"main", 
				"vs_4_0",
				psh, 
				"main",
				"ps_4_0");
	}

	shared_ptr<Effect> Device::create_effect(
		string const& vertex_code,
		string const& vertex_entry,
		string const& vertex_model,
		string const& pixel_code,
		string const& pixel_entry,
		string const& pixel_model)
	{
		auto const vs_blob = compile_shader(vertex_code, vertex_entry, vertex_model);

		ID3D11VertexShader* vshdr = nullptr;
		ID3D11InputLayout* layout = nullptr;

		if (vs_blob)
		{
			device_->CreateVertexShader(
					vs_blob->GetBufferPointer(), 
					vs_blob->GetBufferSize(), 
					nullptr, 
					&vshdr);

			D3D11_INPUT_ELEMENT_DESC layout_desc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			UINT elements = ARRAYSIZE(layout_desc);

			// Create the input layout
			device_->CreateInputLayout(
				layout_desc,
				elements,
				vs_blob->GetBufferPointer(),
				vs_blob->GetBufferSize(),
				&layout);
		}

		auto const ps_blob = compile_shader(pixel_code, pixel_entry, pixel_model);
		ID3D11PixelShader* pshdr = nullptr;
		if (ps_blob) 
		{
			device_->CreatePixelShader(
					ps_blob->GetBufferPointer(), 
					ps_blob->GetBufferSize(), 
					nullptr, 
					&pshdr);
		}

		return make_shared<Effect>(vshdr, pshdr, layout);
	}


	shared_ptr<Device> create_device()
	{
		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		
		D3D_FEATURE_LEVEL feature_levels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			//D3D_FEATURE_LEVEL_9_3,
		};
		UINT num_feature_levels = sizeof(feature_levels) / sizeof(feature_levels[0]);

		
		ID3D11Device* pdev = nullptr;
		ID3D11DeviceContext* pctx = nullptr;

		D3D_FEATURE_LEVEL selected_level;
		HRESULT hr = D3D11CreateDevice(
				nullptr, 
				D3D_DRIVER_TYPE_HARDWARE, 
				nullptr, 
				flags, feature_levels, 
				num_feature_levels,
				D3D11_SDK_VERSION, 
				&pdev,
				&selected_level,
				&pctx);

		if (hr == E_INVALIDARG)
		{
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 
			// so we need to retry without it
			hr = D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_HARDWARE,
				nullptr,
				flags,
				&feature_levels[1],
				num_feature_levels - 1,
				D3D11_SDK_VERSION,
				&pdev,
				&selected_level,
				&pctx);
		}
		
		if (SUCCEEDED(hr)) 
		{
			auto const dev = make_shared<Device>(pdev, pctx);

			log_message("d3d11: selected adapter: %s\n", dev->adapter_name().c_str());

			log_message("d3d11: selected feature level: 0x%04X\n", selected_level);

			return dev;
		}

		return nullptr;
	}
}