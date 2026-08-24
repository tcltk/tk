/*
 *----------------------------------------------------------------------
 *
 * Debug logging.
 *
 * This header defines a macro which prints debug log messages to
 * DEBUG_CHANNEL with the label DEBUG_LABEL when DEBUG_CHANNEL and
 * DEBUG_LABEL are both defined.
 *
 * Usage:
 * #define DEBUG_CHANNEL stderr
 * #define DEBUG_LABEL "mylabel"
 * ...
 *     DEBUG_LOG("The value of mystring is %s", mystring);
 *
 * This file is included in tkWaylandInt.h.  The #define commands
 * above must come before the #include tkWaylandInt.h.
 *
 * The terminating semicolon is required.  This avoids confusing your
 * auto-indenter.
 * ----------------------------------------------------------------------
 */

#if defined(DEBUG_CHANNEL) && defined(DEBUG_LABEL)
    #define DEBUG_LOG(fmt, ...)    \
        do {fprintf(DEBUG_CHANNEL, \
	    ("[" DEBUG_LABEL "] " fmt "\n"), ##__VA_ARGS__); \
	    fflush(DEBUG_CHANNEL);  \
	} while (0)

    #define GL_DEBUG_LOG(fmt, ...)  \
        do {GLenum error = glGetError();                         \
	    if (error != GL_NO_ERROR) {                          \
	        fprintf(DEBUG_CHANNEL,			         \
	        ("[" DEBUG_LABEL "] " fmt " GLError: 0x%04x\n"), \
		    ##__VA_ARGS__, error);			 \
	        fflush(DEBUG_CHANNEL);                           \
	    }                                                    \
         } while (0)
#else
    #define GL_DEBUG_LOG(fmt, ...) do {} while (0)
    #define DEBUG_LOG(fmt, ...) do {} while (0)
#endif
