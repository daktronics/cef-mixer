#include "util.h"
#include "composition.h"

#include <wincodec.h>

using namespace std;

class ImageLayer : public Layer
{
public:
	ImageLayer(
			std::shared_ptr<d3d11::Device> const& device,
			std::shared_ptr<d3d11::Texture2D> const& texture)
		: Layer(device, false)
		, texture_(texture)
	{
	}

	void render(shared_ptr<d3d11::Context> const& ctx) override
	{
		// simply use the base class method to draw our texture
		render_texture(ctx, texture_);
	}

private:

	shared_ptr<d3d11::Texture2D> const texture_;
};

//
// use WIC to load a texture from an image file
//
shared_ptr<Layer> create_image_layer(
	std::shared_ptr<d3d11::Device> const& device,
	string const& filename)
{
	if (!device) {
		return nullptr;
	}

	auto const wfilename = to_utf16(filename);

	IWICImagingFactory* pwic = nullptr;

	// bummer, we need CoCreateInstance rather than a direct creator for WIC
	auto hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pwic));
	if (FAILED(hr)) {
		return nullptr;
	}

	auto const wic = to_com_ptr(pwic);

	IWICBitmapDecoder* pdec = nullptr;
	hr = wic->CreateDecoderFromFilename(
			wfilename.c_str(),
			nullptr,
			GENERIC_READ,
			WICDecodeMetadataCacheOnDemand,
			&pdec);
	if (FAILED(hr)) {
		return nullptr;
	}

	auto const decoder = to_com_ptr(pdec);

	IWICBitmapFrameDecode* pfrm = nullptr;
	hr = decoder->GetFrame(0, &pfrm);
	if (FAILED(hr)) {
		return nullptr;
	}

	auto const frame = to_com_ptr(pfrm);

	IWICFormatConverter* pcnv = nullptr;
	hr = wic->CreateFormatConverter(&pcnv);
	if (FAILED(hr)) {
		return nullptr;
	}
	auto const converter(pcnv);

	hr = converter->Initialize(
			frame.get(), GUID_WICPixelFormat32bppPRGBA, 
			WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) {
		return nullptr;
	}

	UINT width, height;
	frame->GetSize(&width, &height);

	uint32_t stride = width * 4;
	uint32_t cb = height * stride;

	shared_ptr<BYTE> buffer(
		reinterpret_cast<BYTE*>(malloc(cb)), free);

	converter->CopyPixels(nullptr, stride, cb, buffer.get());

	auto const texture = device->create_texture(
			width, 
			height, 
			DXGI_FORMAT_R8G8B8A8_UNORM, 
			buffer.get(), 
			stride);

	if (texture) {
		return make_shared<ImageLayer>(device, texture);
	}

	return nullptr;
}