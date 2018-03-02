#pragma once

#include <d3d11_1.h>
#include <memory>
#include <string>

namespace d3d11 {

	class SwapChain;
	class Geometry;
	class Effect;
	class Texture2D;

	template<class T>
	class ScopedBinder
	{
	public:
		ScopedBinder(std::shared_ptr<T> const& target)
			: _target(target)
		{
			if (_target) { _target->bind(); }
		}
		~ScopedBinder() { if (_target) { _target->unbind(); } }
	private:
		std::shared_ptr<T> const _target;
	};

	class Context
	{
	public:
		Context(ID3D11DeviceContext*);

		void flush();

	private:
		
		std::shared_ptr<ID3D11DeviceContext> const ctx_;
	};

	//
	// encapsulate a D3D11 Device object
	//
	class Device
	{
	public:
		Device(ID3D11Device*, ID3D11DeviceContext*);

		std::shared_ptr<Context> immedidate_context();

		std::shared_ptr<SwapChain> create_swapchain(HWND, int width=0, int height=0);
		
		std::shared_ptr<Geometry> create_quad(
					float x, float y, float width, float height, bool flip=false);

		std::shared_ptr<Texture2D> create_texture(
					int width, 
					int height, 
					DXGI_FORMAT format, 
					const void* data,
					size_t row_stride);

		std::shared_ptr<Texture2D> open_shared_texture(void*);

		std::shared_ptr<Effect> create_default_effect();

		std::shared_ptr<Effect> create_effect(
						std::string const& vertex_code,
						std::string const& vertex_entry,
						std::string const& vertex_model,
						std::string const& pixel_code,
						std::string const& pixel_entry,
						std::string const& pixel_model);

	private:

		std::shared_ptr<ID3DBlob> compile_shader(
					std::string const& source_code, 
					std::string const& entry_point, 
					std::string const& model);

		HMODULE _lib_compiler;

		std::shared_ptr<ID3D11Device> const _device;
		std::shared_ptr<Context> const ctx_;
		std::shared_ptr<ID3D11DeviceContext> const _ctx;
	};

	//
	// encapsulate a DXGI swapchain for a window
	//
	class SwapChain
	{
	public:
		SwapChain(std::shared_ptr<ID3D11DeviceContext> const& ctx,
				IDXGISwapChain*, 
				ID3D11RenderTargetView*,
				ID3D11SamplerState*,
				ID3D11BlendState*);

		void clear(float red, float green, float blue, float alpha);

		void present(int sync_interval);
		void resize(int width, int height);

	private:
		
		std::shared_ptr<ID3D11SamplerState> const _sampler;
		std::shared_ptr<ID3D11BlendState> const _blender;
		std::shared_ptr<IDXGISwapChain> const _swapchain;
		std::shared_ptr<ID3D11RenderTargetView> const _rtv;
		std::shared_ptr<ID3D11DeviceContext> const _ctx;
	};

	class Texture2D
	{
	public:
		Texture2D(std::shared_ptr<ID3D11DeviceContext> const& ctx,
			ID3D11Texture2D* tex,
			ID3D11ShaderResourceView* srv);

		void bind();
		void unbind();

		uint32_t width() const;
		uint32_t height() const;
		DXGI_FORMAT format() const;

		bool lock_key(uint64_t key);
		void unlock_key(uint64_t key);

		void* share_handle() const;

		void copy_from(std::shared_ptr<Texture2D> const&);

	private:

		HANDLE share_handle_;
		
		std::shared_ptr<ID3D11Texture2D> const _texture;
		std::shared_ptr<ID3D11ShaderResourceView> const _srv;
		std::shared_ptr<ID3D11DeviceContext> const _ctx;
	};

	class Effect
	{
	public:
		Effect(std::shared_ptr<ID3D11DeviceContext> const& ctx,
				ID3D11VertexShader* vsh,
				ID3D11PixelShader* psh,
				ID3D11InputLayout* layout);

		void bind();
		void unbind();

	private:

		std::shared_ptr<ID3D11VertexShader> const _vsh;
		std::shared_ptr<ID3D11PixelShader> const _psh;
		std::shared_ptr<ID3D11InputLayout> const _layout;
		std::shared_ptr<ID3D11DeviceContext> const _ctx;
	};


	class Geometry
	{
	public:
		Geometry(std::shared_ptr<ID3D11DeviceContext> const& ctx,
			D3D_PRIMITIVE_TOPOLOGY primitive,
			uint32_t vertices,
			uint32_t stride,
			ID3D11Buffer*);

		void bind();
		void unbind();
		void draw();

	private:

		D3D_PRIMITIVE_TOPOLOGY _primitive;
		uint32_t _vertices;
		uint32_t _stride;
		std::shared_ptr<ID3D11Buffer> const _buffer;
		std::shared_ptr<ID3D11DeviceContext> const _ctx;
	};


	std::shared_ptr<Device> create_device();
}