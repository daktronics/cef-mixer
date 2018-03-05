#include "composition.h"

using namespace std;

Layer::Layer(std::shared_ptr<d3d11::Device> const& device, bool flip)
	: device_(device)
	, flip_(flip)
{
}

Layer::~Layer() 
{
}

void Layer::move(float x, float y, float width, float height)
{	
	// obviously, it is not efficient to create the quad everytime we
	// move ... but for now we're just trying to get something on-screen
	geometry_ = device_->create_quad(x, y, width, height, flip_);
}

//
// helper method for derived classes to draw a textured-quad.
//
void Layer::render_texture(
		shared_ptr<d3d11::Context> const& ctx, 
		shared_ptr<d3d11::Texture2D> const& texture)
{
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


Composition::Composition(std::shared_ptr<d3d11::Device> const& device)
	: device_(device)
{
}

void Composition::add_layer(std::shared_ptr<Layer> const& layer)
{
	if (layer) {
		layers_.push_back(layer);
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