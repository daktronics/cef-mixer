#pragma once

#include "d3d11.h"
#include <vector>

enum class Anchor
{
	TopLeft,
	TopRight,
	Center,
	BottomLeft,
	BottomRight
};

//
// a simple abstraction for a 2D layer within a composition
// 
// see image_layer.cpp or html_layer.cpp for example implementations
//
class Layer
{
public:
	Layer(std::shared_ptr<d3d11::Device> const& device, bool flip);
	~Layer();

	virtual void move(Anchor anchor, float x, float y, float width, float height);	
	virtual void render() = 0;

protected:

	void render_texture(std::shared_ptr<d3d11::Texture2D> const&);

	bool _flip;

	std::shared_ptr<d3d11::Geometry> _geometry;
	std::shared_ptr<d3d11::Effect> _effect;
	std::shared_ptr<d3d11::Device> const _device;
};


//
// A collection of layers. 
// A composition will render 1-N layers to a D3D11 device
//
class Composition
{
public:
	Composition(std::shared_ptr<d3d11::Device> const& device);

	void render();
	void add_layer(std::shared_ptr<Layer> const& layer);

private:

	std::shared_ptr<d3d11::Device> const _device;
	std::vector<std::shared_ptr<Layer>> _layers;
};

int cef_initialize(HINSTANCE);
void cef_uninitialize();
void cef_do_message_work();

// create a layer to show a image
std::shared_ptr<Layer> create_image_layer(
			std::shared_ptr<d3d11::Device> const& device,
			std::string const& file_name);

// create a layer to show an HTML web page (using CEF)
std::shared_ptr<Layer> create_html_layer(
			std::shared_ptr<d3d11::Device> const& device,
			std::string const& url,
			int width,
			int height);