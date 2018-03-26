#include "composition.h"
#include "util.h"

#include <include/cef_parser.h>

using namespace std;

Layer::Layer(shared_ptr<d3d11::Device> const& device, bool flip)
	: device_(device)
	, flip_(flip)
{
	bounds_.x = bounds_.y = bounds_.width = bounds_.height = 0.0f;
}

Layer::~Layer() 
{
}

void Layer::attach(std::shared_ptr<Composition> const& parent)
{
	composition_ = parent;
}

shared_ptr<Composition> Layer::composition() const
{
	return composition_.lock();
}

Rect Layer::bounds() const
{
	return bounds_;
}

void Layer::move(float x, float y, float width, float height)
{	
	bounds_.x = x;
	bounds_.y = y;
	bounds_.width = width;
	bounds_.height = height;

	// obviously, it is not efficient to create the quad everytime we
	// move ... but for now we're just trying to get something on-screen
	geometry_.reset();
}

void Layer::tick(double)
{
	// nothing to update in the base class
}

//
// helper method for derived classes to draw a textured-quad.
//
void Layer::render_texture(
		shared_ptr<d3d11::Context> const& ctx, 
		shared_ptr<d3d11::Texture2D> const& texture)
{
	if (!geometry_) {
		geometry_ = device_->create_quad(bounds_.x, 
			bounds_.y, bounds_.width, bounds_.height, flip_);
	}
	
	if (geometry_ && texture)
	{
		// we need a shader
		if (!effect_) {
			effect_ = device_->create_default_effect();
		}

		// bind our states/resource to the pipeline
		d3d11::ScopedBinder<d3d11::Geometry> quad_binder(ctx, geometry_);
		d3d11::ScopedBinder<d3d11::Effect> fx_binder(ctx, effect_);
		d3d11::ScopedBinder<d3d11::Texture2D> tex_binder(ctx, texture);

		// actually draw the quad
		geometry_->draw();
	}
}


Composition::Composition(shared_ptr<d3d11::Device> const& device,
	int width, int height)
	: width_(width)
	, height_(height)
	, device_(device)
{
}

void Composition::add_layer(shared_ptr<Layer> const& layer)
{
	if (layer) {
		layers_.push_back(layer);

		// attach ourself as the parent
		layer->attach(shared_from_this());
	}
}

void Composition::resize(int width, int height)
{
	width_ = width;
	height_ = height;
}

void Composition::tick(double t)
{
	for (auto const& layer : layers_) {
		layer->tick(t);
	}
}

void Composition::render(shared_ptr<d3d11::Context> const& ctx)
{
	// pretty simple ... just use painter's algorithm and render 
	// our layers in order (not doing any depth or 3D here)
	for (auto const& layer : layers_) {
		layer->render(ctx);
	}
}

int to_int(CefRefPtr<CefDictionaryValue> const& dict, string const& key, int default_value)
{
	if (dict)
	{
		auto const type = dict->GetType(key);
		if (type == VTYPE_INT) {
			return dict->GetInt(key);
		}
		if (type == VTYPE_DOUBLE) {
			return static_cast<int>(dict->GetDouble(key));
		}
	}
	return default_value;
}

float to_float(CefRefPtr<CefDictionaryValue> const& dict, string const& key, float default_value)
{
	if (dict)
	{
		auto const type = dict->GetType(key);
		if (type == VTYPE_INT) {
			return static_cast<float>(dict->GetInt(key));
		}
		if (type == VTYPE_DOUBLE) {
			return static_cast<float>(dict->GetDouble(key));
		}
	}
	return default_value;
}

//
// create a composition layer from the given dictionary
//
shared_ptr<Layer> to_layer(
		shared_ptr<d3d11::Device> const& device, 
		int width, 
		int height,
		CefRefPtr<CefDictionaryValue> const& dict)
{
	if (!dict || dict->GetType("type") != VTYPE_STRING) {
		return nullptr;
	}

	auto const type = dict->GetString("type");

	// get the url or filename for the layer
	if (dict->GetType("src") != VTYPE_STRING) {
		return nullptr;
	}
	auto const src = dict->GetString("src");

	if (type == "image")
	{
		auto const realpath = locate_media(src);
		if (realpath) {
			return create_image_layer(device, *realpath);
		}
	}
	else if (type == "web") {
		return create_web_layer(device, src, width, height);
	}

	return nullptr;
}

shared_ptr<Composition> create_composition(
	shared_ptr<d3d11::Device> const& device,
	string const& json)
{
	auto const val = CefParseJSON(
		CefString(json), JSON_PARSER_ALLOW_TRAILING_COMMAS);
	if (!val.get() || !val->IsValid() || (val->GetType() != VTYPE_DICTIONARY)) {
		return nullptr;
	}

	auto const dict = val->GetDictionary();

	auto const width = to_int(dict, "width", 1280);
	auto const height = to_int(dict, "height", 720);

	auto const composition = make_shared<Composition>(device, width, height);

	// create and add layers as defined in the layers array
	if (dict->GetType("layers") == VTYPE_LIST)
	{
		auto const layers = dict->GetList("layers");
		if (layers && layers->IsValid())
		{
			for (size_t n = 0; n < layers->GetSize(); ++n)
			{
				if (layers->GetType(n) == VTYPE_DICTIONARY)
				{
					auto const obj = layers->GetDictionary(n);
					if (obj && obj->IsValid()) 
					{
						// create a valid layer from the JSON-object
						auto const layer = to_layer(device, 
									composition->width(), 
									composition->height(), 
									obj);
						if (!layer) {
							continue;
						}
						
						// add the layer to the composition
						composition->add_layer(layer);

						// move to default position
						auto const x = to_float(obj, "left", 0.0f);
						auto const y = to_float(obj, "top", 0.0f);
						auto const w = to_float(obj, "width", 1.0f);
						auto const h = to_float(obj, "height", 1.0f);

						layer->move(x, y, w, h);
					}
				}
			}		
		}	
	}
	
	return composition;
}