# macOS-OpenGL-hook
Example of macOS OpenGL hook with dylib injection.

<b>OpenGLSimpleApp</b> - Simple MacOS Application, using NSOpenGLView, drawing simple triangle with custom drawing loop with 1 sec interval. 
<br>Artificially we call
```obj-c
CGLContextObj ctx = 0;
CGLFlushDrawable(ctx);
```
to simulate usual open-gl game/app based behaviour.<br><br>
This example shows hooking <b>CGLFlushDrawable</b> method.

<b>opengl_hook</b> - Our custom library which is going to be injected.

## Usage
Easiest way to inject dylib is using <b>osxinj</b>. However <b>mach_inject</b> can be used directly as well.
I do recommend using [my fork of osxinj](https://github.com/karek314/osxinj) with fixed bootstrap.dylib and few misc fixes.

<pre>
sudo osxinj OpenGLSimpleAPP libopengl_hook.dylib
</pre>

That's it. <b>OpenGLSimpleApp</b> should now additionally draw rectangle over triangle. 

<b>Original</b>
![Original](/unhooked.png?raw=true "Original")
<b>Hooked</b>
![Hooked](/hooked.png?raw=true "Hooked")
