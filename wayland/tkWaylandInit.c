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
#include <wayland-client.h>
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
extern int IsPixmap(Drawable drawable);

/*
 *----------------------------------------------------------------------
 *
 * Vertex and fragment shaders for texture display
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
    "    vec4 c = texture2D(u_texture, v_texcoord);\n"
    "    gl_FragColor = vec4(c.rgb, 1.0);\n"
    "}\n";

/*
 *----------------------------------------------------------------------
 *
 * Vertex data for full-screen quad
 *
 * libcg writes pixels top-to-bottom (row 0 = top of image).
 * OpenGL's default texture origin is bottom-left, so we flip the
 * Y texcoord here so that surface row 0 appears at the top of the window.
 *
 *----------------------------------------------------------------------
 */

static const GLfloat quadVertices[] = {
    /* NDC position     texcoord (Y flipped so row 0 = screen top) */
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
 * TkGlfwAllocDrawableId --
 *
 *	Allocate the next sequential drawable ID.
 *	Called from Tk_MakeWindow so that child widget drawables are
 *	assigned from the same namespace as toplevel drawables.
 *
 * Results:
 *	A unique Drawable integer.
 *
 * Side effects:
 *	Increments the nextDrawableId counter.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Drawable
TkGlfwAllocDrawableId(void)
{
    return nextDrawableId++;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback. Silenced during shutdown to avoid spurious
 *	error messages during application termination.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error messages to stderr unless shutdown is in progress.
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
 *	Compile and link the texture-display shader program.
 *	Shared across all windows to minimize GPU resource usage.
 *
 * Results:
 *	GL program ID on success, or 0 on failure.
 *
 * Side effects:
 *	Creates a shader program object in the current OpenGL context.
 *
 *----------------------------------------------------------------------
 */

static GLuint
TkGlfwCreateShaderProgram(void)
{
    GLuint vertexShader, fragmentShader, program;
    GLint  status;
    char   log[512];

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
 *	Allocate the shared VBO/IBO for the full-screen quad.
 *	These buffers are reused by all windows to render their textures.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates GL buffer objects and initializes them with quad geometry.
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices,
		 GL_STATIC_DRAW);

    glGenBuffers(1, &globalIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, globalIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices,
		 GL_STATIC_DRAW);

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
 *	Delete the shared VBO and IBO used for quad rendering.
 *	Called during shutdown to release GPU resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes GL buffer objects and resets initialization flags.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwCleanupGlobalBuffers(void)
{
    if (globalVBO) { glDeleteBuffers(1, &globalVBO); globalVBO = 0; }
    if (globalIBO) { glDeleteBuffers(1, &globalIBO); globalIBO = 0; }
    globalBuffersInitialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEnsureSurface --
 *
 *	Ensure a valid libcg surface exists for the window mapping.
 *	Creates or recreates the surface when stale or sized incorrectly.
 *	New surfaces are initialized to the default Tk background color
 *	so that widgets which don't paint every pixel never expose garbage.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates or destroys libcg surfaces as needed; updates texture
 *	needs_texture_update flag.
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
        m->width  != m->surface->width ||
        m->height != m->surface->height) {

        if (m->surface) {
            cg_surface_destroy(m->surface);
            m->surface = NULL;
        }

        m->surface = cg_surface_create(m->width, m->height);
        if (!m->surface) {
            fprintf(stderr,
                    "TkGlfwEnsureSurface: cg_surface_create(%d,%d) failed\n",
                    m->width, m->height);
            return TCL_ERROR;
        }

        /* Initialize every pixel to opaque background. */
        if (m->surface->pixels) {
            int total_pixels = m->width * m->height;
            unsigned char *p = (unsigned char *)m->surface->pixels;
            
            /* Use the window's background color if set, otherwise light gray. */
            unsigned char R, G, B;
            if (m->background_set) {
                R = (m->background_pixel >> 16) & 0xFF;
                G = (m->background_pixel >> 8) & 0xFF;
                B = m->background_pixel & 0xFF;
            } else {
                /* Default Tk background (light gray). */
                R = 235; G = 235; B = 235;
            }
            
            /* Fill with opaque pixels. */
            for (int i = 0; i < total_pixels; i++) {
                p[i*4 + 0] = B;  /* Blue */
                p[i*4 + 1] = G;  /* Green */
                p[i*4 + 2] = R;  /* Red */
                p[i*4 + 3] = 255; /* Alpha - opaque */
            }
        }

        m->surfaceStale = 0;
        m->texture.needs_texture_update = 1;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwClearSurface --
 *
 *	Fill the window surface with the default background color.
 *	Called once at the start of every expose cycle (from
 *	TkWaylandQueueExposeEvent) so that all widget draws within
 *	the cycle accumulate onto a clean slate.
 *
 *	DO NOT call this from TkGlfwBeginDraw — that would erase every
 *	previous primitive in the same expose cycle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears the libcg surface to the default background color and
 *	marks the texture as needing update.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwClearSurface(WindowMapping *m)
{
    if (!m || !m->surface || !m->surface->pixels) return;
    
    int total_pixels = m->width * m->height;
    unsigned char *p = (unsigned char *)m->surface->pixels;
    
    unsigned char R, G, B;
    if (m->background_set) {
        R = (m->background_pixel >> 16) & 0xFF;
        G = (m->background_pixel >> 8) & 0xFF;
        B = m->background_pixel & 0xFF;
    } else {
        /* Default Tk background (light gray). */
        R = 235; G = 235; B = 235;
    }
    
    /* Fill with opaque pixels. */
    for (int i = 0; i < total_pixels; i++) {
        p[i*4 + 0] = B;  /* Blue */
        p[i*4 + 1] = G;  /* Green */
        p[i*4 + 2] = R;  /* Red */
        p[i*4 + 3] = 255; /* Alpha - fully opaque */
    }
    
    m->texture.needs_texture_update = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitializeTexture --
 *
 *	Create the OpenGL texture object for a window mapping.
 *	The shared shader and VBO/IBO are initialized here too if not
 *	yet done.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates GL texture object, initializes shared shader program,
 *	and sets up global buffers if not already done.
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
	if (!sharedShaderProgram) return TCL_ERROR;
    }
    m->texture.program = sharedShaderProgram;

    if (TkGlfwInitializeGlobalBuffers() != TCL_OK) return TCL_ERROR;

    glGenTextures(1, &m->texture.texture_id);
    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Allocate storage; pixels supplied later by upload. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m->width, m->height, 0,
		 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    m->texture.width  = m->width;
    m->texture.height = m->height;
    m->texture.needs_texture_update = 1;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCleanupTexture --
 *
 *	Delete the OpenGL texture object associated with a window mapping.
 *	Called during window destruction to release GPU resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes GL texture and resets texture_id to 0.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCleanupTexture(WindowMapping *m)
{
    if (!m) return;
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
 *	Convert the libcg surface (premultiplied BGRA) to correct RGBA byte
 *	order for OpenGL and upload it to the window's texture.
 *
 *
 *	The GL context for this window MUST be current before calling.
 *	Clears needs_texture_update on success.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Uploads pixel data to GPU texture.
 *
 *----------------------------------------------------------------------
 */
 
MODULE_SCOPE void
TkGlfwUploadSurfaceToTexture(WindowMapping *m)
{
    int w, h, stride, y, x;
    unsigned char *srcBase;
    uint32_t *rgba = NULL;

    if (!m || !m->surface || !m->surface->pixels) {
        return;
    }

    if (!glfwGetCurrentContext() && m->glfwWindow) {
        glfwMakeContextCurrent(m->glfwWindow);
    }

    w = m->surface->width;
    h = m->surface->height;
    stride = m->surface->stride;
    srcBase = (unsigned char *)m->surface->pixels;

    rgba = (uint32_t *)ckalloc(w * h * sizeof(uint32_t));
    if (!rgba) return;

    /* Convert BGRA (libcg) → RGBA (OpenGL) with alpha forced to 255.  */
    for (y = 0; y < h; y++) {
        unsigned char *srcRow = srcBase + y * stride;
        for (x = 0; x < w; x++) {
            unsigned char b = srcRow[x*4 + 0];
            unsigned char g = srcRow[x*4 + 1];
            unsigned char r = srcRow[x*4 + 2];
            unsigned char a = srcRow[x*4 + 3];
            
            /* Force alpha to 255 - ignore any transparency.
            /* This ensures no transparency artifacts reach 
             * the screen.*/
            rgba[y*w + x] = (r << 0) | (g << 8) | (b << 16) | (255 << 24);
        }
    }

    glBindTexture(GL_TEXTURE_2D, m->texture.texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    ckfree((char *)rgba);
    m->texture.needs_texture_update = 0;
}
/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRenderTexture --
 *
 *	Render the window's texture onto a full-screen quad.
 *	The GL context must be current and glViewport already set.
 *	Blending is disabled — we composite in software.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Issues GL draw commands to render the texture to the current
 *	framebuffer.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwRenderTexture(WindowMapping *m)
{
    GLint posAttrib, texAttrib, texUniform;

    if (!m || !m->glfwWindow || !m->texture.texture_id) return;
    if (!m->texture.program || !globalVBO || !globalIBO) return;

    /* Disable blending - we want to replace framebuffer entirely. */
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

    /* Disable blending before drawing. */
    glDisable(GL_BLEND);
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
 *	Initialize GLFW and create the shared hidden GL context window.
 *	This shared context is used for all OpenGL operations and is
 *	promoted to the first real window when needed.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW library, creates a hidden window for context
 *	management, and sets up the shared shader program.
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
    
	glfwWindowHint(GLFW_RED_BITS,   8);
	glfwWindowHint(GLFW_GREEN_BITS, 8);
	glfwWindowHint(GLFW_BLUE_BITS,  8);
	glfwWindowHint(GLFW_ALPHA_BITS, 0);
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
 *	Orderly cleanup of all GLFW and libcg resources.
 *	Called during Tcl exit to release all native resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all windows, cleans up OpenGL resources, and terminates
 *	GLFW library.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwShutdown(TCL_UNUSED(void *))
{
    if (shutdownInProgress) return;
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
 *	Create a GLFW window for a Tk toplevel, allocate the libcg surface,
 *	initialize the GL texture, and register the drawable.
 *
 * Results:
 *	GLFWwindow * on success, NULL on failure.
 *	If drawableOut is non-NULL it receives the Drawable ID.
 *
 * Side effects:
 *	Creates a native window, initializes drawing surfaces, and sets up
 *	the window mapping for later lookups.
 *
 *----------------------------------------------------------------------
 */


MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    WindowMapping *m;
    GLFWwindow    *window;

    if (shutdownInProgress) {
        fprintf(stderr, "TkGlfwCreateWindow: shutdown in progress\n");
        return NULL;
    }

    /* Ensure GLFW is initialized */
    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            fprintf(stderr, "TkGlfwCreateWindow: TkGlfwInitialize failed\n");
            return NULL;
        }
    }

    /* Reuse existing mapping if TkWindow already has one. */
    if (tkWin != NULL) {
        m = FindMappingByTk(tkWin);
        if (m != NULL) {
            if (drawableOut) *drawableOut = m->drawable;
            return m->glfwWindow;
        }
    }

    if (width  <= 1) width  = 200;
    if (height <= 1) height = 200;

    /* Create GLFW window with opaque framebuffer */
    glfwWindowHint(GLFW_RED_BITS,   8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS,  8);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);        /* No alpha channel - force opaque */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);  /* Explicitly disable transparency. */

    window = glfwCreateWindow(width, height, title ? title : "", NULL, NULL);
    if (!window) {
        fprintf(stderr, "TkGlfwCreateWindow: glfwCreateWindow failed\n");
        return NULL;
    }

    glfwShowWindow(window);

    /* Allocate mapping. */
    m = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(m, 0, sizeof(WindowMapping));

    m->tkWindow   = tkWin;
    m->glfwWindow = window;
    m->drawable   = TkGlfwAllocDrawableId();
    m->width      = width;
    m->height     = height;
    m->surface    = NULL;
    m->surfaceStale = 0;
    m->needsDisplay = 0;
    m->frameOpen = 0;
    
    /* Default background - opaque light gray. */
    m->background_pixel = 0xEBEBEB;  /* RGB: 235,235,235 */
    m->background_set = 1;

    /* Add to global list. */
    AddMapping(m);
    glfwSetWindowUserPointer(window, m);

    /* Install callbacks. */
    if (tkWin != NULL) {
        TkGlfwSetupCallbacks(window, tkWin);
    }

    /* Query compositor for actual size. */
    {
        int actual_w = 0, actual_h = 0;
        int timeout = 0;

        while ((actual_w == 0 || actual_h == 0) && timeout < 50) {
            glfwPollEvents();
            glfwGetWindowSize(window, &actual_w, &actual_h);
            timeout++;
            usleep(10000);
        }

        if (actual_w > 0 && actual_h > 0) {
            m->width  = actual_w;
            m->height = actual_h;
        }
    }

    /* Create libcg surface with opaque initial fill. */
    if (TkGlfwEnsureSurface(m) != TCL_OK) {
        fprintf(stderr, "TkGlfwCreateWindow: EnsureSurface failed\n");
        glfwDestroyWindow(window);
        ckfree((char *)m);
        return NULL;
    }

    /* Create GL texture. */
    if (TkGlfwInitializeTexture(m) != TCL_OK) {
        fprintf(stderr, "TkGlfwCreateWindow: InitializeTexture failed\n");
        cg_surface_destroy(m->surface);
        glfwDestroyWindow(window);
        ckfree((char *)m);
        return NULL;
    }

    /* Register drawable → mapping. */
    RegisterDrawableForMapping(m->drawable, m);

    /* Update Tk window geometry. */
    if (tkWin != NULL) {
        tkWin->changes.width  = m->width;
        tkWin->changes.height = m->height;
    }

    if (drawableOut) {
        *drawableOut = m->drawable;
    }

    /* Initial clear with opaque background + schedule display. */
    TkGlfwClearSurface(m);
    m->texture.needs_texture_update = 1;
    TkWaylandScheduleDisplay(m);

    TkWaylandWakeupGLFW();
    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and free its libcg surface and GL texture.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases all resources associated with the window and removes
 *	it from the mapping list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;

    if (!glfwWindow || shutdownInProgress) return;

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
 *	Handle a window resize: update dimensions, mark surface stale,
 *	and immediately recreate the surface at the new size so the next
 *	draw has a valid backing store.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window dimensions, marks surface as stale, and recreates
 *	the libcg surface at the new size.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);

    if (m) {
	m->width  = width;
	m->height = height;
	m->surfaceStale = 1;  /* Mark stale - will be recreated at next expose. */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *	Alias for TkGlfwResizeWindow for compatibility with callers that
 *	expect this function name.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See TkGlfwResizeWindow.
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
 * DrawableMapping management
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * ComputeWidgetOffset --
 *
 *	Accumulate (x,y) of winPtr relative to top by walking parent pointers.
 *
 * Results:
 *	The offset coordinates are stored in the x and y output parameters.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
ComputeWidgetOffset(TkWindow *winPtr, TkWindow *top, int *x, int *y)
{
    TkWindow *w = winPtr;

    *x = 0;
    *y = 0;

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
 *	Associate a child widget's drawable with its toplevel WindowMapping.
 *	Records (x,y) offset and (width,height) at registration time.
 *	Called from TkGlfwRegisterChildDrawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a DrawableMapping entry to the drawableMappingList.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
AddDrawableMapping(Drawable drawable, TkWindow *winPtr)
{
    TkWindow      *top;
    WindowMapping *m;
    DrawableMapping *dm;

    if (!drawable || !winPtr) return;

    top = winPtr;
    while (!Tk_IsTopLevel(top)) {
        top = top->parentPtr;
        if (!top) return;
    }

    m = FindMappingByTk(top);
    if (!m) return;

    dm = (DrawableMapping *)ckalloc(sizeof(DrawableMapping));
    memset(dm, 0, sizeof(DrawableMapping));

    dm->drawable = drawable;
    dm->toplevel = m;
    ComputeWidgetOffset(winPtr, top, &dm->x, &dm->y);
    dm->width  = Tk_Width((Tk_Window)winPtr);
    dm->height = Tk_Height((Tk_Window)winPtr);

    dm->next = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * FindDrawableMapping --
 *
 *	Look up a DrawableMapping by drawable ID.
 *
 * Results:
 *	Pointer to DrawableMapping on success, NULL on failure.
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
    
    /* Reject obviously invalid drawables. */
    if (d == None || d == 0) {
        return NULL;
    }
    
    /* Reject values that look like GC pointers (typical GC pointers are small integers or low addresses). */
    if ((unsigned long)d < 0x10000 && d != 0) {
        /* This might be a GC - let's check if it's in the drawable range. */
        static int gc_warning_printed = 0;
        if (!gc_warning_printed && d > 1000 && d < 2000) {
            fprintf(stderr, "FindDrawableMapping: possible GC %lu passed as drawable\n", 
                    (unsigned long)d);
            gc_warning_printed = 1;
        }
        return NULL;
    }
    
    /* Reject raw pointers that look like stack addresses (0xffff...). */
    if ((unsigned long)d > 0xffff000000000000ULL) {
        static int stack_warning_printed = 0;
        if (!stack_warning_printed) {
            fprintf(stderr, "FindDrawableMapping: rejecting stack pointer %p as drawable\n", 
                    (void*)d);
            stack_warning_printed = 1;
        }
        return NULL;
    }

    while (dm) {
        if (dm->drawable == d) {
            return dm;
        }
        dm = dm->next;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveDrawableMapping --
 *
 *	Remove and free the DrawableMapping for a given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the mapping from the list and frees its memory.
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
 * TkGlfwUpdateDrawableDims --
 *
 *	Refresh the recorded width/height of a child drawable, e.g. after
 *	a ConfigureNotify.  Without this the clipping region may be stale.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the dimensions stored in the DrawableMapping.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateDrawableDims(Drawable d, int width, int height)
{
    DrawableMapping *dm = FindDrawableMapping(d);
    if (dm) {
        dm->width  = width;
        dm->height = height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwRegisterChildDrawable --
 *
 *	Public entry point called from TkpMakeWindow (or lazily from
 *	BeginDraw) to enter a child widget drawable into the lookup table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Registers the drawable with its toplevel window mapping.
 *
 *----------------------------------------------------------------------
 */


MODULE_SCOPE void
TkGlfwRegisterChildDrawable(Drawable drawable, TkWindow *tkWin)
{
    if (shutdownInProgress || !tkWin || drawable == None) return;

    AddDrawableMapping(drawable, tkWin);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *	Prepare a libcg drawing context for the given drawable.
 *
 *	Handles three drawable types transparently:
 *
 *	  Pixmap  — The drawable is a TkWaylandPixmapImpl*.  We bind the
 *	            context directly to the pixmap's own surface.  Draws
 *	            accumulate there and are composited onto the window
 *	            surface later by XCopyArea.
 *
 *	  Window  — The drawable is an integer ID registered in
 *	            drawableMappingList.  We bind to the persistent toplevel
 *	            surface and translate into the widget's sub-region.
 *	            If not yet registered (first draw after TkpMakeWindow)
 *	            we register lazily via Tk_IdToWindow.
 *
 *	KEY: we do NOT clear the surface here.  The surface accumulates all
 *	widget draws within one expose cycle.  Clearing is done once at
 *	expose-cycle start by TkGlfwClearSurface() → called from
 *	TkWaylandQueueExposeEvent.
 *
 * Results:
 *	TCL_OK on success; dcPtr->cg is the live drawing context.
 *	TCL_ERROR on failure; dcPtr->cg is NULL.
 *
 * Side effects:
 *	Creates a cg drawing context and applies GC state. For window
 *	drawables, may lazily register the drawable.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr)
{
    WindowMapping   *m = NULL;
    DrawableMapping *dm;
    TkWaylandPixmapImpl *px;
    int x_offset = 0, y_offset = 0;
    int width = 0, height = 0;

    memset(dcPtr, 0, sizeof(*dcPtr));

    if (drawable == None) return TCL_ERROR;

    /* Pixmap path. */
    if (IsPixmap(drawable)) {
        px = (TkWaylandPixmapImpl *)drawable;
        if (!px->surface) return TCL_ERROR;
        dcPtr->width   = px->width;
        dcPtr->height  = px->height;
        dcPtr->offsetX = 0;
        dcPtr->offsetY = 0;
        dcPtr->isPixmap = 1;
        dcPtr->cg = cg_create(px->surface);
        if (gc) TkGlfwApplyGC(dcPtr->cg, gc);
        return TCL_OK;
    }

    /*
     * Primary path: look up drawableMappingList.
     * This works for ALL window IDs — toplevel and child — because
     * both are registered there (toplevels via RegisterDrawableForMapping,
     * children via AddDrawableMapping/TkGlfwRegisterChildDrawable).
     *
     * Recompute x/y offset from the live TkWindow geometry
     * rather than using the stale values stored at registration time
     * (registration happens in Tk_MakeWindow, before pack/grid runs).
     */
    dm = FindDrawableMapping(drawable);
    if (dm && dm->toplevel) {
        m = dm->toplevel;
        if (m->surface) {
            /*
             * Walk the TkWindow parent chain to get the current offset
             * relative to the toplevel.  dm->toplevel->tkWindow is the
             * toplevel TkWindow.
             */
            TkWindow *top = m->tkWindow;
            TkWindow *win = NULL;

            /* Find the TkWindow for this drawable. */
            Tk_Window tkw = Tk_IdToWindow(
                TkGetDisplayList()->display, drawable);
            if (tkw) {
                win = (TkWindow *)tkw;
            }

            if (win && top) {
                ComputeWidgetOffset(win, top, &x_offset, &y_offset);
                width  = Tk_Width((Tk_Window)win);
                height = Tk_Height((Tk_Window)win);
            } else {
                /* Toplevel drawing into itself — offset is 0,0. */
                x_offset = 0;
                y_offset = 0;
                width  = m->width;
                height = m->height;
            }
            goto have_mapping;
        }
    }

    /*
     * Fallback: Tk_IdToWindow (handles toplevels whose drawable was
     * registered before drawableMappingList was populated).
     */
    {
        Tk_Window tkwin = Tk_IdToWindow(
            TkGetDisplayList()->display, drawable);
        if (tkwin) {
            Tk_Window top = tkwin;
            while (top && !Tk_IsTopLevel(top))
                top = Tk_Parent(top);
            if (top) {
                m = FindMappingByTk((TkWindow *)top);
                int rx, ry, tx, ty;
                Tk_GetRootCoords(tkwin, &rx, &ry);
                Tk_GetRootCoords(top,   &tx, &ty);
                x_offset = rx - tx;
                y_offset = ry - ty;
                width    = Tk_Width(tkwin);
                height   = Tk_Height(tkwin);
            }
        }
    }

    if (!m || !m->surface) return TCL_ERROR;

have_mapping:
    dcPtr->cg = cg_create(m->surface);
    if (!dcPtr->cg) return TCL_ERROR;

    dcPtr->width   = width;
    dcPtr->height  = height;
    dcPtr->offsetX = x_offset;
    dcPtr->offsetY = y_offset;
    dcPtr->drawable = drawable;
    dcPtr->isPixmap = 0;

    if (x_offset != 0 || y_offset != 0)
        cg_translate(dcPtr->cg, (double)x_offset, (double)y_offset);   

    if (gc) TkGlfwApplyGC(dcPtr->cg, gc);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *	End a drawing operation: destroy the cg context and schedule a
 *	deferred display update.
 *
 *	For pixmap drawables the window is NOT marked dirty — the pixmap's
 *	pixels aren't on screen until XCopyArea composites them.
 *
 *	We use Tcl_DoWhenIdle (via TkWaylandScheduleDisplay) so that
 *	multiple primitives drawn in the same Tk draw pass are coalesced
 *	into a single GPU upload + swap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the cg context and schedules a display update for the
 *	window if the drawable is not a pixmap.
 *
 *----------------------------------------------------------------------
 */


MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    WindowMapping   *m = NULL;
    DrawableMapping *dm;

    if (dcPtr->cg) {
        cg_destroy(dcPtr->cg);
        dcPtr->cg = NULL;
    }

    if (dcPtr->isPixmap) return;

    /* Primary path via drawableMappingList */
    dm = FindDrawableMapping(dcPtr->drawable);
    if (dm && dm->toplevel) {
        m = dm->toplevel;
        goto schedule;
    }

    /* Fallback via Tk window table */
    {
        Tk_Window tkwin = Tk_IdToWindow(
            TkGetDisplayList()->display, dcPtr->drawable);
        if (tkwin) {
            Tk_Window top = tkwin;
            while (top && !Tk_IsTopLevel(top))
                top = Tk_Parent(top);
            if (top)
                m = FindMappingByTk((TkWindow *)top);
        }
    }

schedule:
    if (m && m->glfwWindow) {
        m->texture.needs_texture_update = 1;
        TkWaylandScheduleDisplay(m);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContext --
 *
 *	Retrieve the shared cg context from the GLFW context structure.
 *
 * Results:
 *	Pointer to cg_ctx_t if initialized, NULL otherwise.
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
 *	Retrieve the shared cg context for text measurement operations.
 *	Makes the shared context current if needed.
 *
 * Results:
 *	Pointer to cg_ctx_t if available, NULL otherwise.
 *
 * Side effects:
 *	May make the shared GLFW context current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContextForMeasure(void)
{
    if (!glfwContext.initialized || shutdownInProgress) return NULL;
    if (!glfwGetCurrentContext() && glfwContext.mainWindow) {
	glfwMakeContextCurrent(glfwContext.mainWindow);
    }
    return glfwContext.cg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandProcessEvents --
 *
 *	Alias for TkGlfwProcessEvents for compatibility with other
 *	Wayland-related code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes GLFW event queue.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandProcessEvents(void)
{
    glfwPollEvents();
}

/*
 *----------------------------------------------------------------------
 *
 * Color conversion utilities
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwXColorToCG --
 *
 *	Convert an XColor structure to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t with normalized RGB values.
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
	c.r = c.g = c.b = 0.0;
	c.a = 1.0;
	return c;
    }
    /* XColor channels are 16-bit; >> 8 gives 0-255, then normalize. */
    c.r = (xcolor->red   >> 8) / 255.0;
    c.g = (xcolor->green >> 8) / 255.0;
    c.b = (xcolor->blue  >> 8) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToCG --
 *
 *	Convert an X pixel value (24-bit RGB) to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t with normalized RGB values.
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
 *	Apply GC state (color, line width, cap, join) to a cg context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the cg context's drawing state.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(struct cg_ctx_t *cg, GC gc)
{
    XGCValues     v;
    struct cg_color_t c;
    double        lw;

    if (!cg || !gc || shutdownInProgress) return;

    TkWaylandGetGCValues(gc,
	GCForeground | GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle,
	&v);

    c = TkGlfwPixelToCG(v.foreground);
    cg_set_source_rgba(cg, c.r, c.g, c.b, c.a);

	/* Use SRC_OVER operator for proper compositing of widget content over background. */
	cg_set_operator(cg, CG_OPERATOR_SRC_OVER);
  
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
 *	Tk platform entry point: initialize the Wayland/GLFW layer.
 *	Called during Tk initialization to set up platform-specific
 *	resources.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, Wayland notifier, menu system, tray support,
 *	notifications, printing, and accessibility.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(Tcl_Interp *interp)
{
    if (TkGlfwInitialize() != TCL_OK) return TCL_ERROR;
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
 *	Extract the application name from argv0 for use in the window
 *	manager and application metadata.
 *
 * Results:
 *	Appends the application name to the provided Tcl_DString.
 *
 * Side effects:
 *	Modifies the Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(Tcl_Interp *interp, Tcl_DString *namePtr)
{
    const char *p;
    const char *name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);

    if (!name || !*name) {
	name = "tk";
    } else {
	p = strrchr(name, '/');
	if (p) name = p + 1;
    }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	Display a warning message to the user. Under Wayland, warnings
 *	are written to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes warning message to stderr.
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
 * Window mapping management
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByGLFW --
 *
 *	Find a WindowMapping structure by its GLFWwindow pointer.
 *
 * Results:
 *	Pointer to WindowMapping if found, NULL otherwise.
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
	if (c->glfwWindow == w) return c;
	c = c->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByTk --
 *
 *	Find a WindowMapping structure by its TkWindow pointer.
 *
 * Results:
 *	Pointer to WindowMapping if found, NULL otherwise.
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
	if (c->tkWindow == w) return c;
	c = c->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByDrawable --
 *
 *	Find the WindowMapping that owns a drawable.
 *	Fast path: drawableMappingList.
 *	Slow path: Tk_IdToWindow → GetToplevelOfWidget.
 *	Pixmaps are not resolved here; callers that handle pixmaps check
 *	IsPixmap() before calling this.
 *
 * Results:
 *	Pointer to WindowMapping if found, NULL otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    DrawableMapping *dm;
    Tk_Window        tkwin, top;

    /* Fast path. */
    dm = drawableMappingList;
    while (dm) {
        if (dm->drawable == d) return dm->toplevel;
        dm = dm->next;
    }

    /* Slow path via Tk window table. */
    tkwin = Tk_IdToWindow(TkGetDisplayList()->display, d);
    if (!tkwin) return NULL;

    top = GetToplevelOfWidget(tkwin);
    if (!top) return NULL;

    return FindMappingByTk((TkWindow *)top);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetMappingList --
 *
 *	Return the head of the window mapping list.
 *
 * Results:
 *	Pointer to the first WindowMapping in the list.
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
 *	Add a WindowMapping to the global mapping list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Inserts the mapping at the head of the list.
 *
 *----------------------------------------------------------------------
 */

void
AddMapping(WindowMapping *m)
{
    if (!m) return;
    m->nextPtr = windowMappingList;
    windowMappingList = m;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveMapping --
 *
 *	Remove a WindowMapping from the global mapping list and free it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes and frees the mapping.
 *
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 *
 * CleanupAllMappings --
 *
 *	Destroy all window mappings and free all associated resources.
 *	Called during shutdown to clean up all windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all windows, surfaces, and textures; frees all mappings.
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
 *	Associate a toplevel Drawable ID with its WindowMapping.
 *	Used during TkGlfwCreateWindow.  For child widgets, use
 *	AddDrawableMapping (via TkGlfwRegisterChildDrawable) instead,
 *	which computes the correct (x,y) offset.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a DrawableMapping entry for the toplevel drawable.
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
    dm->width  = m->width;
    dm->height = m->height;
    dm->next   = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * GL/GLFW window accessor utilities
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetGLFWWindow --
 *
 *	Retrieve the GLFWwindow associated with a Tk window.
 *
 * Results:
 *	GLFWwindow pointer if found, NULL otherwise.
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
 *	Retrieve the Drawable ID associated with a GLFWwindow.
 *
 * Results:
 *	Drawable ID if found, 0 otherwise.
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
 *	Retrieve the GLFWwindow associated with a Drawable ID.
 *
 * Results:
 *	GLFWwindow pointer if found, NULL otherwise.
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
 *	Retrieve the TkWindow associated with a GLFWwindow.
 *
 * Results:
 *	TkWindow pointer if found, NULL otherwise.
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
 * ResolveSurface --
 *
 *	Resolve any drawable — integer window ID, raw TkWindow* pointer,
 *	or pixmap — to its backing cg_surface_t.
 *
 *	Handles three cases:
 *	  1. Pixmap: identified by IsPixmap() magic check.
 *	  2. Integer window ID: looked up via FindMappingByDrawable.
 *	  3. Raw TkWindow* (passed before winPtr->window is assigned):
 *	     validated via display pointer then walked to toplevel.
 *
 * Results:
 *	Pointer to cg_surface_t on success, NULL on failure.
 *	*outW and *outH are set to the surface dimensions when non-NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

struct cg_surface_t *
ResolveSurface(
    Drawable  d,
    int      *outW,
    int      *outH)
{
    WindowMapping       *m;
    TkWaylandPixmapImpl *px;
    TkWindow            *candidate, *top;
    Display             *ourDisp;

    if (!d) return NULL;

    /*  Pixmap. */
    if (IsPixmap(d)) {
        px = (TkWaylandPixmapImpl *)d;
        if (outW) *outW = px->width;
        if (outH) *outH = px->height;
        return px->surface;
    }

    /* Integer window ID via mapping list. */
    m = FindMappingByDrawable(d);
    if (m) {
        if (!m->surface) return NULL;
        if (outW) *outW = m->width;
        if (outH) *outH = m->height;
        return m->surface;
    }

    /* 
     * Raw TkWindow* — validate via display pointer then walk to
     * toplevel. 
     */
    ourDisp   = TkGetDisplayList()->display;
    candidate = (TkWindow *)d;
    if (candidate == NULL || candidate->display != ourDisp) {
        return NULL;
    }

    top = candidate;
    while (top && !Tk_IsTopLevel((Tk_Window)top)) {
        top = top->parentPtr;
    }
    if (!top) return NULL;

    m = FindMappingByTk(top);
    if (!m || !m->surface) return NULL;

    if (outW) *outW = m->width;
    if (outH) *outH = m->height;
    return m->surface;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
