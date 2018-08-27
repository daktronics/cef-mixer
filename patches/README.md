# Manually patching a CEF branch

The patch files in this folder can be used to enable accelerated offscreen rendering to corresponding CEF branches

Following the instructions in the [Master Build Quickstart][quickstart]

1. Modify the **update.bat** to specify a specific branch.  In this case we'll use 3440 as an example.

```
python ..\automate\automate-git.py <your other arguments> --branch=3440
```

2. Apply the corresponding patch file, (e.g. shared_textures_3440.patch)

```
cd chromium/src/cef
git apply shared_textures_3440.patch
```

3. Run **create.bat** to generate projects

4. Build cef

```
ninja -C out/Release_GN_x64
```

[quickstart]: https://bitbucket.org/chromiumembedded/cef/wiki/MasterBuildQuickStart "Master Build Quickstart"
