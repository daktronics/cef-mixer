# CEF Offscreen-Rendering (OSR) Mixer Demo

A sample application to demonstrate how to use the proposed `OnAcceleratedPaint()` callback when using CEF for HTML off-screen rendering.  This application uses [pull request 158][pr158] for CEF which improves the OSR rendering performance.

## Build Instructions

1. If you don't have it already - install CMake and Visual Studio 2017
    * VS 2017 Community Edition is fine - just make sure to install C/C++ development tools

2. Download CEF and apply the [pull request][pr158] to create a custom build or download an example binary distribution
    * [x64 sample binary distribution][x64_build] (Release build only)
    * ~~x86 sample binary distribution (Release build only)~~
    * The above sample distributions are not supported official builds - they are intended for testing/demo purposes.
    
3. From a command prompt set the environment variable **CEF_ROOT** to the location of your CEF binary distribution.  Then run the gen_vs2017.bat script.

```
> set CEF_ROOT=<path\to\cef\binary-distribution>
> gen_vs2017.bat
```

> Note: if you're building for x86 you will need to modify gen_vs2017.bat to specify the correct generator for CMake

4. Open the build/cefmixer.sln solution in Visual Studio

> If using one of the sample binary distributions from step 2 - make sure to change the build configuration to **Release** since the distributions above do not contain **Debug** versions

5. Build the **ALL_BUILD** project

6. Run the **cefmixer.exe** application

## Usage
Once the cefmixer.exe is built, it can be run without any arguments - in which case it will automatically navigate to https://webglsamples.org/aquarium/aquarium.html

In addition to rendering an HTML view off-screen, the demo application will also create an overlay layer using a PNG image file (the red DEMO graphic in the screenshots below).

The following screenshot was taken when running on a gaming monitor at 144Hz:

![VSync On][demo1]

The url for the HTML layer can be specified as a command line argument: (width x height for the window size are also supported on the command-line)

```
cefmixer.exe https://threejs.org/examples/webgl_animation_keyframes_json.html --width=960 --height=540
```
Pressing `Ctrl+V` will allow the HTML view to run unthrottled with no v-sync:

![VSync Off][demo2]

Obviously, there are not many use cases to render frames completely unthrottled - but the point is to let the integrating application control all timing aspects. If the integrating application is doing its own v-sync ... then there shouldn't be any other component in the rendering pipeline that is also doing v-sync.  This demo application passes the command-line arg `disable-gpu-vsync` to Chromium.

### Multiple Views

The application can tile a url into layers arranged in a grid to test multiple HTML browser instances.  Each layer is an independent CEF Browser instance.  The following example uses the `--grid` command-line switch to specify a 2 x 2 grid:

```
cefmixer.exe http://webglsamples.org/dynamic-cubemap/dynamic-cubemap.html --grid=2x2
```

![Grid][demo3]

### Custom Layering

The command-line examples above work to get something running quickly.  However, it is also possible to define the layers using a simple JSON file.

For example, if the following is saved to a file called `composition.json` :

```json
{
  "width":960,
  "height":540,
  "layers": [
     {
       "type":"web",
       "src":"http://webglsamples.org/spacerocks/spacerocks.html"
     },
     {
       "type":"web",
       "src":"file:///C:/examples/overlay.svg",
	"left":0.5,
	"top":0.5,
	"width":0.5,
	"height":0.5			
     }
  ]
}
```

> Note: layer position and size are in normalized 0..1 units

We can run `cefmixer` using the above JSON layer description:

```
cefmixer.exe c:\examples\composition.json
```

![JSON][demo4]

The application uses the handy utility method `CefParseJSON` to parse JSON strings.

## Integration
The update to CEF proposes the following changes to the API for application integration.

1. Enable the use of shared textures when using window-less rendering (OSR).

```c
CefWindowInfo info;
info.SetAsWindowless(nullptr);
info.shared_texture_enabled = true;
```

2. Override the new `OnAcceleratedPaint` method in a `CefRenderHandler` derived class:

```c
void OnAcceleratedPaint(
		CefRefPtr<CefBrowser> browser,
		PaintElementType type,
		const RectList& dirtyRects,
		void* share_handle, 
		uint64 sync_key) override
{
}
```

`OnAcceleratedPaint` will be invoked rather than the existing `OnPaint` when `shared_texture_enabled` is set to true and Chromium is able to create a shared D3D11 texture for the HTML view.

## Room for Improvement
A future update could include the following 
 * ~~Allow the client application to perform SendBeginFrame by adding a new method to CEF's public interface.~~
     * ~~Chromium already supports an External BeginFrame source - CEF currently does not expose it directly.~~
     * **Update** this is now supported in the latest revision
 * Update `OffscreenBrowserCompositorOutputSurface` class to handle both the Reflector and a shared texture
     * This was attempted originally but ran into issues creating a complete FBO on the Reflector texture
     * Not a big deal for CEF applications, since CEF does not use the Reflector concept in Chromium anyway.
 * Take the Chromium changes directly to the Chromium team
     * We can get the job done with the patching system built into CEF to apply Chromium changes, but rather the shared texture FBO probably makes more sense as a pull request on Chromium itself.  Seems only reasonable applications that use Headless-mode in Chromium could also benefit from shared textures.

[demo1]: https://user-images.githubusercontent.com/2717038/37864646-def58a70-2f3f-11e8-9df9-551fe65ae766.png "Cefmixer Demo"
[demo2]: https://user-images.githubusercontent.com/2717038/37864824-a02a0648-2f41-11e8-9265-be60ad8bf8a0.png "No VSync"
[demo3]: https://user-images.githubusercontent.com/2717038/37864648-ea76954c-2f3f-11e8-90d6-4130e56086f4.png "Grid"
[demo4]: https://user-images.githubusercontent.com/2717038/37930171-9850afe0-3107-11e8-9a24-21e1b1996fa5.png "JSON"
[x64_build]: https://s3.amazonaws.com/wesselsga/cef/issue_1006/cef_binary_3.3359.1754.g3312bc8_windows64.7z "x64 Distribution"
[pr158]: https://bitbucket.org/chromiumembedded/cef/pull-requests/158/support-external-textures-in-osr-mode/diff "Pull Request"
[changes]: https://github.com/daktronics/cef-mixer/blob/master/CHANGES.md "Walkthrough"

