#include "tkInt.h"
#include "tkGlfwInt.h"

/*
 * The design of Tk takes advantage of a number of features provided by X11,
 * and expects these features to be replicated by any port to a different
 * windowing system.  One such feature is that X11 provides a clipping region
 * for each widget which masks the bounding box of any other widget displayed
 * inside of it.  This allows redrawing a widget without having to redraw all
 * of its descendents and siblings in the widget hierarchy.  While X11 and Tk
 * maintain independent hierarchies, Tk keeps its hierarchy in sync with the
 * X11 hierarchy.  (Tk adds a small amount of extra flexibility, allowing
 * a widget to be displayed inside of a sibling widget which is lower
 * in the stacking order.)
 *
 * Therefore, Tk assumes that any widget can be drawn at any time without
 * redrawing any other widget.  For example, a ttk::frame redraws itself
 * whenever a <Leave> or <Enter> event is received.  Without the clipping,
 * every <Leave> or <Enter> event would cause the ttk::frame to paint over all
 * of the widgets displayed inside of it with the background color of the
 * frame.
 *
 * This file contains an implementation of subwindow clipping regions for the
 * Wayland port of Tk.  The implementation uses the OpenGL depth buffer
 * associated to the backing store framebuffer of a toplevel.  (This requires
 * modifying nanoVG to ensure that an external framebuffer always has a depth
 * buffer.)  Before drawing any widget rectangles corresponding to the
 * widget's clipping region are drawn at height z=-0.5, causing them to have
 * larger depth than the plane z=0, where nanoVG does all of its drawing.  The
 * color buffer is disabled while drawing these rectangles but the depth
 * buffer is enabled, so every fragment inside a clipping rectange in the
 * plane z=0 is covered by a fragment at a higher depth.  Before calling
 * nvgEndFrame a GL_LEQUAL depth test is enabled, causing every nanoVG
 * fragment inside of a clipping rectangle * to be discarded when the nanoVG
 * fragment shader runs. (This requires modifying nanoVG to prevent it
 * from disabling the depth buffer.)
 *
 * Each Tk window owns a VAO in the GL context of its toplevel window.  The
 * VAO manages a VBO that stores the vertices used to fill the rectangular
 * regions of the stencil corresponding to subwindows.  (Each rectangle is
 * triangulated with two triangles, described by 6 vertices in the VBO.)
 */

/*
 * The glsl shaders below are used to draw the clipping rectangles in the
 * plane z=-0.5. The vertex shader just transforms coordinates.  Since no
 * pixel color values are changed when drawing the clipping mask, the fragment
 * shader can be a no-op.  The framebuffer size is provided to the vertex
 * shader as a uniform value in location 0.
 */

static const char* clipVertexShader = 
    "#version 320 es\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 fbSize;\n"
    "void main() {\n"
    "   gl_Position = vec4((aPos.x / fbSize.x) * 2.0 - 1.0, \n"
    "                      1.0 - (aPos.y / fbSize.y) * 2.0, -0.5, 1.0);\n"
    "}\n";

static const char* clipFragmentShader = 
    "#version 320 es\n"
    "precision highp float;\n"
    "out vec4 fragColor;\n"
    "void main() {}\n";

void createClipShaders(TkWindow *winPtr) {
    TkWindow* toplevelPtr;
    for (toplevelPtr = winPtr; !Tk_IsTopLevel(toplevelPtr);
	 toplevelPtr = toplevelPtr->parentPtr) {}
    glfwMakeContextCurrent(toplevelPtr->privatePtr->glfwWindow);
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &clipVertexShader, NULL);
    /* Compile the vertex shader. */
    glCompileShader(vs);
#if 0 // activate to debug compilation of the vertex shader
    GLint vertCompileStatus = GL_FALSE;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &vertCompileStatus);
    if (vertCompileStatus == GL_FALSE) {
	GLint logLength = 0;
	glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logLength);
	if (logLength > 0) {
	    char logBuffer[logLength];
	    glGetShaderInfoLog(vs, logLength, NULL, logBuffer);
	    printf("VERTEX SHADER COMPILE ERROR:\n%s\n", logBuffer);
	}
    }
#endif
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &clipFragmentShader, NULL);
    /* Compile the no-op fragment shader. */
    glCompileShader(fs);
#if 0 // activate to debug compilation of the fragment shader
    GLint fragCompileStatus = GL_FALSE;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &fragCompileStatus);
    if (fragCompileStatus == GL_FALSE) {
        GLint logLength = 0;
	glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logLength);
	if (logLength > 0) {
	    char logBuffer[logLength];
	    glGetShaderInfoLog(fs, logLength, NULL, logBuffer);
	    printf("FRAGMENT SHADER COMPILE ERROR:\n%s\n", logBuffer);
	}
    }
#endif
    winPtr->privatePtr->clipShader = glCreateProgram();
    glAttachShader(winPtr->privatePtr->clipShader, vs);
    glAttachShader(winPtr->privatePtr->clipShader, fs);
    /* Link the shaders into a program.*/
    glLinkProgram(winPtr->privatePtr->clipShader);
#if 0 // activate to debug linking
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(winPtr->privatePtr->clipShader,
		   GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        GLint logLength = 0;
	glGetProgramiv(winPtr->privatePtr->clipShader,
		       GL_INFO_LOG_LENGTH, &logLength);
	if (logLength > 0) {
	    char logBuffer[logLength]; 
	    glGetProgramInfoLog(winPtr->privatePtr->clipShader,
				logLength, NULL, logBuffer);
	    printf("GLSL LINKER FAILURE:\n%s\n", logBuffer);
	} else {
	    printf("GLSL Linker failed, but no reason was provided.\n");
	}
    }
#endif
    /* The shaders can be deleted once the program is linked. */
    glDeleteShader(vs);
    glDetachShader(winPtr->privatePtr->clipShader, vs);
    glDeleteShader(fs);
    glDetachShader(winPtr->privatePtr->clipShader, fs);
    
    /* Save the location of the framebuffer size uniform. */
    winPtr->privatePtr->fbSizeUniform = glGetUniformLocation(
        winPtr->privatePtr->clipShader, "fbSize");

    /* Create the vertex array and buffer objects. */
    glGenVertexArrays(1, &(winPtr->privatePtr->clipVAO));
    glGenBuffers(1, &(winPtr->privatePtr->clipVBO));

    /* Assign location 0 for passing vertices to the vertex shader. */
    glBindVertexArray(winPtr->privatePtr->clipVAO);
    glBindBuffer(GL_ARRAY_BUFFER, winPtr->privatePtr->clipVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);

    /* Restore GL defaults */
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* UpdateClipRects --
 *
 * Called after configure.  TODO: We should set a ClipInvalid flag when
 * windows are created or destroyed or configured, and check the flag before
 * drawing, and call this if the flag is set.
 */

static inline clipRect
getBounds(
    TkWindow *winPtr,
    float scale)
{
    TkWindow *winPtr2 = winPtr;
    float x = 0, y = 0;
    while (!Tk_IsTopLevel(winPtr2)) {
        x += winPtr2->changes.x;
	y += winPtr2->changes.y;
    	winPtr2 = winPtr2->parentPtr;
    }
    return (clipRect) {
        .x = scale * x,
	.y = scale * y,
	.w = scale * Tk_Width(winPtr),
	.h = scale * Tk_Height(winPtr)};
}

static bool disjoint(
    clipRect a,
    clipRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w < 0 || b.h <= 0 ||
	a.x + a.w <= b.x || b.x + b.w <= a.x ||
	a.y + a.w <= b.y || b.y + b.w <= a.y) {
	return true;
    }
    return false;
}

static inline void
addClipRect(
    TkWindow* subwinPtr,
    TkWindow* winPtr,
    float scale)
{
    glfwData *data = winPtr->privatePtr;

    /*
     * Allocate a larger buffer if necessary.
     */
    if (data->clipRectCount >= data->clipRectBufferSize - 1) {
	printf("Reallocating clipRects for %s\n", Tk_PathName(winPtr));
        data->clipRectBufferSize *= 2;
        data->clipRectBuffer = ckrealloc(data->clipRectBuffer,
	    data->clipRectBufferSize * sizeof(clipRect));
    }
    /*
     * The vertex shader used when drawing the clipping rectangles
     * flips the y coordinate, so we should not do that here.  We
     * can use Tk coordinates with origin at the upper left corner.
     */
    int n = data->clipRectCount;
    data->clipRectBuffer[data->clipRectCount++] = getBounds(subwinPtr, scale);
    printf("Adding clipRect for %s in %s: %.0fx%.0f+%.0f+%.0f\n",
	   Tk_PathName(subwinPtr), Tk_PathName(winPtr),
	   data->clipRectBuffer[n].w, data->clipRectBuffer[n].h,
	   data->clipRectBuffer[n].x, data->clipRectBuffer[n].y);
}

void updateClipRects(
     TkWindow* winPtr,
     GLFWwindow* glfwWindow)
{
    printf("updateClipRects: %s\n", Tk_PathName(winPtr));
    float scale;
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    /* Reset the buffer */
    winPtr->privatePtr->clipRectCount = 0;
    clipRect bounds = getBounds(winPtr, scale);
    /* Clip children that overlap the window. */
    for (TkWindow *childPtr = winPtr->childList;
	 childPtr != NULL;
	 childPtr = childPtr->nextPtr) {
	if (Tk_IsMapped(childPtr) &&
	    !disjoint(bounds, getBounds(childPtr, scale))) {
	    addClipRect(childPtr, winPtr, scale);
        }
    }
    /* Clip non-toplevel siblings higher in the stacking order
     * that overlap the window . */
    if (!Tk_IsTopLevel(winPtr)) {
        for (TkWindow *sibPtr = winPtr->nextPtr;
	     sibPtr != NULL;
	     sibPtr = sibPtr->nextPtr) {
	    if (Tk_IsMapped(sibPtr) &&
		!Tk_IsTopLevel(sibPtr) &&
		!disjoint(bounds, getBounds(sibPtr, scale))) {
	        addClipRect(sibPtr, winPtr, scale);
	    }
	}
    }
    if (winPtr->privatePtr->clipRectCount == 0) {
	return;
    }

    /*
     * Load the clipRect vertices into the window's VBO. Each clipRect needs 2
     * triangles, so 6 vertices, so 12 floats.
     *    o-----o
     *    |\    |
     *    | \ 2 |
     *    |  \  |
     *    | 1 \ |
     *    |    \|
     *    o-----o
     */

    int floatCount = 12 * winPtr->privatePtr->clipRectCount;
    float vertices[floatCount];
    int n = 0;

    for (int i = 0; i < winPtr->privatePtr->clipRectCount; i++) {
	clipRect rect = winPtr->privatePtr->clipRectBuffer[i];
	float xmin = rect.x;
	float xmax = rect.x + rect.w;
	float ymin = rect.y;
	float ymax = rect.y + rect.h;
	// Triangle 1
	vertices[n++] = xmin; vertices[n++] = ymin;
	vertices[n++] = xmax; vertices[n++] = ymin;
	vertices[n++] = xmin; vertices[n++] = ymax;
	// Triangle 2
	vertices[n++] = xmax; vertices[n++] = ymin;
	vertices[n++] = xmax; vertices[n++] = ymax;
	vertices[n++] = xmin; vertices[n++] = ymax;
    }

    /* Load the vertices of the triangles into the VBO. */
    glfwMakeContextCurrent(glfwWindow);
    glBindBuffer(GL_ARRAY_BUFFER, winPtr->privatePtr->clipVBO);
    glBufferData(GL_ARRAY_BUFFER, floatCount * sizeof(float),
		 vertices, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawClipMask(
    TkWindow* winPtr,
    GLFWwindow* glfwWindow)
{
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    /* Bind the framebuffer. */
    nvgluBindFramebuffer(infoPtr->winPtr->privatePtr->fb);
    /* Enable drawing to the depth buffer. */
    glDepthMask(GL_TRUE);
    /*
     * Clear all depths to 0.5, which is where nanoVG draws.
     * We need to do this even if there are no clipRects.
     */
    glClearDepthf(0.5f);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (winPtr->privatePtr->clipRectCount == 0) {
	return;
    }
    /* Get the framebuffer size to save in the fbSize uniform. */
    int fbWidth;
    int fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    /* Don't change any colors. */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    /* Clipping rectangles are drawn at z = -0.5, i.e. depth 0.25 */
    glUseProgram(winPtr->privatePtr->clipShader);
    glUniform2f(winPtr->privatePtr->fbSizeUniform,
		(float)fbWidth, (float)fbHeight);
    glBindVertexArray(winPtr->privatePtr->clipVAO);
    glDrawArrays(GL_TRIANGLES, 0, 12 * winPtr->privatePtr->clipRectCount);
    // Restore defaults
    glBindVertexArray(0);
    glUseProgram(0);
    glDepthMask(GL_FALSE); 
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
