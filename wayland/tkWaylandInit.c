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
 *----------------------------------------------------------------------
 */

static const GLfloat quadVertices[] = {
    /* positions        texture coordinates (Flipped Y) */
    -1.0f, -1.0f,       0.0f, 1.0f,   /* bottom left  -> maps to texture top */
     1.0f, -1.0f,       1.0f, 1.0f,   /* bottom right -> maps to texture top */
     1.0f,  1.0f,       1.0f, 0.0f,   /* top right    -> maps to texture bottom */
    -1.0f,  1.0f,       0.0f, 0.0f    /* top left     -> maps to texture bottom */
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error messages to stderr.
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
 *	This is shared across all windows to minimize OpenGL state
 *	changes.
 *
 * Results:
 *	Returns the compiled shader program ID, or 0 on failure.
 *
 * Side effects:
 *	Allocates OpenGL shader objects.
 *
 *----------------------------------------------------------------------
 */

static GLuint
TkGlfwCreateShaderProgram(void)
{
    GLuint vertexShader, fragmentShader, program;
    GLint status;
    char log[512];

    /* Create and compile vertex shader. */
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    if (!status) {
	glGetShaderInfoLog(vertexShader, sizeof(log), NULL, log);
	fprintf(stderr, "Vertex shader compilation failed: %s\n", log);
	return 0;
    }

    /* Create and compile fragment shader. */
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

    /* Create and link program. */
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
 *	texture rendering. These are shared across all windows.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Allocates OpenGL buffer objects.
 *
 *----------------------------------------------------------------------
 */

static int
TkGlfwInitializeGlobalBuffers(void)
{
    if (globalBuffersInitialized) {
	return TCL_OK;
    }

    /* Create VBO. */
    glGenBuffers(1, &globalVBO);
    glBindBuffer(GL_ARRAY_BUFFER, globalVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    /* Create IBO. */
    glGenBuffers(1, &globalIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, globalIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    /* Unbind buffers. */
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes OpenGL buffer objects.
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
 *	Safe to call at any time except during active drawing.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	May allocate a new cg_surface_t and destroy the old one.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwEnsureSurface(WindowMapping *m)
{
    if (!m) {
	return TCL_ERROR;
    }

    /* Check if we need to recreate the surface. */
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
 *	Creates the texture and allocates storage.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Allocates OpenGL texture object.
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

    /* Create shared shader program if not present. */
    if (!sharedShaderProgram) {
	sharedShaderProgram = TkGlfwCreateShaderProgram();
	if (!sharedShaderProgram) {
	    return TCL_ERROR;
	}
    }
    m->texture.program = sharedShaderProgram;

    /* Initialize global buffers if not already done. */
    if (TkGlfwInitializeGlobalBuffers() != TCL_OK) {
	return TCL_ERROR;
    }

    /* Create texture. */
    glGenTextures(1, &m->texture.texture_id);
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Allocate empty texture storage. */
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes OpenGL texture object.
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
 *	Upload libcg surface pixel data to OpenGL texture.
 *	This copies the software-rendered pixels to GPU memory.
 *	libcg surfaces store pixels in ARGB format (premultiplied alpha).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the OpenGL texture with new pixel data.
 *
 *----------------------------------------------------------------------
 */
 
MODULE_SCOPE void
TkGlfwUploadSurfaceToTexture(WindowMapping *m)
{
    if (!m || !m->surface || !m->surface->pixels) return;

    int w = m->surface->width;
    int h = m->surface->height;
    int stride = m->surface->stride;
    unsigned char *srcBase = (unsigned char *)m->surface->pixels;

    /* Allocate the destination buffer for RGBA. */
    uint32_t *rgba = (uint32_t *)ckalloc(w * h * sizeof(uint32_t));

    for (int y = 0; y < h; y++) {
        /* We MUST use the stride to find the start of the row.
         * Then we treat the row as a byte array to avoid pointer math errors.
         */
        unsigned char *srcRow = srcBase + (y * stride);
        
        for (int x = 0; x < w; x++) {
            /* libcg usually stores pixels as BGRA or ARGB depending on endianness.
             * For most Wayland systems (Little Endian), it's 0xBB 0xGG 0xRR 0xAA.
             */
            unsigned char b = srcRow[x * 4 + 0];
            unsigned char g = srcRow[x * 4 + 1];
            unsigned char r = srcRow[x * 4 + 2];
            /* We ignore srcRow[x * 4 + 3] because we want OPAQUE */

            /* Pack into GL_RGBA order (Little Endian):
             * Byte 0: R, Byte 1: G, Byte 2: B, Byte 3: 0xFF.
             */
            rgba[y * w + x] = (uint32_t)r | 
                             ((uint32_t)g << 8) | 
                             ((uint32_t)b << 16) | 
                             (0xFFu << 24);
        }
    }

    /* Ensure the context is current before talking to the GPU. */
    glfwMakeContextCurrent(m->glfwWindow);
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    
    /* Upload to GPU. Using GL_UNSIGNED_BYTE with our manually packed
     * uint32_t ensures Red is in the first byte, Green in second, etc.
     */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    ckfree((char *)rgba);
    
    /* Flag reset: Only do this here so DisplayProc knows 
     * the work is actually finished.
     */
    m->texture.needs_texture_update = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRenderTexture --
 *
 *	Render the texture to the screen using a full-screen quad.
 *	This draws the libcg-rendered content to the window.
 *	Assumes the GL context is current and a frame is open.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the texture to the current OpenGL context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwRenderTexture(WindowMapping *m)
{
    GLint posAttrib, texAttrib, texUniform;

    if (!m || !m->glfwWindow || !m->texture.texture_id) return;
    if (!m->texture.program || !globalVBO || !globalIBO) return;

    glfwMakeContextCurrent(m->glfwWindow);

    /* Set the viewport to match the window size. */
    glViewport(0, 0, m->width, m->height);
    
    /* Disable blending so Alpha channel bugs don't create holes. */
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m->texture.program);

    posAttrib = glGetAttribLocation(m->texture.program, "a_position");
    texAttrib = glGetAttribLocation(m->texture.program, "a_texcoord");
    texUniform = glGetUniformLocation(m->texture.program, "u_texture");

    if (posAttrib < 0 || texAttrib < 0 || texUniform < 0) {
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
 *	Initialize the GLFW library and create a shared context window.
 *	libcg contexts are surface-bound and are created per-window in
 *	TkGlfwCreateWindow; no global cg context is needed here.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW and creates a hidden shared GL context window.
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

    /* Initialize OpenGL ES. */
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);

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
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW terminated, all mappings freed.
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

    /* Clean up all window mappings. */
    CleanupAllMappings();

    /* Destroy shared shader program. */
    if (sharedShaderProgram) {
	glDeleteProgram(sharedShaderProgram);
	sharedShaderProgram = 0;
    }

    /* Clean up global buffers. */
    TkGlfwCleanupGlobalBuffers();

    /* Destroy the global cg context if one was allocated. */
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
 * Results:
 *	Returns the GLFWwindow pointer on success, NULL on failure.
 *
 * Side effects:
 *	Creates a new GLFW window, a cg_surface_t, and adds the window
 *	to the mapping list.
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

    /* Reuse existing mapping if already created for this TkWindow. */
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
    mapping->surface = NULL;  /* Will be created lazily in EnsureSurface */
    mapping->needsDisplay = 0;
    mapping->frameOpen = 0;
    mapping->inEventCycle = 0;

    AddMapping(mapping);
    glfwSetWindowUserPointer(window, mapping);

    if (tkWin != NULL) {
	TkGlfwSetupCallbacks(window, tkWin);
    }

    /* Ensure surface exists. */
    if (TkGlfwEnsureSurface(mapping) != TCL_OK) {
	fprintf(stderr, "TkGlfwCreateWindow: failed to create surface\n");
	glfwDestroyWindow(window);
	ckfree((char *)mapping);
	return NULL;
    }

    /* Initialize OpenGL texture for this window. */
    if (TkGlfwInitializeTexture(mapping) != TCL_OK) {
	fprintf(stderr, "TkGlfwCreateWindow: failed to initialize texture\n");
	cg_surface_destroy(mapping->surface);
	glfwDestroyWindow(window);
	ckfree((char *)mapping);
	return NULL;
    }

    /* Register this drawable with the mapping. */
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
 *	Destroy a GLFW window and free the associated cg surface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the window from the mapping list, frees its cg surface,
 *	and destroys the GLFW window.
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
 *	Handle window resize - mark surface as stale for recreation and
 *	immediately recreate it to ensure it's ready for the next draw.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates cached dimensions, marks surface as stale, and recreates
 *	the cg surface with the new size.
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
	/* Mark surface as stale - will be recreated immediately */
	m->surfaceStale = 1;
	m->texture.needs_texture_update = 1;

	/* Immediately recreate the cg surface with the new size */
	if (TkGlfwEnsureSurface(m) != TCL_OK) {
	    fprintf(stderr, "TkGlfwResizeWindow: failed to ensure surface after resize\n");
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
 * Results:
 *	None.
 *
 * Side effects:
 *	mapping->width and mapping->height updated.
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
 *	Compute the cumulative offset of a widget relative to its
 *	toplevel window. This traverses the widget hierarchy to
 *	calculate the absolute position.
 *
 * Results:
 *	None. Output parameters x and y are set to the offset.
 *
 * Side effects:
 *	None.
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
 *	This creates a mapping entry that stores the drawable ID, the
 *	toplevel mapping, and the widget's offset and dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a new DrawableMapping and adds it to the global list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
AddDrawableMapping(Drawable drawable, TkWindow *winPtr)
{
    if (!drawable || !winPtr) return;

    /* Find the toplevel window. */
    TkWindow *top = winPtr;
    while (!Tk_IsTopLevel(top)) {
        top = top->parentPtr;
        if (!top) return;
    }

    /* Find the mapping for the toplevel. */
    WindowMapping *m = FindMappingByTk(top);
    if (!m) return;

    /* Allocate and initialize the mapping entry. */
    DrawableMapping *dm = (DrawableMapping *)ckalloc(sizeof(DrawableMapping));
    memset(dm, 0, sizeof(DrawableMapping));

    dm->drawable = drawable;
    dm->toplevel = m;

    /* Compute the widget's offset within the toplevel. */
    ComputeWidgetOffset(winPtr, top, &dm->x, &dm->y);

    dm->width  = Tk_Width(winPtr);
    dm->height = Tk_Height(winPtr);

    /* Add to the list. */
    dm->next = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * FindDrawableMapping --
 *
 *	Find the DrawableMapping entry associated with a drawable ID.
 *
 * Results:
 *	Returns a pointer to the DrawableMapping, or NULL if not found.
 *
 * Side effects:
 *	None.
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

/*
 *----------------------------------------------------------------------
 *
 * RemoveDrawableMapping --
 *
 *	Remove and free a DrawableMapping entry associated with a drawable ID.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the DrawableMapping entry.
 *
 *----------------------------------------------------------------------
 */

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
 *	Ensures a valid surface exists, creates a cg_ctx_t, and applies
 *	child-window translation and GC settings.
 *
 * Results:
 *	TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Allocates a cg_ctx_t stored in dcPtr->cg.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr)
{
    DrawableMapping *dm = FindDrawableMapping(drawable);
    if (!dm) {
        fprintf(stderr, "BeginDraw: NO drawable mapping for %lu\n",
                (unsigned long)drawable);
        return TCL_ERROR;
    }

    WindowMapping *m = dm->toplevel;
    if (!m || !m->surface) return TCL_ERROR;

    if (TkGlfwEnsureSurface(m) != TCL_OK) return TCL_ERROR;

    memset(dcPtr, 0, sizeof(*dcPtr));
    dcPtr->drawable = drawable;
    dcPtr->width = dm->width;
    dcPtr->height = dm->height;
    dcPtr->offsetX = dm->x;
    dcPtr->offsetY = dm->y;

    dcPtr->cg = cg_create(m->surface);
    if (!dcPtr->cg) return TCL_ERROR;

    /* Translate into child widget region */
    cg_translate(dcPtr->cg, dm->x, dm->y);

    /* Fill the background with solid gray before drawing.
     * This replaces transparency with solid Gray.
     */
    cg_set_source_rgba(dcPtr->cg, 0.92, 0.92, 0.92, 1.0);
    cg_set_operator(dcPtr->cg, CG_OPERATOR_SRC);
    cg_paint(dcPtr->cg);
    
    /* Reset to normal mode so widgets draw ON TOP of the gray. */
    cg_set_operator(dcPtr->cg, CG_OPERATOR_SRC_OVER);

    /* Apply GC settings if provided. */
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
 *	End a drawing operation: restore cg state and release the context.
 *	Marks the window as needing display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys dcPtr->cg and marks the window dirty.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *m = NULL;
    
    if (dcPtr->cg) {
        /* Find the mapping for this drawable. */
        DrawableMapping *dm = FindDrawableMapping(dcPtr->drawable);
        if (dm && dm->toplevel) {
            m = dm->toplevel;
        }
        cg_destroy(dcPtr->cg);
        dcPtr->cg = NULL;
    }

    if (m) {
        m->texture.needs_texture_update = 1; 
        m->needsDisplay = 1;
        m->frameOpen = 0;
        
        /* This is vital! It tells Tcl to run the DisplayProc 
         * right now instead of waiting for a mouse move.
         */
        Tcl_ThreadAlert(Tcl_GetCurrentThread());
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRegisterChildDrawable --
 *
 *	Register a drawable for a child widget with the parent's window
 *	mapping. This allows proper lookup of the WindowMapping for any
 *	drawable ID, including those belonging to child windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds an entry to drawableMappingList associating the drawable
 *	with the toplevel window's mapping.
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

    /* Fallback for toplevels */
    WindowMapping *m = FindMappingByTk(tkWin);
    if (m) {
	AddDrawableMapping(drawable, tkWin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContext --
 *
 *	Return the global libcg context, if one exists.
 *	cg contexts are normally per-window; this returns the global
 *	fallback stored in glfwContext.cg (may be NULL).
 *
 * Results:
 *	The cg_ctx_t pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContext(void)
{
    return (glfwContext.initialized && !shutdownInProgress) ?
	    glfwContext.cg : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContextForMeasure --
 *
 *	Return a cg context suitable for font measurement outside a draw
 *	frame.  Ensures the shared GL context is current.
 *
 * Results:
 *	Returns the cg_ctx_t or NULL on failure.
 *
 * Side effects:
 *	Makes the shared GL context current if no context is current.
 *
 *----------------------------------------------------------------------
 */

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
 *	Process pending GLFW events. Called from the Tk event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Polls and dispatches GLFW events.
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
 * TkGlfwXColorToCG --
 *
 *	Convert an XColor structure to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t
TkGlfwXColorToCG(XColor *xcolor)
{
    struct cg_color_t c;

    if (!xcolor) {
	c.r = 0.0;
	c.g = 0.0;
	c.b = 0.0;
	c.a = 1.0;
	return c;
    }

    c.r = (xcolor->red >> 8) / 255.0;
    c.g = (xcolor->green >> 8) / 255.0;
    c.b = (xcolor->blue >> 8) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToCG --
 *
 *	Convert a 24-bit RGB pixel value to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t
TkGlfwPixelToCG(unsigned long pixel)
{
    struct cg_color_t c;

    c.r = ((pixel >> 16) & 0xFF) / 255.0;
    c.g = ((pixel >> 8) & 0xFF) / 255.0;
    c.b = (pixel & 0xFF) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Apply settings from a graphics context to a libcg context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets source color, line width, line cap, and line join on the
 *	cg context based on the GC values.
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
	case CapRound:
	    cg_set_line_cap(cg, CG_LINE_CAP_ROUND);
	    break;
	case CapProjecting:
	    cg_set_line_cap(cg, CG_LINE_CAP_SQUARE);
	    break;
	default:
	    cg_set_line_cap(cg, CG_LINE_CAP_BUTT);
	    break;
    }

    switch (v.join_style) {
	case JoinRound:
	    cg_set_line_join(cg, CG_LINE_JOIN_ROUND);
	    break;
	case JoinBevel:
	    cg_set_line_join(cg, CG_LINE_JOIN_BEVEL);
	    break;
	default:
	    cg_set_line_join(cg, CG_LINE_JOIN_MITER);
	    break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Initialize the Tk platform-specific layer for Wayland/GLFW.
 *	Called during interpreter initialization.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, Wayland protocols, and various Tk extensions
 *	(tray, system notifications, printing, accessibility).
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
 * TkpGetAppName --
 *
 *	Extract the application name from argv0 for use in window titles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends the application name to the Tcl_DString.
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
	if (p) {
	    name = p + 1;
	}
    }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	Display a warning message to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the warning message to the standard error channel.
 *
 *----------------------------------------------------------------------
 */

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
 * FindMappingByGLFW --
 *
 *	Search the windowMappingList by native GLFW window handle.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;

    while (c) {
	if (c->glfwWindow == w) {
	    return c;
	}
	c = c->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByTk --
 *
 *	Search the windowMappingList by Tk window pointer.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;

    while (c) {
	if (c->tkWindow == w) {
	    return c;
	}
	c = c->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByDrawable --
 *
 *	Search the windowMappingList by Drawable.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	May register an implicit drawable-to-mapping association.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    /* Direct lookup (toplevel drawables) */
    DrawableMapping *dm = drawableMappingList;
    while (dm) {
        if (dm->drawable == d) {
            return dm->toplevel;
        }
        dm = dm->next;
    }

    /* Fallback: resolve Tk window from drawable */
    Tk_Window tkwin = Tk_IdToWindow(TkGetDisplayList()->display, d);
    if (!tkwin) {
        return NULL;
    }

    /* Walk up to the toplevel. */
    Tk_Window top = GetToplevelOfWidget(tkwin);
    if (!top) {
        return NULL;
    }

    /* Return the mapping for the toplevel. */
    return FindMappingByTk((TkWindow *)top);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetMappingList --
 *
 *	Return the head of the windowMappingList.
 *
 * Results:
 *	WindowMapping list head.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
TkGlfwGetMappingList(void)
{
    return windowMappingList;
}

/*
 *----------------------------------------------------------------------
 *
 * AddMapping --
 *
 *	Prepend an entry to the windowMappingList.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry prepended to windowMappingList.
 *
 *----------------------------------------------------------------------
 */

void
AddMapping(WindowMapping *m)
{
    if (!m) {
	return;
    }
    m->nextPtr = windowMappingList;
    windowMappingList = m;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveMapping --
 *
 *	Remove an entry from the windowMappingList and free it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry removed and freed.
 *
 *----------------------------------------------------------------------
 */

void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;

    if (!m) {
	fprintf(stderr, "RemoveMapping: called with NULL mapping\n");
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

/*
 *----------------------------------------------------------------------
 *
 * CleanupAllMappings --
 *
 *	Destroy all GLFW windows, free cg surfaces, and free mapping
 *	structures.  Called during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows destroyed and mappings freed.
 *
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 *
 * RegisterDrawableForMapping --
 *
 *	Associate a Drawable with a WindowMapping in the drawable list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry prepended to drawableMappingList.
 *
 *----------------------------------------------------------------------
 */

void
RegisterDrawableForMapping(Drawable d, WindowMapping *m)
{
    DrawableMapping *dm = ckalloc(sizeof(DrawableMapping));
    memset(dm, 0, sizeof(DrawableMapping));

    dm->drawable = d;
    dm->toplevel = m;
    dm->x = 0;
    dm->y = 0;
    dm->width = m->width;
    dm->height = m->height;
    dm->next = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetGLFWWindow --
 *
 *	Retrieve the GLFW window associated with a Tk window.
 *
 * Results:
 *	GLFWwindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);

    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetDrawable --
 *
 *	Retrieve the Drawable associated with a GLFW window.
 *
 * Results:
 *	Drawable ID, or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);

    return m ? m->drawable : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetWindowFromDrawable --
 *
 *	Retrieve the GLFW window associated with a Drawable.
 *
 * Results:
 *	GLFWwindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);

    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetTkWindow --
 *
 *	Retrieve the Tk window associated with a GLFW window.
 *
 * Results:
 *	TkWindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow *
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);

    return m ? m->tkWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandProcessEvents --
 *
 *	Spins the Glfw event loop.
 *
 * Results:
 *	Events processed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

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