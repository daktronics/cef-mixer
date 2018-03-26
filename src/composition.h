#pragma once

#include "d3d11.h"
#include <vector>

// basic rect for floats
struct Rect
{
	float x;
	float y;
	float width;
	float height;
};

class Composition;

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

	void attach(std::shared_ptr<Composition> const&);

	virtual void move(float x, float y, float width, float height);	
	
	virtual void tick(double);
	virtual void render(std::shared_ptr<d3d11::Context> const&) = 0;

	Rect bounds() const;
	
	std::shared_ptr<Composition> composition() const;

protected:

	void render_texture(
			std::shared_ptr<d3d11::Context> const& ctx, 
			std::shared_ptr<d3d11::Texture2D> const& texture);

	bool flip_;
	Rect bounds_;

	std::shared_ptr<d3d11::Geometry> geometry_;
	std::shared_ptr<d3d11::Effect> effect_;
	std::shared_ptr<d3d11::Device> const device_;

private:
	
	std::weak_ptr<Composition> composition_;

};


//
// A collection of layers. 
// A composition will render 1-N layers to a D3D11 device
//
class Composition : public std::enable_shared_from_this<Composition>
{
public:
	Composition(std::shared_ptr<d3d11::Device> const& device, 
			int width, int height);

	int width() const { return width_; }
	int height() const { return height_; }

	void tick(double);
	void render(std::shared_ptr<d3d11::Context> const&);
	
	void add_layer(std::shared_ptr<Layer> const& layer);
	void resize(int width, int height);

private:

	int width_;
	int height_;

	std::shared_ptr<d3d11::Device> const device_;
	std::vector<std::shared_ptr<Layer>> layers_;
};

int cef_initialize(HINSTANCE);
void cef_uninitialize();
std::string cef_version();

// create a composition from a JSON string
std::shared_ptr<Composition> create_composition(
	std::shared_ptr<d3d11::Device> const& device,
	std::string const& json);

// create a layer to show a image
std::shared_ptr<Layer> create_image_layer(
			std::shared_ptr<d3d11::Device> const& device,
			std::string const& file_name);

// create a layer to show a web page (using CEF)
std::shared_ptr<Layer> create_web_layer(
			std::shared_ptr<d3d11::Device> const& device,
			std::string const& url,
			int width,
			int height);