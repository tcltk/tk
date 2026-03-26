/*
 * tkWaylandInit.c --
 *
 *	GLFW/Wayland-specific interpreter initialization: context
 *	management, window mapping, drawing context lifecycle, color
 *	conversion, and platform init/cleanup. GLFW, libcg and libdecor
 *	provide the native platform on which Tk's widget set and event loop
 *	are deployed.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

struct TkGlfwContext glfwContext = {0, NULL, NULL, NULL};
WindowMapping *windowMappingList = NULL;
static Drawable nextDrawableId = 1000;
static DrawableMapping *drawableMappingList = NULL;
static int shutdownInProgress = 0;
static GLuint sharedShaderProgram = 0;

/* Vertex and index buffer objects for texture rendering. */
static GLuint globalVBO = 0;
static GLuint globalIBO = 0;
static int globalBuffersInitialized = 0;

/*
 *----------------------------------------------------------------------
 *
 * Forward declarations
 *
 *----------------------------------------------------------------------
 */

extern int TkWaylandGetGCValues(GC gc, unsigned long valuemask, XGCValues *values);
extern void TkWaylandMenuInit(void);
extern void Tk_WaylandSetupTkNotifier(void);
extern int Tktray_Init(Tcl_Interp *interp);
extern int SysNotify_Init(Tcl_Interp *interp);
extern int Cups_Init(Tcl_Interp *interp);
extern void TkGlfwSetupCallbacks(GLFWwindow *window, TkWindow *tkWin);
extern Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
extern void TkWaylandAccessibility_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Vertex and fragment shaders for displaying textures
 *
 *----------------------------------------------------------------------
 */

static const char *vertexShaderSource =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

static const char *fragmentShaderSource =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_texture, v_texcoord);\n"
    "}\n";

/*
 *----------------------------------------------------------------------
 *
 * Vertex data for full-screen quad
 *
 * The texture Y-axis is flipped relative to screen space.
 * libcg writes pixels top-to-bottom (row 0 = top of image).
 * OpenGL's default texture origin is bottom-left, so v_texcoord.y=0
 * maps to the bottom of the screen without the flip.
 * We flip here so that texture row 0 appears at the top of the window.
 *
 *----------------------------------------------------------------------
 */

static const GLfloat quadVertices[] = {
    /* NDC position     texcoord (Y flipped) */
    -1.0f, -1.0f,       0.0f, 1.0f,   /* bottom-left  screen -> top    of texture */
     1.0f, -1.0f,       1.0f, 1.0f,   /* bottom-right screen -> top    of texture */
     1.0f,  1.0f,       1.0f, 0.0f,   /* top-right    screen -> bottom of texture */
    -1.0f,  1.0f,       0.0f, 0.0f    /* top-left     screen -> bottom of texture */
};

static const GLushort quadIndices[] = {
    0, 1, 2,
    2, 3, 0
};

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback that prints errors to stderr.
 *	Silences errors during shutdown.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(int error, const char *desc)
{
    if (shutdownInProgress) {
	return;
    }

    if (glfwContext.initialized && glfwContext.mainWindow) {
	fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateShaderProgram --
 *
 *	Create and compile the shader program for texture rendering.
 *	Shared across all windows to minimize OpenGL state changes.
 *
 * Results:
 *	Returns the compiled shader program ID, or 0 on failure.
 *
 *----------------------------------------------------------------------
 */

static GLuint
TkGlfwCreateShaderProgram(void)
{
    GLuint vertexShader, fragmentShader, program;
    GLint status;
    char log[512];

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    if (!status) {
	glGetShaderInfoLog(vertexShader, sizeof(log), NULL, log);
	fprintf(stderr, "Vertex shader compilation failed: %s\n", log);
	return 0;
    }

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
    if (!status) {
	glGetShaderInfoLog(fragmentShader, sizeof(log), NULL, log);
	fprintf(stderr, "Fragment shader compilation failed: %s\n", log);
	glDeleteShader(vertexShader);
	return 0;
    }

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
	glGetProgramInfoLog(program, sizeof(log), NULL, log);
	fprintf(stderr, "Program linking failed: %s\n", log);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	glDeleteProgram(program);
	return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitializeGlobalBuffers --
 *
 *	Initialize the global vertex and index buffers used for
 *	texture rendering. Shared across all windows.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 *----------------------------------------------------------------------
 */

static int
TkGlfwInitializeGlobalBuffers(void)
{
    if (globalBuffersInitialized) {
	return TCL_OK;
    }

    glGenBuffers(1, &globalVBO);
    glBindBuffer(GL_ARRAY_BUFFER, globalVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &globalIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, globalIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    globalBuffersInitialized = 1;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCleanupGlobalBuffers --
 *
 *	Clean up the global vertex and index buffers.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwCleanupGlobalBuffers(void)
{
    if (globalVBO) {
	glDeleteBuffers(1, &globalVBO);
	globalVBO = 0;
    }
    if (globalIBO) {
	glDeleteBuffers(1, &globalIBO);
	globalIBO = 0;
    }
    globalBuffersInitialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEnsureSurface --
 *
 *	Ensure a valid libcg surface exists for the window.
 *	Recreates the surface if needed (stale or wrong size).
 *
 *	NOTE: When a surface is recreated it must be cleared to the
 *	default background colour so that the next expose cycle does not
 *	show garbage pixels.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwEnsureSurface(WindowMapping *m)
{
    if (!m) {
	return TCL_ERROR;
    }

    if (m->surfaceStale || !m->surface ||
	(m->width != m->surface->width || m->height != m->surface->height)) {

	if (m->surface) {
	    cg_surface_destroy(m->surface);
	    m->surface = NULL;
	}

	m->surface = cg_surface_create(m->width, m->height);
	if (!m->surface) {
	    fprintf(stderr, "TkGlfwEnsureSurface: cg_surface_create(%d,%d) failed\n",
		    m->width, m->height);
	    return TCL_ERROR;
	}

	/*
	 * Initialise the new surface to the default Tk background colour.
	 * This guarantees a solid base so that any widget that does not
	 * paint every pixel still shows the correct background rather than
	 * transparent/black pixels leaking through to the GPU texture.
	 */
	struct cg_ctx_t *initCg = cg_create(m->surface);
	if (initCg) {
	    cg_set_source_rgba(initCg, 0.92, 0.92, 0.92, 1.0);
	    cg_set_operator(initCg, CG_OPERATOR_SRC);
	    cg_paint(initCg);
	    cg_destroy(initCg);
	}

	m->surfaceStale = 0;
	m->texture.needs_texture_update = 1;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitializeTexture --
 *
 *	Initialize OpenGL texture for a window.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitializeTexture(WindowMapping *m)
{
    if (!m || !m->glfwWindow) {
	return TCL_ERROR;
    }

    glfwMakeContextCurrent(m->glfwWindow);

    if (!sharedShaderProgram) {
	sharedShaderProgram = TkGlfwCreateShaderProgram();
	if (!sharedShaderProgram) {
	    return TCL_ERROR;
	}
    }
    m->texture.program = sharedShaderProgram;

    if (TkGlfwInitializeGlobalBuffers() != TCL_OK) {
	return TCL_ERROR;
    }

    glGenTextures(1, &m->texture.texture_id);
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Allocate storage; pixels supplied later by Upload. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m->width, m->height, 0,
		 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(GL_TEXTURE_2D, 0);

    m->texture.width = m->width;
    m->texture.height = m->height;
    m->texture.needs_texture_update = 1;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCleanupTexture --
 *
 *	Clean up OpenGL texture resources for a window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCleanupTexture(WindowMapping *m)
{
    if (!m) {
	return;
    }

    if (m->texture.texture_id) {
	glDeleteTextures(1, &m->texture.texture_id);
	m->texture.texture_id = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUploadSurfaceToTexture --
 *
 *	Upload libcg surface pixel data to the OpenGL texture.
 *
 *	libcg stores pixels as premultiplied BGRA on little-endian systems
 *	(byte order: B G R A).  OpenGL GL_RGBA/GL_UNSIGNED_BYTE expects
 *	bytes in R G B A order.  We convert here.
 *
 *	The GL context for this window MUST be current before calling.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUploadSurfaceToTexture(WindowMapping *m)
{
    if (!m || !m->surface || !m->surface->pixels) return;

    int w      = m->surface->width;
    int h      = m->surface->height;
    int stride = m->surface->stride;   /* bytes per row (may be > w*4) */
    unsigned char *srcBase = (unsigned char *)m->surface->pixels;

    uint32_t *rgba = (uint32_t *)ckalloc(w * h * sizeof(uint32_t));
    if (!rgba) return;

    for (int y = 0; y < h; y++) {
        unsigned char *srcRow = srcBase + (y * stride);
        for (int x = 0; x < w; x++) {
            /*
             * libcg pixel on LE: byte[0]=B  byte[1]=G  byte[2]=R  byte[3]=A
             * We want GL_RGBA byte order: byte[0]=R byte[1]=G byte[2]=B byte[3]=A
             * Force alpha to 0xFF so we never get transparent holes.
             */
            unsigned char b = srcRow[x * 4 + 0];
            unsigned char g = srcRow[x * 4 + 1];
            unsigned char r = srcRow[x * 4 + 2];
            /* byte[3] is alpha — discarded; we composite onto opaque gray */

            rgba[y * w + x] = (uint32_t)r
                             | ((uint32_t)g <<  8)
                             | ((uint32_t)b << 16)
                             | (0xFFu      << 24);
        }
    }

    /* Context must already be current (caller's responsibility). */
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    ckfree((char *)rgba);

    m->texture.needs_texture_update = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRenderTexture --
 *
 *	Render the window texture to the screen using a full-screen quad.
 *	The GL context must be current and the viewport already set.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwRenderTexture(WindowMapping *m)
{
    GLint posAttrib, texAttrib, texUniform;

    if (!m || !m->glfwWindow || !m->texture.texture_id) return;
    if (!m->texture.program || !globalVBO || !globalIBO) return;

    /* Disable blending — we composite in software; GPU sees opaque pixels. */
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m->texture.program);

    posAttrib  = glGetAttribLocation(m->texture.program, "a_position");
    texAttrib  = glGetAttribLocation(m->texture.program, "a_texcoord");
    texUniform = glGetUniformLocation(m->texture.program, "u_texture");

    if (posAttrib < 0 || texAttrib < 0 || texUniform < 0) {
        fprintf(stderr, "TkGlfwRenderTexture: bad attribute/uniform location\n");
        glUseProgram(0);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, globalVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, globalIBO);

    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), (void *)0);

    glEnableVertexAttribArray(texAttrib);
    glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glUniform1i(texUniform, 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray(posAttrib);
    glDisableVertexAttribArray(texAttrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *	Initialize GLFW and create a shared hidden context window.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (glfwContext.initialized) {
	return TCL_OK;
    }

    glfwSetErrorCallback(TkGlfwErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
	fprintf(stderr, "TkGlfwInitialize: glfwInit() failed\n");
	return TCL_ERROR;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfwContext.mainWindow = glfwCreateWindow(640, 480, "Tk Shared Context",
					      NULL, NULL);
    if (!glfwContext.mainWindow) {
	fprintf(stderr, "TkGlfwInitialize: failed to create shared window\n");
	glfwTerminate();
	return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSwapInterval(1);

    glfwContext.initialized = 1;
    shutdownInProgress = 0;

    Tcl_CreateExitHandler(TkGlfwShutdown, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwShutdown --
 *
 *	Orderly cleanup of GLFW resources on app shutdown.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwShutdown(TCL_UNUSED(void *))
{
    if (shutdownInProgress) {
	return;
    }
    shutdownInProgress = 1;

    if (!glfwContext.initialized) {
	shutdownInProgress = 0;
	return;
    }

    CleanupAllMappings();

    if (sharedShaderProgram) {
	glDeleteProgram(sharedShaderProgram);
	sharedShaderProgram = 0;
    }

    TkGlfwCleanupGlobalBuffers();

    if (glfwContext.cg) {
	cg_destroy(glfwContext.cg);
	glfwContext.cg = NULL;
    }

    if (glfwContext.mainWindow) {
	glfwDestroyWindow(glfwContext.mainWindow);
	glfwContext.mainWindow = NULL;
    }

    glfwPollEvents();

    if (glfwContext.initialized) {
	glfwTerminate();
	glfwContext.initialized = 0;
    }

    shutdownInProgress = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateWindow --
 *
 *	Create a new GLFW window and allocate a libcg surface for it.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow *tkWin,
    int width,
    int height,
    const char *title,
    Drawable *drawableOut)
{
    WindowMapping *mapping;
    GLFWwindow *window;

    window = NULL;

    if (shutdownInProgress) {
	return NULL;
    }

    if (!glfwContext.initialized) {
	if (TkGlfwInitialize() != TCL_OK) {
	    return NULL;
	}
    }

    if (tkWin != NULL) {
	mapping = FindMappingByTk(tkWin);
	if (mapping != NULL) {
	    if (drawableOut) {
		*drawableOut = mapping->drawable;
	    }
	    return mapping->glfwWindow;
	}
    }

    if (width <= 0) width = 200;
    if (height <= 0) height = 200;

    if (glfwContext.mainWindow != NULL) {
	window = glfwContext.mainWindow;
	glfwSetWindowSize(window, width, height);
	glfwSetWindowTitle(window, title ? title : "");
	glfwShowWindow(window);
	glfwContext.mainWindow = NULL;
    } else {
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
	glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
	window = glfwCreateWindow(width, height, title ? title : "",
				  NULL, NULL);
	if (!window) {
	    return NULL;
	}
	glfwShowWindow(window);
    }

    mapping = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(mapping, 0, sizeof(WindowMapping));
    mapping->tkWindow = tkWin;
    mapping->glfwWindow = window;
    mapping->drawable = nextDrawableId++;
    mapping->width = width;
    mapping->height = height;
    mapping->surfaceStale = 0;
    mapping->surface = NULL;
    mapping->needsDisplay = 0;
    mapping->frameOpen = 0;
    mapping->inEventCycle = 0;

    AddMapping(mapping);
    glfwSetWindowUserPointer(window, mapping);

    if (tkWin != NULL) {
	TkGlfwSetupCallbacks(window, tkWin);
    }

    if (TkGlfwEnsureSurface(mapping) != TCL_OK) {
	fprintf(stderr, "TkGlfwCreateWindow: failed to create surface\n");
	glfwDestroyWindow(window);
	ckfree((char *)mapping);
	return NULL;
    }

    if (TkGlfwInitializeTexture(mapping) != TCL_OK) {
	fprintf(stderr, "TkGlfwCreateWindow: failed to initialize texture\n");
	cg_surface_destroy(mapping->surface);
	glfwDestroyWindow(window);
	ckfree((char *)mapping);
	return NULL;
    }

    RegisterDrawableForMapping(mapping->drawable, mapping);

    /* Wait for the compositor to confirm real dimensions. */
    int timeout = 0;
    while ((mapping->width == 0 || mapping->height == 0) && timeout < 100) {
	glfwPollEvents();
	if (mapping->width == 0 || mapping->height == 0) {
	    int w, h;
	    glfwGetWindowSize(window, &w, &h);
	    if (w > 0 && h > 0) {
		mapping->width = w;
		mapping->height = h;
		break;
	    }
	}
	timeout++;
    }

    if (mapping->width == 0) mapping->width = width;
    if (mapping->height == 0) mapping->height = height;

    if (tkWin != NULL) {
	tkWin->changes.width = mapping->width;
	tkWin->changes.height = mapping->height;
    }

    if (drawableOut) {
	*drawableOut = mapping->drawable;
    }

    if (tkWin != NULL) {
	TkWaylandQueueExposeEvent(tkWin, 0, 0, mapping->width, mapping->height);
    }

    TkWaylandWakeupGLFW();

    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and free associated resources.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;

    if (!glfwWindow) {
	return;
    }
    if (shutdownInProgress) {
	return;
    }

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
	if (mapping->surface) {
	    cg_surface_destroy(mapping->surface);
	    mapping->surface = NULL;
	}
	TkGlfwCleanupTexture(mapping);
	mapping->glfwWindow = NULL;
	RemoveMapping(mapping);
    }

    glfwDestroyWindow(glfwWindow);

    if (Tk_GetNumMainWindows() == 0 && !shutdownInProgress) {
	Tcl_DoWhenIdle(TkGlfwShutdown, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwResizeWindow --
 *
 *	Handle window resize: update dimensions, mark surface stale,
 *	and immediately recreate it so the next draw has a valid surface.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);

    if (m) {
	m->width = width;
	m->height = height;
	m->surfaceStale = 1;
	m->texture.needs_texture_update = 1;

	if (TkGlfwEnsureSurface(m) != TCL_OK) {
	    fprintf(stderr, "TkGlfwResizeWindow: EnsureSurface failed\n");
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *	Update cached dimensions for a GLFW window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height)
{
    TkGlfwResizeWindow(glfwWindow, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeWidgetOffset --
 *
 *	Compute the cumulative offset of a widget relative to its toplevel.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
ComputeWidgetOffset(TkWindow *winPtr, TkWindow *top, int *x, int *y)
{
    *x = 0;
    *y = 0;

    TkWindow *w = winPtr;
    while (w && w != top) {
        *x += w->changes.x;
        *y += w->changes.y;
        w = w->parentPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AddDrawableMapping --
 *
 *	Associate a drawable (child widget) with its toplevel window mapping.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
AddDrawableMapping(Drawable drawable, TkWindow *winPtr)
{
    if (!drawable || !winPtr) return;

    TkWindow *top = winPtr;
    while (!Tk_IsTopLevel(top)) {
        top = top->parentPtr;
        if (!top) return;
    }

    WindowMapping *m = FindMappingByTk(top);
    if (!m) return;

    DrawableMapping *dm = (DrawableMapping *)ckalloc(sizeof(DrawableMapping));
    memset(dm, 0, sizeof(DrawableMapping));

    dm->drawable = drawable;
    dm->toplevel = m;
    ComputeWidgetOffset(winPtr, top, &dm->x, &dm->y);
    dm->width  = Tk_Width(winPtr);
    dm->height = Tk_Height(winPtr);

    dm->next = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * FindDrawableMapping / RemoveDrawableMapping
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE DrawableMapping *
FindDrawableMapping(Drawable d)
{
    DrawableMapping *dm = drawableMappingList;
    while (dm) {
        if (dm->drawable == d) return dm;
        dm = dm->next;
    }
    return NULL;
}

MODULE_SCOPE void
RemoveDrawableMapping(Drawable d)
{
    DrawableMapping **pp = &drawableMappingList;
    while (*pp) {
        if ((*pp)->drawable == d) {
            DrawableMapping *dead = *pp;
            *pp = dead->next;
            ckfree(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *	Prepare a libcg context for drawing to the given drawable.
 *
 *	KEY DESIGN:
 *	  - We create a fresh cg_ctx_t bound to the *persistent* surface.
 *	  - We do NOT clear the surface here.  The surface accumulates all
 *	    widget draws within one expose cycle.  Clearing happens once per
 *	    expose cycle, in TkGlfwClearSurface(), which is called from
 *	    TkWaylandQueueExposeEvent before any widget redraws begin.
 *	  - We only translate into the child-widget region; we do not fill
 *	    any background.  The grey base was already painted at surface-
 *	    creation time (TkGlfwEnsureSurface) and at expose-cycle start.
 *
 * Results:
 *	TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr)
{
    DrawableMapping *dm = FindDrawableMapping(drawable);
    if (!dm) {
        fprintf(stderr, "TkGlfwBeginDraw: no mapping for drawable %lu\n",
                (unsigned long)drawable);
        return TCL_ERROR;
    }

    WindowMapping *m = dm->toplevel;
    if (!m || !m->surface) return TCL_ERROR;

    if (TkGlfwEnsureSurface(m) != TCL_OK) return TCL_ERROR;

    memset(dcPtr, 0, sizeof(*dcPtr));
    dcPtr->drawable = drawable;
    dcPtr->width    = dm->width;
    dcPtr->height   = dm->height;
    dcPtr->offsetX  = dm->x;
    dcPtr->offsetY  = dm->y;

    /* Bind context to the persistent surface — do NOT clear it. */
    dcPtr->cg = cg_create(m->surface);
    if (!dcPtr->cg) return TCL_ERROR;

    /* Translate into the child widget region within the surface. */
    if (dm->x != 0 || dm->y != 0) {
        cg_translate(dcPtr->cg, (double)dm->x, (double)dm->y);
    }

    /* Apply GC state (colour, line width, cap/join). */
    if (gc) {
        TkGlfwApplyGC(dcPtr->cg, gc);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *	End a drawing operation: destroy the cg context and mark the
 *	window dirty.  We schedule a display via Tcl_DoWhenIdle so that
 *	multiple primitives drawn in the same Tk draw pass are coalesced
 *	into a single GPU upload + swap.
 *
 *	We deliberately do NOT call Tcl_ThreadAlert here — that would
 *	cause DisplayProc to fire between individual primitives, uploading
 *	and swapping a partially-drawn frame and producing flicker.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *m = NULL;

    if (dcPtr->cg) {
        DrawableMapping *dm = FindDrawableMapping(dcPtr->drawable);
        if (dm && dm->toplevel) {
            m = dm->toplevel;
        }
        cg_destroy(dcPtr->cg);
        dcPtr->cg = NULL;
    }

    if (m) {
        m->texture.needs_texture_update = 1;

        /*
         * Schedule display only if not already pending.
         * Tcl_DoWhenIdle coalesces: if TkWaylandDisplayProc is already
         * queued, adding it again is a no-op (Tcl deduplicates idle procs
         * with the same proc+clientData pair).
         */
        TkWaylandScheduleDisplay(m);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwClearSurface --
 *
 *	Clear the persistent surface to the default background colour.
 *	Must be called once at the START of each expose cycle, before
 *	any widget redraws.  This gives every widget a clean slate to
 *	paint onto without leaving stale pixels from the previous frame.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwClearSurface(WindowMapping *m)
{
    if (!m || !m->surface) return;

    struct cg_ctx_t *ctx = cg_create(m->surface);
    if (!ctx) return;

    cg_set_source_rgba(ctx, 0.92, 0.92, 0.92, 1.0);
    cg_set_operator(ctx, CG_OPERATOR_SRC);
    cg_paint(ctx);

    cg_destroy(ctx);

    m->texture.needs_texture_update = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRegisterChildDrawable --
 *
 *	Register a drawable for a child widget with the parent's mapping.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwRegisterChildDrawable(Drawable drawable, TkWindow *tkWin)
{
    if (shutdownInProgress || !tkWin || drawable == None) {
	return;
    }

    Tk_Window top = GetToplevelOfWidget((Tk_Window)tkWin);
    if (top) {
	WindowMapping *m = FindMappingByTk((TkWindow *)top);
	if (m) {
	    AddDrawableMapping(drawable, tkWin);
	    return;
	}
    }

    WindowMapping *m = FindMappingByTk(tkWin);
    if (m) {
	AddDrawableMapping(drawable, tkWin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContext / TkGlfwGetCGContextForMeasure
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContext(void)
{
    return (glfwContext.initialized && !shutdownInProgress) ?
	    glfwContext.cg : NULL;
}

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContextForMeasure(void)
{
    if (!glfwContext.initialized || shutdownInProgress) {
	return NULL;
    }
    if (!glfwGetCurrentContext() && glfwContext.mainWindow) {
	glfwMakeContextCurrent(glfwContext.mainWindow);
    }
    return glfwContext.cg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwProcessEvents --
 *
 *	Process pending GLFW events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (glfwContext.initialized && !shutdownInProgress) {
	glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwXColorToCG / TkGlfwPixelToCG
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t
TkGlfwXColorToCG(XColor *xcolor)
{
    struct cg_color_t c;

    if (!xcolor) {
	c.r = c.g = c.b = 0.0;
	c.a = 1.0;
	return c;
    }

    /* XColor channels are 16-bit; shift to 8-bit then normalise. */
    c.r = (xcolor->red   >> 8) / 255.0;
    c.g = (xcolor->green >> 8) / 255.0;
    c.b = (xcolor->blue  >> 8) / 255.0;
    c.a = 1.0;
    return c;
}

MODULE_SCOPE struct cg_color_t
TkGlfwPixelToCG(unsigned long pixel)
{
    struct cg_color_t c;

    c.r = ((pixel >> 16) & 0xFF) / 255.0;
    c.g = ((pixel >>  8) & 0xFF) / 255.0;
    c.b = ( pixel        & 0xFF) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Apply GC settings (colour, line width, cap, join) to a cg context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(struct cg_ctx_t *cg, GC gc)
{
    XGCValues v;
    struct cg_color_t c;
    double lw;

    if (!cg || !gc || shutdownInProgress) {
	return;
    }

    TkWaylandGetGCValues(gc,
	GCForeground | GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle,
	&v);

    c = TkGlfwPixelToCG(v.foreground);
    cg_set_source_rgba(cg, c.r, c.g, c.b, c.a);

    lw = (v.line_width > 0) ? (double)v.line_width : 1.0;
    cg_set_line_width(cg, lw);

    switch (v.cap_style) {
	case CapRound:      cg_set_line_cap(cg, CG_LINE_CAP_ROUND);  break;
	case CapProjecting: cg_set_line_cap(cg, CG_LINE_CAP_SQUARE); break;
	default:            cg_set_line_cap(cg, CG_LINE_CAP_BUTT);   break;
    }

    switch (v.join_style) {
	case JoinRound: cg_set_line_join(cg, CG_LINE_JOIN_ROUND);  break;
	case JoinBevel: cg_set_line_join(cg, CG_LINE_JOIN_BEVEL);  break;
	default:        cg_set_line_join(cg, CG_LINE_JOIN_MITER);  break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Initialize the Tk platform-specific layer for Wayland/GLFW.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(Tcl_Interp *interp)
{
    if (TkGlfwInitialize() != TCL_OK) {
	return TCL_ERROR;
    }
    TkWaylandMenuInit();
    Tk_WaylandSetupTkNotifier();
    Tktray_Init(interp);
    SysNotify_Init(interp);
    Cups_Init(interp);
    TkWaylandAccessibility_Init(interp);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName / TkpDisplayWarning
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(Tcl_Interp *interp, Tcl_DString *namePtr)
{
    const char *p, *name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);

    if (!name || !*name) {
	name = "tk";
    } else {
	p = strrchr(name, '/');
	if (p) name = p + 1;
    }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

void
TkpDisplayWarning(const char *msg, const char *title)
{
    Tcl_Channel ch = Tcl_GetStdChannel(TCL_STDERR);

    if (ch) {
	Tcl_WriteChars(ch, title, TCL_INDEX_NONE);
	Tcl_WriteChars(ch, ": ", 2);
	Tcl_WriteChars(ch, msg, TCL_INDEX_NONE);
	Tcl_WriteChars(ch, "\n", 1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Mapping management: FindMappingByGLFW / FindMappingByTk /
 * FindMappingByDrawable / TkGlfwGetMappingList / AddMapping /
 * RemoveMapping / CleanupAllMappings / RegisterDrawableForMapping
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) {
	if (c->glfwWindow == w) return c;
	c = c->nextPtr;
    }
    return NULL;
}

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) {
	if (c->tkWindow == w) return c;
	c = c->nextPtr;
    }
    return NULL;
}

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    /* Fast path: direct drawable-to-mapping lookup. */
    DrawableMapping *dm = drawableMappingList;
    while (dm) {
        if (dm->drawable == d) return dm->toplevel;
        dm = dm->next;
    }

    /* Slow path: resolve via Tk window hierarchy. */
    Tk_Window tkwin = Tk_IdToWindow(TkGetDisplayList()->display, d);
    if (!tkwin) return NULL;

    Tk_Window top = GetToplevelOfWidget(tkwin);
    if (!top) return NULL;

    return FindMappingByTk((TkWindow *)top);
}

WindowMapping *
TkGlfwGetMappingList(void)
{
    return windowMappingList;
}

void
AddMapping(WindowMapping *m)
{
    if (!m) return;
    m->nextPtr = windowMappingList;
    windowMappingList = m;
}

void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;

    if (!m) {
	fprintf(stderr, "RemoveMapping: called with NULL\n");
	return;
    }

    while (*pp) {
	if (*pp == m) {
	    *pp = m->nextPtr;
	    memset(m, 0, sizeof(WindowMapping));
	    ckfree((char *)m);
	    return;
	}
	pp = &(*pp)->nextPtr;
    }
}

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;

    while (c) {
	n = c->nextPtr;
	if (c->surface) {
	    cg_surface_destroy(c->surface);
	    c->surface = NULL;
	}
	TkGlfwCleanupTexture(c);
	if (c->glfwWindow) {
	    glfwDestroyWindow(c->glfwWindow);
	}
	memset(c, 0, sizeof(WindowMapping));
	ckfree((char *)c);
	c = n;
    }
    windowMappingList = NULL;
}

void
RegisterDrawableForMapping(Drawable d, WindowMapping *m)
{
    DrawableMapping *dm = ckalloc(sizeof(DrawableMapping));
    memset(dm, 0, sizeof(DrawableMapping));

    dm->drawable = d;
    dm->toplevel = m;
    dm->x = 0;
    dm->y = 0;
    dm->width  = m->width;
    dm->height = m->height;
    dm->next   = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * GL/GLFW window accessors
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);
    return m ? m->glfwWindow : NULL;
}

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);
    return m ? m->drawable : 0;
}

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
}

MODULE_SCOPE TkWindow *
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    return m ? m->tkWindow : NULL;
}

MODULE_SCOPE void
TkWaylandProcessEvents(void)
{
    glfwPollEvents();
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
