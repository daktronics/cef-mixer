#pragma once

#include "d3d11.h"
#include <vector>

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

	virtual void move(float x, float y, float width, float height);	
	virtual void render(std::shared_ptr<d3d11::Context> const&) = 0;

protected:

	void render_texture(
			std::shared_ptr<d3d11::Context> const& ctx, 
			std::shared_ptr<d3d11::Texture2D> const& texture);

	bool flip_;

	std::shared_ptr<d3d11::Geometry> geometry_;
	std::shared_ptr<d3d11::Effect> effect_;
	std::shared_ptr<d3d11::Device> const device_;
};


//
// A collection of layers. 
// A composition will render 1-N layers to a D3D11 device
//
class Composition
{
public:
	Composition(std::shared_ptr<d3d11::Device> const& device);

	void render(std::shared_ptr<d3d11::Context> const&);
	void add_layer(std::shared_ptr<Layer> const& layer);

private:

	std::shared_ptr<d3d11::Device> const device_;
	std::vector<std::shared_ptr<Layer>> layers_;
};

int cef_initialize(HINSTANCE);
void cef_uninitialize();
std::string cef_version();

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