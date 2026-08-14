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
 * #include "tkWaylandDebug.h"
 * ...
 *     DEBUG_LOG("The value of mystring is %s", mystring);
 *
 * This file must be included after defining DEBUG_CHANNEL and
 * DEBUG_LABEL.
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
#else
    #define DEBUG_LOG(fmt, ...) do {} while (0)
#endif
