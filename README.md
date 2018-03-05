# CEF Offscreen-Rendering (OSR) Mixer Demo

A sample application to demonstrate how to use the proposed OnAcceleratedPaint() callback when using CEF for HTML off-screen rendering.  This application uses [pull request 158](https://bitbucket.org/chromiumembedded/cef/pull-requests/158/support-external-textures-in-osr-mode/diff) for CEF which improves the OSR rendering performance.

# Build Instructions

1. Download CEF and apply the pull request to create a custom build or download an example binary distribution
    * [x64 sample binary distribution (Release build only)][x64_build]
    * [x86 sample binary distribution (Release build only)][x86_build]
    * The above sample distributions are not supported official builds - they are intended for testing/demo purposes.
    
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

In addition to to rendering an HTML view off-screen, the demo application will also create an overlay layer using a PNG image file (the DEMO graphic in the screenshots below).

The following screenshot was taken when running on a gaming monitor at 144Hz:

![VSync On][demo1]

The url for the HTML layer can be specified as a command line argument: (width x height for the window size are also supported on the command-line)

```Batchfile
cefmixer.exe https://threejs.org/examples/webgl_animation_keyframes_json.html --width=1920 --height=1080
```
Pressing Ctrl+V while the application is running can show the HTML view running completely unthrottled:

![VSync Off][demo2]

# Room for Improvement
A future update should allow the client application to perform SendBeginFrame by adding a new method to CEF's public interface.

[demo1]: https://user-images.githubusercontent.com/2717038/36959722-2af057e8-2009-11e8-94a4-fd556f832001.png "Cefmixer Demo"
[demo2]: https://user-images.githubusercontent.com/2717038/36979126-25625fcc-204c-11e8-841d-058d2f53ba91.png "No VSync"
[x64_build]: https://s3.amazonaws.com/wesselsga/cef/issue_1006/cef_binary_3.3325.1745.g0492438_windows64_minimal.7z "x64 Distribution"
[x86_build]: https://s3.amazonaws.com/wesselsga/cef/issue_1006/cef_binary_3.3325.1745.g0492438_windows32_minimal.7z "x86 Distribution"


