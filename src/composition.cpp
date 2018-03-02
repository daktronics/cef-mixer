#include "composition.h"

using namespace std;

Layer::Layer(std::shared_ptr<d3d11::Device> const& device, bool flip)
	: _device(device)
	, _flip(flip)
{
}

Layer::~Layer() 
{
}

void Layer::move(Anchor anchor, float x, float y, float width, float height)
{
	switch (anchor)
	{
		case Anchor::Center:
			x = x - (width / 2.0f);
			y = y - (height / 2.0f);
			break;
			
		default : break;
	}	
	
	// obviously, it is not efficient to create the quad everytime we
	// move ... but for now we're just trying to get something on-screen
	_geometry = _device->create_quad(x, y, width, height, _flip);
}

//
// helper method for derived classes to draw a textured-quad.
//
void Layer::render_texture(shared_ptr<d3d11::Texture2D> const& texture)
{
	if (_geometry && texture)
	{
		// we need a shader
		if (!_effect) {
			_effect = _device->create_default_effect();
		}

		// bind our states/resource to the pipeline
		d3d11::ScopedBinder<d3d11::Geometry> quad_binder(_geometry);
		d3d11::ScopedBinder<d3d11::Effect> fx_binder(_effect);
		d3d11::ScopedBinder<d3d11::Texture2D> tex_binder(texture);

		// actually draw the quad
		_geometry->draw();
	}
}


Composition::Composition(std::shared_ptr<d3d11::Device> const& device)
	: _device(device)
{
}

void Composition::add_layer(std::shared_ptr<Layer> const& layer)
{
	if (layer) {
		_layers.push_back(layer);
	}
}

void Composition::render()
{
	// pretty simple ... just use painter's algorithm and render 
	// our layers in order (not doing any depth or 3D here)
	for (auto const& layer : _layers) {
		layer->render();
	}
}