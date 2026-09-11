// Desktop (OpenGL 3.3) NanoVG backend + embedded font data.
//
// On the Switch the NanoVG GLES3 implementation and the Roboto glyph arrays
// live inside the core OSD translation unit.  The desktop core is built
// without an OpenGL stack, so the implementation and font data are compiled
// here instead.

#include <OpenGL/gl3.h>

#include "../nanovg/nanovg.h"

#define NANOVG_GL3_IMPLEMENTATION
#include "../nanovg/nanovg_gl.h"

#include "../nanovg/Roboto-Regular.h"
#include "../nanovg/Roboto-Bold.h"

#include "../gbastation/NvgUi.h"

NVGcontext* UiCreateGL3Context()
{
  return nvgCreateGL3(NVG_ANTIALIAS);
}
