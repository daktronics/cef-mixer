#include "composition.h"

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