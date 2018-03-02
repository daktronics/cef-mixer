#include "d3d11.h"

#include <d3dcompiler.h>
#include <directxmath.h>
#include <d3d11_4.h>

using namespace std;

namespace d3d11 {

	template<class T>
	shared_ptr<T> to_com_ptr(T* obj)
	{
		return shared_ptr<T>(obj, [](T* p) { if (p) p->Release(); });
	}

	struct SimpleVertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 tex;
	};

	Context::Context(ID3D11DeviceContext* ctx)
		: ctx_(to_com_ptr<ID3D11DeviceContext>(ctx))
	{
	}

	void Context::flush()
	{
		ctx_->Flush();
	}


	SwapChain::SwapChain(std::shared_ptr<ID3D11DeviceContext> const& ctx, 
				IDXGISwapChain* swapchain, 
				ID3D11RenderTargetView* rtv,
				ID3D11SamplerState* sampler,
				ID3D11BlendState* blender)
		: _sampler(to_com_ptr<ID3D11SamplerState>(sampler))
		, _blender(to_com_ptr<ID3D11BlendState>(blender))
		, _swapchain(shared_ptr<IDXGISwapChain>(swapchain, [](IDXGISwapChain* p) { if (p) p->Release(); }))
		, _rtv(shared_ptr<ID3D11RenderTargetView>(rtv, [](ID3D11RenderTargetView* p) { if (p) p->Release(); }))
		, _ctx(ctx)
	{
	}

	void SwapChain::clear(float red, float green, float blue, float alpha)
	{
		ID3D11RenderTargetView* rtv[1] = { _rtv.get() };
		_ctx->OMSetRenderTargets(1, rtv, nullptr);

		// set default blending state
		if (_blender) 
		{
			float factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			_ctx->OMSetBlendState(_blender.get(), factor, 0xffffffff);
		}

		// set default sampler state
		if (_sampler)
		{
			ID3D11SamplerState* samplers[1] = { _sampler.get() };
			_ctx->PSSetSamplers(0, 1, samplers);
		}

		FLOAT color[4] = { red, green, blue, alpha };
		_ctx->ClearRenderTargetView(_rtv.get(), color);
	}

	void SwapChain::present(int sync_interval)
	{
		_swapchain->Present(sync_interval, 0);
	}

	void SwapChain::resize(int width, int height)
	{
	}


	Effect::Effect(std::shared_ptr<ID3D11DeviceContext> const& ctx,
		ID3D11VertexShader* vsh,
		ID3D11PixelShader* psh,
		ID3D11InputLayout* layout)
		: _vsh(shared_ptr<ID3D11VertexShader>(vsh, [](ID3D11VertexShader* p) { if (p) p->Release(); }))
		, _psh(shared_ptr<ID3D11PixelShader>(psh, [](ID3D11PixelShader* p) { if (p) p->Release(); }))
		, _layout(shared_ptr<ID3D11InputLayout>(layout, [](ID3D11InputLayout* p) { if (p) p->Release(); }))
		, _ctx(ctx)
	{
	}

	void Effect::bind()
	{
		_ctx->IASetInputLayout(_layout.get());
		_ctx->VSSetShader(_vsh.get(), nullptr, 0);
		_ctx->PSSetShader(_psh.get(), nullptr, 0);
	}

	void Effect::unbind()
	{
	}
	
	Geometry::Geometry(
			std::shared_ptr<ID3D11DeviceContext> const& ctx,
			D3D_PRIMITIVE_TOPOLOGY primitive,
			uint32_t vertices,
			uint32_t stride,
			ID3D11Buffer* buffer)
		: _primitive(primitive)
		, _vertices(vertices)
		, _stride(stride)
		, _buffer(shared_ptr<ID3D11Buffer>(buffer, [](ID3D11Buffer* p) { if (p) p->Release(); }))
		, _ctx(ctx)
	{
	}

	void Geometry::bind()
	{
		// todo: handle offset
		uint32_t offset = 0;

		ID3D11Buffer* buffers[1] = { _buffer.get() };
		_ctx->IASetVertexBuffers(0, 1, buffers, &_stride, &offset);
		_ctx->IASetPrimitiveTopology(_primitive);
	}

	void Geometry::unbind()
	{
	}

	void Geometry::draw()
	{
		// todo: handle offset
		_ctx->Draw(_vertices, 0);
	}

	
	Texture2D::Texture2D(
		std::shared_ptr<ID3D11DeviceContext> const& ctx,
		ID3D11Texture2D* tex,
		ID3D11ShaderResourceView* srv)
		: _texture(shared_ptr<ID3D11Texture2D>(tex, [](ID3D11Texture2D* p) { if (p) p->Release(); }))
		, _srv(shared_ptr<ID3D11ShaderResourceView>(srv, [](ID3D11ShaderResourceView* p) { if (p) p->Release(); }))
		, _ctx(ctx)
	{
		share_handle_ = nullptr;

		IDXGIResource* res = nullptr;
		if (SUCCEEDED(_texture->QueryInterface(
			__uuidof(IDXGIResource), reinterpret_cast<void**>(&res))))
		{
			res->GetSharedHandle(&share_handle_);
			res->Release();
		}

	}

	uint32_t Texture2D::width() const
	{
		D3D11_TEXTURE2D_DESC desc;
		_texture->GetDesc(&desc);
		return desc.Width;
	}

	uint32_t Texture2D::height() const
	{
		D3D11_TEXTURE2D_DESC desc;
		_texture->GetDesc(&desc);
		return desc.Height;
	}

	DXGI_FORMAT Texture2D::format() const
	{
		D3D11_TEXTURE2D_DESC desc;
		_texture->GetDesc(&desc);
		return desc.Format;
	}

	bool Texture2D::lock_key(uint64_t key)
	{
		IDXGIKeyedMutex* mutex = nullptr;
		if (SUCCEEDED(_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)))
		{
			auto const hr = mutex->AcquireSync(key, INFINITE);
			mutex->Release();
			return SUCCEEDED(hr);
		}
		return true;
	}

	void Texture2D::unlock_key(uint64_t key)
	{
		IDXGIKeyedMutex* mutex = nullptr;
		if (SUCCEEDED(_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)))
		{
			mutex->ReleaseSync(key);
			mutex->Release();		
		}
	}

	void Texture2D::bind()
	{
		if (_srv)
		{
			ID3D11ShaderResourceView* views[1] = { _srv.get() };
			_ctx->PSSetShaderResources(0, 1, views);		
		}
	}

	void Texture2D::unbind()
	{
	}

	void* Texture2D::share_handle() const
	{
		return share_handle_;
	}

	void Texture2D::copy_from(std::shared_ptr<Texture2D> const& other)
	{
		if (other) {
			_ctx->CopyResource(_texture.get(), other->_texture.get());
		}
	}


	Device::Device(ID3D11Device* pdev, ID3D11DeviceContext* pctx)
		: _device(shared_ptr<ID3D11Device>(pdev, [](ID3D11Device* p) { if (p) p->Release(); }))
		, ctx_(make_shared<Context>(pctx))
		, _ctx(shared_ptr<ID3D11DeviceContext>(pctx, [](ID3D11DeviceContext* p) { if (p) p->Release(); }))
	{
		_lib_compiler = LoadLibrary(L"d3dcompiler_47.dll");
	}

	std::shared_ptr<Context> Device::immedidate_context()
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
			hr = _device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev));
			if (FAILED(hr)) {
				return nullptr;
			}
			
			IDXGIAdapter* adapter = nullptr;
			hr = dxgi_dev->GetAdapter(&adapter);
			dxgi_dev->Release();
			if (FAILED(hr)) {
				return nullptr;
			}
			
			hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgi_factory));
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
							_device.get(), window, &sd, nullptr, nullptr, &swapchain1);
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

			hr = dxgi_factory->CreateSwapChain(_device.get(), &sd, &swapchain);
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
		hr = _device->CreateRenderTargetView(back_buffer, nullptr, &rtv);
		back_buffer->Release();
		if (FAILED(hr)) {
			swapchain->Release();
			return nullptr;
		}

		_ctx->OMSetRenderTargets(1, &rtv, nullptr);

		// Setup the viewport
		D3D11_VIEWPORT vp;
		vp.Width = (FLOAT)width;
		vp.Height = (FLOAT)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		_ctx->RSSetViewports(1, &vp);

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
			_device->CreateSamplerState(&desc, &sampler);
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
			_device->CreateBlendState(&desc, &blender);
		}

		return make_shared<SwapChain>(_ctx, swapchain, rtv, sampler, blender);
	}
	
	std::shared_ptr<Geometry> Device::create_quad(
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
		auto const hr = _device->CreateBuffer(&desc, &srd, &buffer);
		if (SUCCEEDED(hr)) {
			return make_shared<Geometry>(_ctx, 
					D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4, static_cast<uint32_t>(sizeof(SimpleVertex)), buffer);
		}

		return nullptr;
	}

	std::shared_ptr<Texture2D> Device::open_shared_texture(void* handle)
	{
		ID3D11Texture2D* tex = nullptr;
		auto hr = _device->OpenSharedResource(
				handle, __uuidof(ID3D11Texture2D), (void**)(&tex));
		if (FAILED(hr)) {
			return nullptr;
		}

		return make_shared<Texture2D>(_ctx, tex, nullptr);
	}

	std::shared_ptr<Texture2D> Device::create_texture(
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
		auto hr = _device->CreateTexture2D(&td, data ? &srd : nullptr, &tex);
		if (FAILED(hr)) {
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
		srv_desc.Format = td.Format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* srv = nullptr;
		hr = _device->CreateShaderResourceView(tex, &srv_desc, &srv);
		if (FAILED(hr))
		{
			tex->Release();
			return nullptr;
		}

		return make_shared<Texture2D>(_ctx, tex, srv);
	}


	shared_ptr<ID3DBlob> Device::compile_shader(
							std::string const& source_code,
							std::string const& entry_point,
							std::string const& model)
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
	std::shared_ptr<Effect> Device::create_default_effect()
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

	std::shared_ptr<Effect> Device::create_effect(
		std::string const& vertex_code,
		std::string const& vertex_entry,
		std::string const& vertex_model,
		std::string const& pixel_code,
		std::string const& pixel_entry,
		std::string const& pixel_model)
	{
		auto const vs_blob = compile_shader(vertex_code, vertex_entry, vertex_model);

		ID3D11VertexShader* vshdr = nullptr;
		ID3D11InputLayout* layout = nullptr;

		if (vs_blob)
		{
			_device->CreateVertexShader(
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
			_device->CreateInputLayout(
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
			_device->CreatePixelShader(
					ps_blob->GetBufferPointer(), 
					ps_blob->GetBufferSize(), 
					nullptr, 
					&pshdr);
		}

		return make_shared<Effect>(_ctx, vshdr, pshdr, layout);
	}


	std::shared_ptr<Device> create_device()
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
			D3D11CreateDevice(
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
			ID3D11Multithread* multithread = nullptr;
			if (SUCCEEDED(pdev->QueryInterface(__uuidof(ID3D11Multithread),
				reinterpret_cast<void**>(&multithread))))
			{
				//multithread->SetMultithreadProtected(TRUE);
				multithread->Release();			
			}

			return make_shared<Device>(pdev, pctx);
		}

		return nullptr;
	}
}