# CEF Offscreen-Rendering (OSR) Mixer Demo

A sample application to demonstrate how to use the proposed OnAcceleratedPaint() callback when using CEF for HTML off-screen rendering.

# Build Instructions

1. Download CEF and apply pull request to create a custom build or download an example binary distribution
2. From a command prompt set the environment variable CEF_ROOT to the location of your CEF binary distribution.  Then run the gen_vs2017.bat script.

```Batchfile
> set CEF_ROOT=<path\to\cef\binary-distribution>
> gen_vs2017.bat
```

3. Open the build/cefmixer.sln solution in Visual Studio
4. Build the ALL_BUILD project
5. Run the cefmixer.exe application

# Usage
Once the cefmixer.exe is built, it can be run without any arguments - in which case it will automatically navigate to https://webglsamples.org/aquarium/aquarium.html

In addition to to rendering an HTML view off-screen, the demo application will also create an overlay layer using a PNG image file.

The following screenshot was taken when running on a gaming monitor at 144Hz:

[cefmixer_demo](https://user-images.githubusercontent.com/2717038/36959722-2af057e8-2009-11e8-94a4-fd556f832001.png)

The url can be specified as a command line argument: (width x height for the window size can also be specified)

```Batchfile
cefmixer.exe https://threejs.org/examples/webgl_animation_keyframes_json.html --width=1920 --height=1080
```

# Room for Improvement
A future update should allow the client application to perform SendBeginFrame.


