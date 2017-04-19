/*
 * tkTextDisp.c --
 *
 *	This module provides facilities to display text widgets. It is the
 *	only place where information is kept about the screen layout of text
 *	widgets. (Well, strictly, each TkTextLine and B-tree node caches its
 *	last observed pixel height, but that information originates here).
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if defined(_MSC_VER ) && _MSC_VER < 1500
/* suppress wrong warnings to support ancient compilers */
#pragma warning (disable : 4305)
#endif

#include "tkText.h"
#include "tkTextTagSet.h"
#include "tkRangeList.h"
#include "tkAlloc.h"
#include "tkInt.h"

#ifdef _WIN32
# include "tkWinInt.h"
#elif defined(__CYGWIN__)
# include "tkUnixInt.h"
#endif

#ifdef MAC_OSX_TK
# include "tkMacOSXInt.h"
/* Version 8.5 has forgotten to define this constant. */
# ifndef TK_DO_NOT_DRAW
#  define TK_DO_NOT_DRAW 0x80
# endif
#endif

#include <stdlib.h>
#include <assert.h>

#ifndef MIN
# define MIN(a,b) (a < b ? a : b)
#endif
#ifndef MAX
# define MAX(a,b) (a < b ? b : a)
#endif

#ifdef NDEBUG
# define DEBUG(expr)
#else
# define DEBUG(expr) expr
#endif

/*
 * "Calculations of line pixel heights and the size of the vertical
 * scrollbar."
 *
 * Given that tag, font and elide changes can happen to large numbers of
 * diverse chunks in a text widget containing megabytes of text, it is not
 * possible to recalculate all affected height information immediately any
 * such change takes place and maintain a responsive user-experience. Yet, for
 * an accurate vertical scrollbar to be drawn, we must know the total number
 * of vertical pixels shown on display versus the number available to be
 * displayed.
 *
 * The way the text widget solves this problem is by maintaining cached line
 * pixel heights (in the BTree for each logical line), and having asynchronous
 * timer callbacks (i) to iterate through the logical lines recalculating
 * their heights, and (ii) to recalculate the vertical scrollbar's position
 * and size.
 *
 * Typically this works well but there are some situations where the overall
 * functional design of this file causes some problems. These problems can
 * only arise because the calculations used to display lines on screen are not
 * connected to those in the iterating-line- recalculation-process.
 *
 * The reason for this disconnect is that the display calculations operate in
 * display lines, and the iteration and cache operates in logical lines.
 * Given that the display calculations both need not contain complete logical
 * lines (at top or bottom of display), and that they do not actually keep
 * track of logical lines (for simplicity of code and historical design), this
 * means a line may be known and drawn with a different pixel height to that
 * which is cached in the BTree, and this might cause some temporary
 * undesirable mismatch between display and the vertical scrollbar.
 *
 * All such mismatches should be temporary, however, since the asynchronous
 * height calculations will always catch up eventually.
 *
 * For further details see the comments before and within the following
 * functions below: LayoutDLine, AsyncUpdateLineMetrics, GetYView,
 * GetYPixelCount, UpdateOneLine, UpdateLineMetrics.
 *
 * For details of the way in which the BTree keeps track of pixel heights, see
 * tkTextBTree.c. Basically the BTree maintains two pieces of information: the
 * logical line indices and the pixel height cache.
 */

/*
 * TK_LAYOUT_WITH_BASE_CHUNKS:
 *
 *	With this macro set, collect all char chunks that have no holes
 *	between them, that are on the same line and use the same font and font
 *	size. Allocate the chars of all these chunks, the so-called "stretch",
 *	in a DString in the first chunk, the so-called "base chunk". Use the
 *	base chunk string for measuring and drawing, so that these actions are
 *	always performed with maximum context.
 *
 *	This is necessary for text rendering engines that provide ligatures
 *	and sub-pixel layout, like ATSU on Mac. If we don't do this, the
 *	measuring will change all the time, leading to an ugly "tremble and
 *	shiver" effect. This is because of the continuous splitting and
 *	re-merging of chunks that goes on in a text widget, when the cursor or
 *	the selection move.
 *
 * Side effects:
 *
 *	Memory management changes. Instead of attaching the character data to
 *	the clientData structures of the char chunks, an additional DString is
 *	used. The collection process will even lead to resizing this DString
 *	for large stretches (> TCL_DSTRING_STATIC_SIZE == 200). We could
 *	reduce the overall memory footprint by copying the result to a plain
 *	char array after the line breaking process, but that would complicate
 *	the code and make performance even worse speedwise.
 */

/*
 * The following macro is used to compare two floating-point numbers to within
 * a certain degree of scale. Direct comparison fails on processors where the
 * processor and memory representations of FP numbers of a particular
 * precision is different (e.g. Intel)
 */

#define FP_EQUAL_SCALE(double1, double2, scaleFactor) \
    (fabs((double1) - (double2))*((scaleFactor) + 1.0) < 0.3)

/*
 * Macro to make debugging/testing logging a little easier.
 */

#define LOG(toVar,what) \
    Tcl_SetVar2(textPtr->interp, toVar, NULL, what, TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT)

/*
 * Speed up if the text content only contains monospaced line heights, and line wrapping
 * is disabled.
 *
 * But this speed-up trial seems not to have any effect which is worth the effort,
 * especially because it can be used only if no line wrapping will be done.
 */

#define SPEEDUP_MONOSPACED_LINE_HEIGHTS 0

/*
 * Structure for line break information:
 */

typedef struct TkTextBreakInfo {
    uint32_t refCount;	/* Reference counter, destroy if this counter is going to zero. */
    char *brks;		/* Array of break info, has exactly the char length of the logical line,
    			 * each cell is one of LINEBREAK_NOBREAK, LINEBREAK_ALLOWBREAK,
			 * LINEBREAK_MUSTBREAK, or LINEBREAK_INSIDEACHAR. */
    struct TkTextBreakInfo *nextPtr;
    			/* Pointer to break information of successor line. Will only be used
    			 * when caching the break information while redrawing tags. */
} TkTextBreakInfo;

typedef struct TkTextDispLine DLine;

/*
 * Flag bits for DLine structures:
 *
 * HAS_3D_BORDER -		Non-zero means that at least one of the chunks in this line has
 *				a 3D border, so itpotentially interacts with 3D borders in
 *				neighboring lines (see DisplayLineBackground).
 * NEW_LAYOUT -			Non-zero means that the line has been re-layed out since the last
 *				time the display was updated.
 * TOP_LINE -			Non-zero means that this was the top line in in the window the last
 *				time that the window was laid out. This is important because a line
 *				may be displayed differently if its at the top or bottom than if
 *				it's in the middle (e.g. beveled edges aren't displayed for middle
 *				lines if the adjacent line has a similar background).
 * BOTTOM_LINE -		Non-zero means that this was the bottom line in the window the last
 *				time that the window was laid out.
 * OLD_Y_INVALID -		The value of oldY in the structure is not valid or useful and should
 *				not be examined. 'oldY' is only useful when the DLine is currently
 *				displayed at a different position and we wish to re-display it via
 *				scrolling, so this means the DLine needs redrawing.
 * PARAGRAPH_START -		We are on the first line of a paragraph (used to choose between
 *				lmargin1, lmargin2).
 * CURSOR -			This display line is displaying the cursor.
 */

#define HAS_3D_BORDER	(1 << 0)
#define NEW_LAYOUT	(1 << 1)
#define TOP_LINE	(1 << 2)
#define BOTTOM_LINE	(1 << 3)
#define OLD_Y_INVALID	(1 << 4)
#define PARAGRAPH_START	(1 << 5)
#define DELETED		(1 << 6) /* for debugging */
#define LINKED		(1 << 7) /* for debugging */
#define CACHED		(1 << 8) /* for debugging */

/*
 * The following structure describes how to display a range of characters.
 * The information is generated by scanning all of the tags associated with
 * the characters and combining that with default information for the overall
 * widget. These structures form the hash keys for dInfoPtr->styleTable.
 */

typedef struct StyleValues {
    Tk_3DBorder border;		/* Used for drawing background under text.
				 * NULL means use widget background. */
    Pixmap bgStipple;		/* Stipple bitmap for background. None means
				 * draw solid. */
    XColor *fgColor;		/* Foreground color for text. */
    XColor *eolColor;		/* Foreground color for end of line symbol, can be NULL. */
    XColor *eotColor;		/* Foreground color for end of text symbol, can be NULL. */
    XColor *hyphenColor;	/* Foreground color for soft hyphens, can be NULL. */
    Tk_Font tkfont;		/* Font for displaying text. */
    Pixmap fgStipple;		/* Stipple bitmap for text and other foreground stuff. None means
    				 * draw solid.*/
    TkTextTabArray *tabArrayPtr;/* Locations and types of tab stops (may be NULL). */
    Tk_3DBorder lMarginColor;	/* Color of left margins (1 and 2). */
    Tk_3DBorder rMarginColor;	/* Color of right margin. */
    XColor *overstrikeColor;	/* Foreground color for overstrike through text. */
    XColor *underlineColor;	/* Foreground color for underline underneath text. */
    char const *lang;		/* Language support (may be NULL). */
    int hyphenRules;		/* Hyphenation rules for spelling changes. */
    int32_t borderWidth;	/* Width of 3-D border for background. */
    int32_t lMargin1;		/* Left margin, in pixels, for first display line of each text line. */
    int32_t lMargin2;		/* Left margin, in pixels, for second and later display lines of
    				 * each text line. */
    int32_t offset;		/* Offset in pixels of baseline, relative to baseline of line. */
    int32_t rMargin;		/* Right margin, in pixels. */
    int32_t spacing1;		/* Spacing above first dline in text line. */
    int32_t spacing2;		/* Spacing between lines of dline. */
    int32_t spacing3;		/* Spacing below last dline in text line. */
    uint32_t wrapMode:3;	/* How to handle wrap-around for this tag. One of TEXT_WRAPMODE_CHAR,
				 * TEXT_WRAPMODE_NONE, TEXT_WRAPMODE_WORD, or TEXT_WRAPMODE_CODEPOINT.*/
    uint32_t tabStyle:3;	/* One of TABULAR or WORDPROCESSOR. */
    uint32_t justify:3;		/* Justification style for text. */
    uint32_t relief:3;		/* 3-D relief for background. */
    uint32_t indentBg:1;	/* Background will be indented accordingly to the -lmargin1,
    				 * and -lmargin2 options. */
    uint32_t overstrike:1;	/* Non-zero means draw overstrike through text. */
    uint32_t underline:1;	/* Non-zero means draw underline underneath text. */
    uint32_t elide:1;		/* Zero means draw text, otherwise not. */
} StyleValues;

/*
 * The following structure extends the StyleValues structure above with
 * graphics contexts used to actually draw the characters. The entries in
 * dInfoPtr->styleTable point to structures of this type.
 */

typedef struct TextStyle {
    StyleValues *sValuePtr;	/* Raw information from which GCs were derived. */
    Tcl_HashEntry *hPtr;	/* Pointer to entry in styleTable. Used to delete entry. */
    GC bgGC;			/* Graphics context for background. None means use widget background. */
    GC fgGC;			/* Graphics context for foreground. */
    GC ulGC;			/* Graphics context for underline. */
    GC ovGC;			/* Graphics context for overstrike. */
    GC eolGC;			/* Graphics context for end of line symbol. */
    GC eotGC;			/* Graphics context for end of text symbol. */
    GC hyphenGC;		/* Graphics context for soft hyphen character. */
    uint32_t refCount;		/* Number of times this structure is referenced in Chunks. */
} TextStyle;

/*
 * In TkTextDispChunk structures for character segments, the clientData field
 * points to one of the following structures:
 */

typedef struct CharInfo {
    union {
	const char *chars;	/* UTF characters to display. Actually points into the baseChars of
				 * it points points into the baseChars of the base chunk. */
	struct CharInfo *next;	/* Pointer to next free info struct. */
    } u;
    int32_t numBytes;		/* Number of bytes that belong to this chunk. */
    int32_t baseOffset;		/* Starting offset in baseChars of base chunk; always zero if
    				 * context drawing is not used. */
    TkTextSegment *segPtr;	/* Pointer to char segment containing the chars. */
} CharInfo;

typedef struct PixelPos {
    int32_t xFirst, xLast; 	/* Horizontal pixel position. */
    int32_t yFirst, yLast;	/* Vertical pixel position. */
} PixelPos;

/*
 * Overall display information for a text widget:
 */

typedef struct TextDInfo {
    Tcl_HashTable styleTable;	/* Hash table that maps from StyleValues to TextStyles for this
    				 * widget. */
    DLine *dLinePtr;		/* First in list of all display lines for this widget, in order
    				 * from top to bottom. */
    DLine *lastDLinePtr;	/* Pointer to last display line in this widget. */
    TextStyle *defaultStyle;	/* Default style. */
    GC copyGC;			/* Graphics context for copying from off-screen pixmaps onto screen. */
    GC scrollGC;		/* Graphics context for copying from one place in the window to
    				 * another (scrolling): differs from copyGC in that we need to get
				 * GraphicsExpose events. */
    GC insertFgGC;		/* Graphics context for drawing text "behind" the insert cursor. */
    double xScrollFirst, xScrollLast;
				/* Most recent values reported to horizontal scrollbar; used to
				 * eliminate unnecessary reports. */
    double yScrollFirst, yScrollLast;
				/* Most recent values reported to vertical scrollbar; used to
				 * eliminate unnecessary reports. */
    uint32_t firstLineNo;	/* Line number of first line in text widget, needed for re-layout. */
    uint32_t lastLineNo;	/* Line number of last line in text widget, needed for re-layout. */
    int32_t topPixelOffset;	/* Identifies first pixel in top display line to display in window. */
    int32_t newTopPixelOffset;	/* Desired first pixel in top display line to display in window. */
    int32_t x;			/* First x-coordinate that may be used for actually displaying line
    				 * information. Leaves space for border, etc. */
    int32_t y;			/* First y-coordinate that may be used for actually displaying line
    				 * information. Leaves space for border, etc. */
    int32_t maxX;		/* First x-coordinate to right of available space for displaying
    				 * lines. */
    int32_t maxY;		/* First y-coordinate below available space for displaying lines. */
    int32_t topOfEof;		/* Top-most pixel (lowest y-value) that has been drawn in the
    				 * appropriate fashion for the portion of the window after the last
				 * line of the text. This field is used to figure out when to redraw
				 * part or all of the eof field. */
    int32_t curYPixelOffset;	/* Y offset of the current view. */
    TkTextSegment *endOfLineSegPtr;
    				/* Holds the end of line symbol (option -showendOfline). */
    TkTextSegment *endOfTextSegPtr;
    				/* Holds the end of text symbol (option -showendOftext). */

    /*
     * Cache for single lines:
     */

    DLine *cachedDLinePtr;	/* We are caching some computed display lines for speed enhancements. */
    DLine *lastCachedDLinePtr;	/* Pointer to last cached display line. */
    unsigned numCachedLines;	/* Number of cached display lines. */
    DLine *lastMetricDLinePtr;	/* We are caching the last computed display line in metric computation,
    				 * one reason is speed up, but the main reason is to avoid that some
				 * cached data (i.e. brks) will be released to early. */

    /*
     * Storage for saved display lines. These lines has been computed for line metric information,
     * and will be used for displaying afterwards.
     */

    DLine *savedDLinePtr;	/* First in list of saved display lines, in order from top to bottom. */
    DLine *lastSavedDLinePtr;/* Pointer to last saved display line. */
    int32_t savedDisplayLinesHeight;
    				/* Sum of saved display line heights. */

    /*
     * Additional buffers:
     */

    char *strBuffer;		/* We need a string buffer for the line break algorithm. */
    unsigned strBufferSize;	/* Size of the line break string buffer. */

    /*
     * Information used for scrolling:
     */

    int32_t newXPixelOffset;	/* Desired x scroll position, measured as the number of pixels
    				 * off-screen to the left for a line with no left margin. */
    int32_t curXPixelOffset;	/* Actual x scroll position, measured as the number of pixels
    				 * off-screen to the left. */
    uint32_t maxLength;		/* Length in pixels of longest line that's visible in window
    				 * (length may exceed window size). If there's no wrapping, this
				 * will be zero. */
    PixelPos curPixelPos;	/* Most recent pixel position, used for the "watch" command. */
    PixelPos prevPixelPos;	/* Previous pixel position, used for the "watch" command. */

    /*
     * The following information is used to implement scanning:
     */

    int32_t scanMarkXPixel;	/* Pixel index of left edge of the window when the scan started. */
    int32_t scanMarkX;		/* X-position of mouse at time scan started. */
    int32_t scanTotalYScroll;	/* Total scrolling (in screen pixels) that has occurred since
    				 * scanMarkY was set. */
    int32_t scanMarkY;		/* Y-position of mouse at time scan started. */

    /*
     * The following is caching the current chunk information:
     */

    TkTextIndex currChunkIndex;	/* Index position of current chunk. */
    TkTextDispChunk *currChunkPtr;
    				/* This is the chunk currently hovered by mouse. */
    DLine *currDLinePtr;	/* The DLine which contains the current chunk. */

    /*
     * Cache current y-view position:
     */

    int32_t topLineNo;
    int32_t topByteIndex;

    /*
     * Pools for lines, chunks, char infos, and break infos:
     */

    DLine *dLinePoolPtr;	/* Pointer to first free display line. */
    TkTextDispChunk *chunkPoolPtr;
    				/* Pointer to first free chunk. */
    struct TkTextDispChunkSection *sectionPoolPtr;
    				/* Pointer to first free section. */
    CharInfo *charInfoPoolPtr;	/* Pointer to first free char info. */
    unsigned chunkCounter;	/* Used for the unique chunk ID. */

    /*
     * Miscellaneous information:
     */

    bool dLinesInvalidated;	/* This value is set to true whenever something happens that
    				 * invalidates information in DLine structures; if a redisplay
				 * is in progress, it will see this and abort the redisplay. This
				 * is needed because, for example, an embedded window could change
				 * its size when it is first displayed, invalidating the DLine that
				 * is currently being displayed. If redisplay continues, it will
				 * use freed memory and could dump core. */
    bool pendingUpdateLineMetricsFinished;
    				/* Did we add RunUpdateLineMetricsFinished to the idle loop? */
    int32_t flags;		/* Various flag values: see below for definitions. */
    uint32_t countImages;	/* Number of displayed images (currently unused except if
    				 * SPEEDUP_MONOSPACED_LINE_HEIGHTS is set). */
    uint32_t countWindows;	/* Number of displayed windows. */
    bool insideLineMetricUpdate;/* Line metric update is currently in progress. */

    /*
     * Information used to handle the asynchronous updating of the y-scrollbar
     * and the vertical height calculations:
     */

    int lineHeight;		/* TkTextRelayoutWindow is using this value: the line height of
    				 * monospaced lines, is zero of the line heights are not monospaced
				 * in last call of TkTextRelayoutWindow. */
    uint32_t lineMetricUpdateEpoch;
    				/* Stores a number which is incremented each time the text widget
				 * changes in a significant way (e.g. resizing or geometry-influencing
				 * tag changes). */
    uint32_t lineMetricUpdateCounter;
    				/* Count updates of line metric information. */
    TkRangeList *lineMetricUpdateRanges;
    				/* Stores the range of line numbers which are not yet up-to-date. */
    TkTextIndex metricIndex;	/* If the current metric update line wraps into very many display
    				 * lines, then this is used to keep track of what index we've got
				 * to so far... */
    Tcl_TimerToken lineUpdateTimer;
				/* A token pointing to the current line metric update callback. */
    Tcl_TimerToken scrollbarTimer;
				/* A token pointing to the current scrollbar update callback. */
    Tcl_TimerToken repickTimer;
				/* A token pointing to the current repick callback. */
} TextDInfo;

typedef struct TkTextDispChunkSection {
    struct TkTextDispChunkSection *nextPtr;
    				/* Next section in chain of display sections. */
    TkTextDispChunk *chunkPtr;	/* First display chunk in this section. */
    uint32_t numBytes;		/* Number of bytes in this section. */
} TkTextDispChunkSection;

/*
 * Flag values for TextDInfo structures:
 *
 * DINFO_OUT_OF_DATE:	Means that the DLine structures for this window are partially or
 *			completely out of date and need to be recomputed.
 *
 * REDRAW_PENDING:	Means that a when-idle handler has been scheduled to update the display.
 *
 * REDRAW_BORDERS:	Means window border or pad area has potentially been damaged and must
 *			be redrawn.
 *
 * ASYNC_UPDATE:	Means that the asynchronous pixel-height calculation is still working.
 *
 * ASYNC_PENDING:	Means that the asynchronous pixel-height calculation is pending until
 *			the display update (DisplayText) has been finished.
 *
 * REPICK_NEEDED:	Means that the widget has been modified in a way that could change
 *			the current character (a different character might be under the mouse
 *			cursor now). Need to recompute the current character before the next
 *			redisplay.
 */

#define DINFO_OUT_OF_DATE	(1 << 0)
#define REDRAW_PENDING		(1 << 1)
#define REDRAW_BORDERS		(1 << 2)
#define ASYNC_UPDATE		(1 << 3)
#define ASYNC_PENDING		(1 << 4)
#define REPICK_NEEDED		(1 << 5)

typedef struct LayoutData {
    TkText *textPtr;
    DLine *dlPtr;		/* Current display line. */
    TkTextDispChunk *chunkPtr;	/* Start of chunk chain. */
    TkTextDispChunk *tabChunkPtr;
				/* Pointer to the chunk containing the previous tab stop. */
    TkTextDispChunk *firstChunkPtr;
				/* Pointer to the first chunk. */
    TkTextDispChunk *lastChunkPtr;
				/* Pointer to the current chunk. */
    TkTextDispChunk *firstCharChunkPtr;
				/* Pointer to the first char/window/image chunk in chain. */
    TkTextDispChunk *lastCharChunkPtr;
				/* Pointer to the last char/window/image chunk in chain. */
    TkTextDispChunk *breakChunkPtr;
				/* Chunk containing best word break point, if any. */
    TkTextDispChunk *cursorChunkPtr;
				/* Pointer to the insert cursor chunk. */
    TkTextLine *logicalLinePtr;	/* Pointer to the logical line. */
    TkTextBreakInfo *breakInfo;	/* Line break information of logical line. */
    const char *brks;		/* Buffer for line break information (for TEXT_WRAPMODE_CODEPOINT). */
    TkTextIndex index;		/* Current index. */
    unsigned countChunks;	/* Number of chunks in current display line. */
    unsigned numBytesSoFar;	/* The number of processed bytes (so far). */
    unsigned byteOffset;	/* The byte offset to start of logical line. */
    unsigned dispLineOffset;	/* The byte offset to start of display line. */
    int increaseNumBytes;	/* Increase number of consumed bytes to realize spelling changes. */
    unsigned decreaseNumBytes;	/* Decrease number of displayable bytes to realize spelling changes. */
    unsigned displayLineNo;	/* Current display line number. */
    int rMargin;		/* Right margin width for line. */
    int hyphenRule;		/* Hyphenation rule applied to last char chunk (only in hyphenation
    				 * has been applied). */
    TkTextTabArray *tabArrayPtr;/* Tab stops for line; taken from style for the first character
    				 * on line. */
    int tabStyle;		/* One of TABULAR or WORDPROCESSOR. */
    int tabSize;		/* Number of pixels consumed by current tab stop. */
    int tabIndex;		/* Index of the current tab stop. */
    unsigned tabWidth;		/* Default tab width of this widget. */
    unsigned numSpaces;		/* Number of expandable space (needed for full justification). */
    TkTextJustify justify;	/* How to justify line: taken from style for the first character
    				 * in this display line. */
    TkWrapMode wrapMode;	/* Wrap mode to use for this chunk. */
    int maxX;			/* Maximal x coord in current line. */
    int width;			/* Maximal x coord in widget. */
    int x;			/* Current x coord. */
    bool paragraphStart;	/* 'true' means that we are on the first line of a paragraph
    				 * (used to choose between lmargin1, lmargin2). */
    bool skipSpaces;		/* 'true' means that we have to gobble spaces at start of next
    				 * segment. */
    bool trimSpaces;		/* 'true' iff space mode is TEXT_SPACEMODE_TRIM. */

#if TK_LAYOUT_WITH_BASE_CHUNKS
    /*
     * Support for context drawing.
     */

    TkTextDispChunk *baseChunkPtr;
    				/* The chunk which contains the actual context data. */
#endif
} LayoutData;

typedef struct DisplayInfo {
    int byteOffset;		/* Byte offset to start of display line (subtract this offset to
    				 * get the index of display line start). */
    int nextByteOffset;		/* Byte offset to start of next display line (add this offset to
    				 * get the index of next display line start). */
    unsigned displayLineNo;	/* Number of display line. */
    unsigned numDispLines;	/* Total number of display lines belonging to corresponding logical
    				 * line (so far). */
    int pixels;			/* Total height of logical line (so far). */
    bool isComplete;		/* The display line metric is complete for this logical line? */
    const TkTextDispLineEntry *entry;
    				/* Pointer to entry in display pixel info for displayLineNo. Note
				 * that the predecessing entries can be accessed, but not the successing
				 * entries. */
    DLine *dLinePtr;		/* Cached display lines, produced while ComputeDisplayLineInfo is
    				 * computing the line metrics. */
    DLine *lastDLinePtr;	/* Pointer to last cached display line. */
    unsigned numCachedLines;	/* Number of cached lines. */
    unsigned heightOfCachedLines;
    				/* Sum of cached display line heights. */
    TkTextIndex index;		/* Index where the computation has finished. */
    TkTextLine *linePtr;	/* Logical line, where computation has started. */
    const TkTextPixelInfo *pixelInfo;
    				/* Pixel information of logical line. */
    TkTextBreakInfo *lineBreakInfo;
    				/* We have to cache the line break information (for
    				 * TEXT_WRAPMODE_CODEPOINT), to avoid repeated computations when
				 * scrolling. */

    /*
     * This attribute is private.
     */

    TkTextDispLineEntry entryBuffer[2];
    				/* This buffer will be used if the logical line has no entries
				 * (single display line). */
} DisplayInfo;

/*
 * Action values for FreeDLines:
 *
 * DLINE_UNLINK:	Free, unlink from current display, and set 'dLinesInvalidated'.
 *
 * DLINE_UNLINK_KEEP_BRKS:
 *			Same as DLINE_UNLINK, but do not destroy break info (except if
 *			now outside of peer).
 *
 * DLINE_FREE_TEMP:	Free, but don't unlink, and also don't set 'dLinesInvalidated'.
 *
 * DLINE_CACHE:		Don't free, don't unlink, cache this line, and don't set 'dLinesInvalidated'.
 *
 * DLINE_METRIC:	Don't free, don't unlink, cache this line temporarily, and don't set
 *			'dLinesInvalidated'.
 *
 * DLINE_SAVE:		Don't free, unlink, and save this line for displaying later.
 */

typedef enum {
    DLINE_UNLINK, DLINE_UNLINK_KEEP_BRKS, DLINE_FREE_TEMP, DLINE_CACHE, DLINE_METRIC, DLINE_SAVE
} FreeDLineAction;

/*
 * Maximal number of cached display lines.
 */

#define MAX_CACHED_DISPLAY_LINES 8

/*
 * Macro that determines how much space to allocate for TkTextDispLineInfo:
 */

#define TEXT_DISPLINEINFO_SIZE(numDispLines) (Tk_Offset(TkTextDispLineInfo, entry) + \
	(numDispLines)*sizeof(((TkTextDispLineInfo *) 0)->entry[0]))

/*
 * We will also mark logical lines with current line metric epoch even if the computation
 * has been done only partial. In this case we add a special bit to mark it as partially
 * computed.
 */

#define EPOCH_MASK		0x7fffffff
#define PARTIAL_COMPUTED_BIT	0x80000000

/*
 * Result values returned by TextGetScrollInfoObj:
 */

typedef enum {
    SCROLL_MOVETO,
    SCROLL_PAGES,
    SCROLL_UNITS,
    SCROLL_ERROR,
    SCROLL_PIXELS
} ScrollMethod;

/*
 * Threshold type for ComputeMissingMetric:
 */

typedef enum {
    THRESHOLD_BYTE_OFFSET,	/* compute until byte offset has been reached */
    THRESHOLD_LINE_OFFSET,	/* compute until display line offset has been reached */
    THRESHOLD_PIXEL_DISTANCE	/* compute until pixel distance has been reached */
} Threshold;

/*
 * We don't want less than 10 chunks per display section.
 */
#define MIN_CHUNKS_PER_SECTION 10u

/*
 * We don't want more than 20 sections per display line.
 */
#define MAX_SECTIONS_PER_LINE 20

/*
 * Forward declarations for functions defined later in this file:
 */

static void		AdjustForTab(LayoutData *data);
static void		ComputeSizeOfTab(LayoutData *data);
static void		ElideBboxProc(TkText *textPtr, TkTextDispChunk *chunkPtr, int index, int y,
			    int lineHeight, int baseline, int *xPtr, int *yPtr, int *widthPtr,
			    int *heightPtr);
static int		ElideMeasureProc(TkTextDispChunk *chunkPtr, int x);
static void		DisplayDLine(TkText *textPtr, DLine *dlPtr, DLine *prevPtr, Pixmap pixmap);
static void		DisplayLineBackground(TkText *textPtr, DLine *dlPtr, DLine *prevPtr,
			    Pixmap pixmap);
static void		DisplayText(ClientData clientData);
static DLine *		FindCachedDLine(TkText *textPtr, const TkTextIndex *indexPtr);
static DLine *		FindDLine(TkText *textPtr, DLine *dlPtr, const TkTextIndex *indexPtr);
static DLine *		FreeDLines(TkText *textPtr, DLine *firstPtr, DLine *lastPtr,
			    FreeDLineAction action);
static void		FreeStyle(TkText *textPtr, TextStyle *stylePtr);
static TextStyle *	GetStyle(TkText *textPtr, TkTextSegment *segPtr);
static void		UpdateDefaultStyle(TkText *textPtr);
static bool		GetBbox(TkText *textPtr, const DLine *dlPtr, const TkTextIndex *indexPtr,
			    int *xPtr, int *yPtr, int *widthPtr, int *heightPtr, bool *isLastCharInLine,
			    Tcl_UniChar *thisChar);
static void		GetXView(Tcl_Interp *interp, TkText *textPtr, bool report);
static void		GetYView(Tcl_Interp *interp, TkText *textPtr, bool report);
static unsigned		GetYPixelCount(TkText *textPtr, DLine *dlPtr);
static DLine *		LayoutDLine(const TkTextIndex *indexPtr, unsigned displayLineNo);
static int		UpdateOneLine(TkText *textPtr, TkTextLine *linePtr, TkTextIndex *indexPtr,
			    unsigned maxDispLines);
static bool		MeasureUp(TkText *textPtr, const TkTextIndex *srcPtr, int distance,
			    TkTextIndex *dstPtr, int32_t *overlap);
static bool		MeasureDown(TkText *textPtr, TkTextIndex *srcPtr, int distance,
			    int32_t *overlap, bool saveDisplayLines);
static int		NextTabStop(unsigned tabWidth, int x, int tabOrigin);
static void		UpdateDisplayInfo(TkText *textPtr);
static void		YScrollByLines(TkText *textPtr, int offset);
static void		YScrollByPixels(TkText *textPtr, int offset);
static void		TextInvalidateRegion(TkText *textPtr, TkRegion region);
static void		TextInvalidateLineMetrics(TkText *textPtr, TkTextLine *linePtr,
			    unsigned lineCount, TkTextInvalidateAction action);
static int		CalculateDisplayLineHeight(TkText *textPtr, const TkTextIndex *indexPtr,
			    unsigned *byteCountPtr);
static TkTextDispChunk * DLineChunkOfX(TkText *textPtr, DLine *dlPtr, int x, TkTextIndex *indexPtr,
			    bool *nearby);
static void		DLineIndexOfX(TkText *textPtr, TkTextDispChunk *chunkPtr, int x,
			    TkTextIndex *indexPtr);
static int		DLineXOfIndex(TkText *textPtr, DLine *dlPtr, unsigned byteIndex);
static ScrollMethod	TextGetScrollInfoObj(Tcl_Interp *interp, TkText *textPtr, int objc,
			    Tcl_Obj *const objv[], double *dblPtr, int *intPtr);
static void		InvokeAsyncUpdateLineMetrics(TkText *textPtr);
static void		InvokeAsyncUpdateYScrollbar(TkText *textPtr);
static void		AsyncUpdateYScrollbar(ClientData clientData);
static void		AsyncUpdateLineMetrics(ClientData clientData);
static void		UpdateLineMetrics(TkText *textPtr, unsigned doThisMuch);
static bool		TestIfLinesUpToDate(const TkTextIndex *indexPtr);
static void		SaveDisplayLines(TkText *textPtr, DisplayInfo *info, bool append);
static TkTextLine *	ComputeDisplayLineInfo(TkText *textPtr, const TkTextIndex *indexPtr,
			    DisplayInfo *info);
static void		ComputeMissingMetric(TkText *textPtr, DisplayInfo *info,
			    Threshold threshold, int offset);
static unsigned		GetPixelsTo(TkText *textPtr, const TkTextIndex *indexPtr,
			    bool inclusiveLastLine, DisplayInfo *info);
static unsigned		FindDisplayLineOffset(TkText *textPtr, TkTextLine *linePtr, int32_t *distance);
static void		FindDisplayLineStartEnd(TkText *textPtr, TkTextIndex *indexPtr, bool end,
			    int cacheType);
static void		CheckIfLineMetricIsUpToDate(TkText *textPtr);
static void		RunUpdateLineMetricsFinished(ClientData clientData);
static void		CheckLineMetricConsistency(const TkText *textPtr);
static int		ComputeBreakIndex(TkText *textPtr, const TkTextDispChunk *chunkPtr,
			    TkTextSegment *segPtr, int byteOffset, TkWrapMode wrapMode,
			    TkTextSpaceMode spaceMode);
static int		CharChunkMeasureChars(TkTextDispChunk *chunkPtr, const char *chars, int charsLen,
			    int start, int end, int startX, int maxX, int flags, int *nextXPtr);
static void		CharDisplayProc(TkText *textPtr, TkTextDispChunk *chunkPtr, int x, int y,
			    int height, int baseline, Display *display, Drawable dst, int screenY);
static void		CharUndisplayProc(TkText *textPtr, TkTextDispChunk *chunkPtr);
static void		HyphenUndisplayProc(TkText *textPtr, TkTextDispChunk *chunkPtr);
static void		DisplayChars(TkText *textPtr, TkTextDispChunk *chunkPtr, int x, int y,
			    int baseline, Display *display, Drawable dst);
static int		CharMeasureProc(TkTextDispChunk *chunkPtr, int x);
static void		CharBboxProc(TkText *textPtr, TkTextDispChunk *chunkPtr, int index, int y,
			    int lineHeight, int baseline, int *xPtr, int *yPtr, int *widthPtr,
			    int *heightPtr);
static int		MeasureChars(Tk_Font tkfont, const char *source, int maxBytes, int rangeStart,
			    int rangeLength, int startX, int maxX, int flags, int *nextXPtr);
static CharInfo *	AllocCharInfo(TkText *textPtr);
static void		FreeCharInfo(TkText *textPtr, CharInfo *ciPtr);
#if TK_LAYOUT_WITH_BASE_CHUNKS
static bool		IsSameFGStyle(TextStyle *style1, TextStyle *style2);
#endif /* TK_LAYOUT_WITH_BASE_CHUNKS */

static const TkTextDispChunkProcs layoutCharProcs = {
    TEXT_DISP_CHAR,		/* type */
    CharDisplayProc,		/* displayProc */
    CharUndisplayProc,		/* undisplayProc */
    CharMeasureProc,		/* measureProc */
    CharBboxProc,		/* bboxProc */
};

#define CHAR_CHUNK_GET_SEGMENT(chunkPtr) (((const CharInfo *) chunkPtr->clientData)->segPtr)

static const TkTextDispChunkProcs layoutHyphenProcs = {
    TEXT_DISP_HYPHEN,		/* type */
    CharDisplayProc,		/* displayProc */
    HyphenUndisplayProc,	/* undisplayProc */
    CharMeasureProc,		/* measureProc */
    CharBboxProc,		/* bboxProc */
};


/*
 * Pointer to int, for some portable pointer hacks - it's guaranteed that
 * 'uintptr_'t and 'void *' are convertible in both directions (C99 7.18.1.4).
 */

typedef union {
    void *ptr;
    uintptr_t flag;
} __ptr_to_int;

static void * MarkPointer(void *ptr) { __ptr_to_int p; p.ptr = ptr; p.flag |= 1; return p.ptr; }

static const TkTextDispChunkProcs layoutElideProcs = {
    TEXT_DISP_ELIDED,	/* type */
    NULL,		/* displayProc */
    NULL,		/* undisplayProc */
    ElideMeasureProc,	/* measureProc */
    ElideBboxProc,	/* bboxProc */
};

#ifndef NDEBUG
/*
 * The following counters keep statistics about redisplay that can be checked
 * to see how clever this code is at reducing redisplays.
 */

typedef struct Statistic {
    unsigned numRedisplays;	/* Number of calls to DisplayText. */
    unsigned linesRedrawn;	/* Number of calls to DisplayDLine. */
    unsigned numLayouted;	/* Number of calls to LayoutDLine. */
    unsigned numCopies;		/* Number of calls to XCopyArea to copy part of the screen. */
    unsigned lineHeightsRecalculated;
				/* Number of line layouts purely for height calculation purposes. */
    unsigned breakInfo;		/* Number of line break computations. */
    unsigned numCached;		/* Number of computed cached lines. */
    unsigned numHits;		/* Number of found cached lines. */
    unsigned numReused;		/* Number of re-used display lines. */

    bool perfFuncIsHooked;
} Statistic;

static Statistic stats;

static void
PerfStatistic()
{
    if (!tkBTreeDebug) {
	return;
    }

    fprintf(stderr, "PERFORMANCE -------------------\n");
    fprintf(stderr, "Calls to DisplayText:    %6u\n", stats.numRedisplays);
    fprintf(stderr, "Calls to DisplayDLine:   %6u\n", stats.linesRedrawn);
    fprintf(stderr, "Calls to LayoutDLine:    %6u\n", stats.numLayouted);
    fprintf(stderr, "Calls to XCopyArea:      %6u\n", stats.numCopies);
    fprintf(stderr, "Re-used display lines:   %6u\n", stats.numReused);
    fprintf(stderr, "Cached display lines:    %6u\n", stats.numCached);
    fprintf(stderr, "Found in cache:          %6u\n", stats.numHits);
    fprintf(stderr, "Line metric calculation: %6u\n", stats.lineHeightsRecalculated);
    fprintf(stderr, "Break info computation:  %6u\n", stats.breakInfo);
}
#endif /* NDEBUG */

#if TK_CHECK_ALLOCS

/*
 * Some stuff for memory checks, and allocation statistic.
 */

static unsigned tkTextCountNewStyle = 0;
static unsigned tkTextCountDestroyStyle = 0;
static unsigned tkTextCountNewChunk = 0;
static unsigned tkTextCountDestroyChunk = 0;
static unsigned tkTextCountNewSection = 0;
static unsigned tkTextCountDestroySection = 0;
static unsigned tkTextCountNewCharInfo = 0;
static unsigned tkTextCountDestroyCharInfo = 0;
static unsigned tkTextCountNewBreakInfo = 0;
static unsigned tkTextCountDestroyBreakInfo = 0;
static unsigned tkTextCountNewDLine = 0;
static unsigned tkTextCountDestroyDLine = 0;
static unsigned tkTextCountNewDispInfo = 0;
unsigned tkTextCountDestroyDispInfo = 0; /* referenced in tkTextBTree.c */
#if TK_LAYOUT_WITH_BASE_CHUNKS
unsigned tkTextCountNewBaseChars = 0;
unsigned tkTextCountDestroyBaseChars = 0;
#endif

extern unsigned tkTextCountDestroySegment;
extern unsigned tkRangeListCountNew;
extern unsigned tkRangeListCountDestroy;

static bool hookStatFunc = true;

static void
AllocStatistic()
{
    if (!tkBTreeDebug) {
	return;
    }

    fprintf(stderr, "--------------------------------\n");
    fprintf(stderr, "ALLOCATION:       new    destroy\n");
    fprintf(stderr, "--------------------------------\n");
    fprintf(stderr, "DLine:       %8u - %8u\n", tkTextCountNewDLine, tkTextCountDestroyDLine);
    fprintf(stderr, "Chunk:       %8u - %8u\n", tkTextCountNewChunk, tkTextCountDestroyChunk);
    fprintf(stderr, "Section:     %8u - %8u\n", tkTextCountNewSection, tkTextCountDestroySection);
    fprintf(stderr, "CharInfo:    %8u - %8u\n", tkTextCountNewCharInfo, tkTextCountDestroyCharInfo);
    fprintf(stderr, "DispInfo:    %8u - %8u\n", tkTextCountNewDispInfo, tkTextCountDestroyDispInfo);
    fprintf(stderr, "BreakInfo:   %8u - %8u\n", tkTextCountNewBreakInfo, tkTextCountDestroyBreakInfo);
#if TK_LAYOUT_WITH_BASE_CHUNKS
    fprintf(stderr, "BaseChars:   %8u - %8u\n", tkTextCountNewBaseChars, tkTextCountDestroyBaseChars);
#endif
    fprintf(stderr, "Style:       %8u - %8u\n", tkTextCountNewStyle, tkTextCountDestroyStyle);
    fprintf(stderr, "RangeList:   %8u - %8u\n", tkRangeListCountNew, tkRangeListCountDestroy);

    if (tkTextCountNewDLine != tkTextCountDestroyDLine
	    || tkTextCountNewChunk != tkTextCountDestroyChunk
	    || tkTextCountNewSection != tkTextCountDestroySection
	    || tkTextCountNewCharInfo != tkTextCountDestroyCharInfo
	    || tkTextCountNewDispInfo != tkTextCountDestroyDispInfo
#if TK_LAYOUT_WITH_BASE_CHUNKS
	    || tkTextCountNewBaseChars != tkTextCountDestroyBaseChars
#endif
	    || tkTextCountNewStyle != tkTextCountDestroyStyle
	    || tkRangeListCountNew != tkRangeListCountDestroy) {
	fprintf(stderr, "*** memory leak detected ***\n");
    }
}
#endif /* TK_CHECK_ALLOCS */

/*
 * Some helpers:
 */

static const char doNotBreakAtAll[8] = {
    LINEBREAK_NOBREAK, LINEBREAK_NOBREAK, LINEBREAK_NOBREAK, LINEBREAK_NOBREAK,
    LINEBREAK_NOBREAK, LINEBREAK_NOBREAK, LINEBREAK_NOBREAK, LINEBREAK_NOBREAK };

static bool IsPowerOf2(unsigned n) { return !(n & (n - 1)); }

static bool IsBlank(int ch) { return ch == ' ' || ch == '\t'; }

static unsigned
NextPowerOf2(uint32_t n)
{
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return ++n;
}

static bool
IsExpandableSpace(
    const char *s)
{
    /* Normal space or non-break space? */
    return UCHAR(s[0]) == 0x20 || (UCHAR(s[0]) == 0xc2 && UCHAR(s[1]) == 0x0a);
}

static void
LogTextHeightCalc(
    TkText *textPtr,
    const TkTextIndex *indexPtr)
{
    char string[TK_POS_CHARS];

    assert(tkTextDebug);

    /*
     * Debugging is enabled, so keep a log of all the lines whose
     * height was recalculated. The test suite uses this information.
     */

    TkTextPrintIndex(textPtr, indexPtr, string);
    LOG("tk_textHeightCalc", string);
}

static void
LogTextRelayout(
    TkText *textPtr,
    const TkTextIndex *indexPtr)
{
    char string[TK_POS_CHARS];

    assert(tkTextDebug);

    /*
     * Debugging is enabled, so keep a log of all the lines that
     * were re-layed out. The test suite uses this information.
     */

    TkTextPrintIndex(textPtr, indexPtr, string);
    LOG("tk_textRelayout", string);
}

static void
LogTextInvalidateLine(
    TkText *textPtr,
    unsigned count)
{
    char buffer[4*TCL_INTEGER_SPACE + 3];
    const TkRangeList *ranges = textPtr->dInfoPtr->lineMetricUpdateRanges;
    unsigned totalCount = TkRangeListCount(ranges) - count;
    unsigned totalLines = TkBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr);
    int lineNum = TkRangeListIsEmpty(ranges) ? -1 : TkRangeListLow(ranges);

    assert(tkTextDebug);

    snprintf(buffer, sizeof(buffer), "%d %u - %u %u", lineNum, totalLines, count, totalCount);
    LOG("tk_textInvalidateLine", buffer);
}

static void
DisplayTextWhenIdle(
    TkText *textPtr)
{
    if (textPtr->sharedTextPtr->allowUpdateLineMetrics && !(textPtr->dInfoPtr->flags & REDRAW_PENDING)) {
	textPtr->dInfoPtr->flags |= REDRAW_PENDING;
	Tcl_DoWhenIdle(DisplayText, textPtr);
    }
}

static int
GetLeftLineMargin(
    const DLine *dlPtr,
    const StyleValues *sValuePtr)
{
    assert(dlPtr);
    assert(sValuePtr);
    return (dlPtr->flags & PARAGRAPH_START) ? sValuePtr->lMargin1 : sValuePtr->lMargin2;
}

#if SPEEDUP_MONOSPACED_LINE_HEIGHTS

static bool
TestMonospacedLineHeights(
    const TkText *textPtr)
{
    return textPtr->wrapMode == TEXT_WRAPMODE_NONE
	    && textPtr->dInfoPtr->countImages == 0
	    && textPtr->dInfoPtr->countWindows == 0
	    && TkTextTagSetDisjunctiveBits(TkBTreeRootTagInfo(textPtr->sharedTextPtr->tree),
		textPtr->sharedTextPtr->affectLineHeightTags);
    return false;
}

#endif /* SPEEDUP_MONOSPACED_LINE_HEIGHTS */

static bool
UseMonospacedLineHeights(
    const TkText *textPtr)
{
#if SPEEDUP_MONOSPACED_LINE_HEIGHTS
    return TestMonospacedLineHeights(textPtr)
	    && TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges);
#else
    return false;
#endif
}

/*
 * Some helpers for hyphenation support (Latin-1 only):
 */

static const unsigned char isVowel[256] = {
#define _ 0
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 00 - 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 10 - 1f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 20 - 2f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 30 - 3f */
    _, 1, _, _, _, 1, _, _, _, 1, _, _, _, _, _, 1, /* 40 - 4f */
    _, _, _, _, _, 1, _, _, _, _, _, _, _, _, _, _, /* 50 - 5f */
    _, 1, _, _, _, 1, _, _, _, 1, _, _, _, _, _, 1, /* 60 - 6f */
    _, _, _, _, _, 1, _, _, _, _, _, _, _, _, _, _, /* 70 - 7f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 80 - 8f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 90 - 9f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* a0 - af */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* b0 - bf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* c0 - cf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* d0 - df */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* e0 - ef */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* f0 - ff */
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
#undef _
};

static const unsigned char isConsonant[256] = {
#define _ 0
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 00 - 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 10 - 1f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 20 - 2f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 30 - 3f */
    _, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, /* 40 - 4f */
    1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, _, _, _, _, _, /* 50 - 5f */
    _, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, /* 60 - 6f */
    1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, _, _, _, _, _, /* 70 - 7f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 80 - 8f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 90 - 9f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* a0 - af */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* b0 - bf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* c0 - cf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* d0 - df */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* e0 - ef */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* f0 - ff */
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
#undef _
};

static const unsigned char isUmlaut[256] = {
#define _ 0
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 00 - 0f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 10 - 1f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 20 - 2f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 30 - 3f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 40 - 4f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 50 - 5f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 60 - 6f */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* 70 - 7f */
    _, _, _, _, 1, _, _, _, _, _, _, 1, _, _, _, _, /* 80 - 8f */
    _, _, _, _, _, _, 1, _, _, _, _, _, 1, _, _, _, /* 90 - 9f */
    _, _, _, _, 1, _, _, _, _, _, _, 1, _, _, _, _, /* a0 - af */
    _, _, _, _, _, _, 1, _, _, _, _, _, 1, _, _, _, /* b0 - bf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* c0 - cf */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* d0 - df */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* e0 - ef */
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, /* f0 - ff */
/*  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f */
#undef _
};

static const unsigned char umlautToVowel[256] = {
#define ___ 0
/*   00   01   02   03   04   05   06   07   08   09   0a   0b   0c   0d   0e   0f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 00 - 0f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 10 - 1f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 20 - 2f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 30 - 3f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 40 - 4f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 50 - 5f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 60 - 6f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 70 - 7f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 80 - 8f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* 90 - 9f */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* a0 - af */
    ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, ___, /* b0 - bf */
    ___, ___, ___, ___, 'A', ___, ___, ___, ___, ___, ___, 'E', ___, ___, ___, ___, /* c0 - cf */
    ___, ___, ___, ___, ___, ___, 'O', ___, ___, ___, ___, ___, 'U', ___, ___, ___, /* d0 - df */
    ___, ___, ___, ___, 'a', ___, ___, ___, ___, ___, ___, 'e', ___, ___, ___, ___, /* e0 - ef */
    ___, ___, ___, ___, ___, ___, 'o', ___, ___, ___, ___, ___, 'u', ___, ___, ___, /* f0 - ff */
/*   00   01   02   03   04   05   06   07   08   09   0a   0b   0c   0d   0e   0f */
#undef ___
};

static bool IsVowel(unsigned char c)     { return isVowel[c]; }
static bool IsUmlaut(unsigned char c)    { return umlautToVowel[c] != 0; }
static bool IsConsonant(unsigned char c) { return isConsonant[c]; }

static unsigned char UmlautToVowel(unsigned char c) { return umlautToVowel[c]; }
static unsigned char ConvertC3Next(unsigned char c) { return 0xc0 | (c - 0x80); }

static bool
IsUmlautOrVowel(const char *s)
{
    return UCHAR(s[0]) == 0xc3 ? isUmlaut[UCHAR(s[1])] : UCHAR(s[0]) < 0x80 && isVowel[UCHAR(s[0])];
}

static void
SetupHyphenChars(
    TkTextSegment *segPtr,
    unsigned offset)
{
    assert(offset <= 2); /* don't exceed 5 characters */

    /*
     * NOTE: U+2010 (HYPHEN) always has a visible rendition, but U+00AD
     * (SOFT HYPHEN) is an invisible format character (per definition).
     * And don't use '-' (U+002D = HYPHEN-MINUS), because the meaning of
     * this character is contextual. So we have to use U+2010.
     */

    assert(segPtr->typePtr->group == SEG_GROUP_HYPHEN);
    assert(sizeof(doNotBreakAtAll) >= 6); /* we need this break array for hyphens */

    memcpy(segPtr->body.chars + offset, "\xe2\x80\x90", 4); /* U+2010 */
    segPtr->body.hyphen.textSize = 3 + offset;
}

static bool
IsDoubleDigraph(
    char c1,
    char c2)
{
    switch (c1) {
	case 'c': /* fallthru */	/* c-cs -> cs-cs */
	case 'z': return c2 == 's';	/* z-zs -> zs-zs */
	case 'g': /* fallthru */	/* g-gy -> gy-gy */
	case 'l': /* fallthru */	/* l-ly -> ly-ly */
	case 'n': /* fallthru */	/* n-ny -> ny-ny */
	case 't': return c2 == 'y';	/* t-ty -> ty-ty */
	case 's': return c2 == 'z';	/* s-sz -> sz-sz */
    }
    return false;
}

static bool
IsHyphenChunk(
    const TkTextDispChunk *chunkPtr)
{
    assert(chunkPtr);
    return chunkPtr->layoutProcs && chunkPtr->layoutProcs->type == TEXT_DISP_HYPHEN;
}

static bool
IsCharChunk(
    const TkTextDispChunk *chunkPtr)
{
    assert(chunkPtr);
    return chunkPtr->layoutProcs && chunkPtr->layoutProcs->type == TEXT_DISP_CHAR;
}

static char
GetLastCharInChunk(
    const TkTextDispChunk *chunkPtr)
{
    const CharInfo *ciPtr;

    if (!chunkPtr) {
	return '\0';
    }

    assert(chunkPtr->layoutProcs);
    assert(chunkPtr->clientData);

    if (!IsCharChunk(chunkPtr)) {
	return '\0';
    }

    ciPtr = chunkPtr->clientData;
    assert(ciPtr->numBytes > 0);
    return ciPtr->u.chars[ciPtr->baseOffset + ciPtr->numBytes - 1];
}

static char
GetSecondLastCharInChunk(
    const TkTextDispChunk *chunkPtr)
{
    const CharInfo *ciPtr;

    if (!chunkPtr || !IsCharChunk(chunkPtr)) {
	return '\0';
    }

    ciPtr = chunkPtr->clientData;
    assert(chunkPtr->clientData);
    assert(ciPtr->numBytes > 0);

    if (ciPtr->numBytes > 1) {
	return ciPtr->u.chars[ciPtr->baseOffset + ciPtr->numBytes - 2];
    }
    if ((chunkPtr = chunkPtr->prevCharChunkPtr) && IsCharChunk(chunkPtr)) {
	ciPtr = chunkPtr->clientData;
	assert(ciPtr->numBytes > 0);
	return ciPtr->u.chars[ciPtr->baseOffset + ciPtr->numBytes - 1];
    }

    return '\0';
}

static int
FilterHyphenRules(
    int hyphenRules,
    const char *lang)
{
    if (lang && hyphenRules) {
	enum {
	    CA_RULES = (1 << TK_TEXT_HYPHEN_GEMINATION),
	    DE_RULES = (1 << TK_TEXT_HYPHEN_CK)|(1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT),
	    HU_RULES = (1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH),
	    NL_RULES = (1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL)|(1 << TK_TEXT_HYPHEN_TREMA),
	    NO_RULES = (1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT),
	    PL_RULES = (1 << TK_TEXT_HYPHEN_REPEAT),
	    SV_RULES = (1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT)
	};

	switch (lang[0]) {
	case 'c': if (lang[1] == 'a') { hyphenRules &= CA_RULES; }; break;
	case 'd': if (lang[1] == 'e') { hyphenRules &= DE_RULES; }; break;
	case 'h': if (lang[1] == 'u') { hyphenRules &= HU_RULES; }; break;
	case 'p': if (lang[1] == 'l') { hyphenRules &= PL_RULES; }; break;
	case 's': if (lang[1] == 'v') { hyphenRules &= SV_RULES; }; break;
	case 'n':
	    switch (lang[1]) {
	    case 'b': /* fallthru */
	    case 'n': /* fallthru */
	    case 'o': hyphenRules &= NO_RULES; break;
	    case 'l': hyphenRules &= NL_RULES; break;
	    }
	    break;
	}
    }

    return hyphenRules;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextPendingSync --
 *
 *	This function checks if any line heights are not up-to-date.
 *
 * Results:
 *	Returns boolean 'true' if it is the case, or 'false' if all line
 *      heights are up-to-date.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

bool
TkTextPendingSync(
    const TkText *textPtr)	/* Information about text widget. */
{
    /*
     * NOTE: We cannot use
     *
     *    !TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges)
     *
     * because this statement does not guarantee that TkTextRunAfterSyncCmd has
     * been triggered, and we need the state after triggering.
     */

    return !!(textPtr->dInfoPtr->flags & (ASYNC_UPDATE|ASYNC_PENDING));
}

/*
 *--------------------------------------------------------------
 *
 * TestIfLinesUpToDate --
 *
 *	This function checks whether the lines up to given index
 *	position (inclusive) is up-to-date.
 *
 * Results:
 *	Returns boolean 'true' if it is the case, or 'false' if these
 *      line heights aren't up-to-date.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
TestIfLinesUpToDate(
    const TkTextIndex *indexPtr)	/* last line of range (inclusive) */
{
    const TkRangeList *ranges;

    assert(indexPtr->textPtr);

    ranges = indexPtr->textPtr->dInfoPtr->lineMetricUpdateRanges;

    if (TkRangeListIsEmpty(ranges)) {
	return true;
    }

    return (int) TkTextIndexGetLineNumber(indexPtr, indexPtr->textPtr) < TkRangeListLow(ranges);
}

/*
 *----------------------------------------------------------------------
 *
 * InvokeAsyncUpdateYScrollbar --
 *
 *	This function invokes the update of the vertical scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
InvokeAsyncUpdateYScrollbar(
    TkText *textPtr)
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    assert(!dInfoPtr->scrollbarTimer);
    textPtr->refCount += 1;

    if (textPtr->syncTime == 0) {
	AsyncUpdateYScrollbar(textPtr);
    } else {
	dInfoPtr->scrollbarTimer = Tcl_CreateTimerHandler(textPtr->syncTime,
		AsyncUpdateYScrollbar, textPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InvokeAsyncUpdateLineMetrics --
 *
 *	This function invokes the update of the line metric calculation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
InvokeAsyncUpdateLineMetrics(
    TkText *textPtr)
{
    assert(textPtr->sharedTextPtr->allowUpdateLineMetrics);

    if (textPtr->syncTime > 0) {
	TextDInfo *dInfoPtr = textPtr->dInfoPtr;

	if (!dInfoPtr->lineUpdateTimer) {
	    textPtr->refCount += 1;
	    dInfoPtr->lineUpdateTimer = Tcl_CreateTimerHandler(1, AsyncUpdateLineMetrics, textPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCreateDInfo --
 *
 *	This function is called when a new text widget is created. Its job is
 *	to set up display-related information for the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A TextDInfo data structure is allocated and initialized and attached
 *	to textPtr.
 *
 *----------------------------------------------------------------------
 */

static void
SetupEolSegment(
    TkText *textPtr,
    TextDInfo *dInfoPtr)
{
    char eolChar[10];
    Tcl_UniChar uc;
    const char *p = textPtr->eolCharPtr ? Tcl_GetString(textPtr->eolCharPtr) : NULL;
    int len;

    if (!p || !*p) {
	p = "\xc2\xb6"; /* U+00B6 = PILCROW SIGN */
    }
    len = Tcl_UtfToUniChar(p, &uc);
    strcpy(eolChar, p);
    strcpy(eolChar + len, "\n");
    if (dInfoPtr->endOfLineSegPtr) {
	TkBTreeFreeSegment(dInfoPtr->endOfLineSegPtr);
    }
    dInfoPtr->endOfLineSegPtr = TkBTreeMakeCharSegment(
	    eolChar, len + 1, textPtr->sharedTextPtr->emptyTagInfoPtr);
}

static void
SetupEotSegment(
    TkText *textPtr,
    TextDInfo *dInfoPtr)
{
    char eotChar[10];
    Tcl_UniChar uc;
    const char *p = textPtr->eotCharPtr ? Tcl_GetString(textPtr->eotCharPtr) : NULL;
    int len;

    if (!p || !*p) {
	if (textPtr->eolCharPtr) {
	    p = Tcl_GetString(textPtr->eolCharPtr);
	}
	if (!p || !*p) {
	    p = "\xc2\xb6"; /* U+00B6 = PILCROW SIGN */
	}
    }
    len = Tcl_UtfToUniChar(p, &uc);
    strcpy(eotChar, p);
    strcpy(eotChar + len, "\n");
    if (dInfoPtr->endOfTextSegPtr) {
	TkBTreeFreeSegment(dInfoPtr->endOfTextSegPtr);
    }
    dInfoPtr->endOfTextSegPtr = TkBTreeMakeCharSegment(
	    eotChar, len + 1, textPtr->sharedTextPtr->emptyTagInfoPtr);
}

void
TkTextCreateDInfo(
    TkText *textPtr)	/* Overall information for text widget. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextBTree tree = sharedTextPtr->tree;
    TextDInfo *dInfoPtr;
    XGCValues gcValues;
    bool isMonospaced;

    dInfoPtr = memset(malloc(sizeof(TextDInfo)), 0, sizeof(TextDInfo));
    Tcl_InitHashTable(&dInfoPtr->styleTable, sizeof(StyleValues)/sizeof(int));
    gcValues.graphics_exposures = True;
    dInfoPtr->copyGC = None;
    dInfoPtr->scrollGC = Tk_GetGC(textPtr->tkwin, GCGraphicsExposures, &gcValues);
    dInfoPtr->insertFgGC = None;
    dInfoPtr->xScrollFirst = -1;
    dInfoPtr->xScrollLast = -1;
    dInfoPtr->yScrollFirst = -1;
    dInfoPtr->yScrollLast = -1;
    dInfoPtr->topLineNo = -1;
    dInfoPtr->topByteIndex = -1;
    dInfoPtr->flags = DINFO_OUT_OF_DATE;
    dInfoPtr->lineMetricUpdateRanges = TkRangeListCreate(64);
    dInfoPtr->firstLineNo = TkBTreeLinesTo(tree, NULL, TkBTreeGetStartLine(textPtr), NULL);
    dInfoPtr->lastLineNo = TkBTreeLinesTo(tree, NULL, TkBTreeGetLastLine(textPtr), NULL);
    dInfoPtr->lineMetricUpdateEpoch = 1;
    dInfoPtr->strBufferSize = 512;
    dInfoPtr->strBuffer = malloc(dInfoPtr->strBufferSize);
    TkTextIndexClear(&dInfoPtr->metricIndex, textPtr);
    TkTextIndexClear(&dInfoPtr->currChunkIndex, textPtr);
    SetupEolSegment(textPtr, dInfoPtr);
    SetupEotSegment(textPtr, dInfoPtr);

    if (textPtr->state == TK_TEXT_STATE_NORMAL
	    && textPtr->blockCursorType
	    && textPtr->showInsertFgColor) {
	XGCValues gcValues;
	gcValues.foreground = textPtr->insertFgColorPtr->pixel;
	dInfoPtr->insertFgGC = Tk_GetGC(textPtr->tkwin, GCForeground, &gcValues);
    }

    /*
     * Note: Setup of defaultStyle must be postponed.
     */

    textPtr->dInfoPtr = dInfoPtr;
    isMonospaced = UseMonospacedLineHeights(textPtr);

    if (isMonospaced) {
	TkBTreeUpdatePixelHeights(textPtr, TkBTreeGetStartLine(textPtr), 1,
		dInfoPtr->lineMetricUpdateEpoch);
    } else {
	dInfoPtr->lineMetricUpdateRanges = TkRangeListAdd(dInfoPtr->lineMetricUpdateRanges, 0, 0);
    }

    if (!sharedTextPtr->breakInfoTableIsInitialized) {
	Tcl_InitHashTable(&sharedTextPtr->breakInfoTable, TCL_ONE_WORD_KEYS);
	sharedTextPtr->breakInfoTableIsInitialized = true;
    }

    if (sharedTextPtr->allowUpdateLineMetrics) {
	if (!isMonospaced) {
	    InvokeAsyncUpdateLineMetrics(textPtr);
	}
	InvokeAsyncUpdateYScrollbar(textPtr);
    }

#if TK_CHECK_ALLOCS
    if (hookStatFunc) {
	atexit(AllocStatistic);
	hookStatFunc = false;
    }
#endif
#ifndef NDEBUG
    if (!stats.perfFuncIsHooked) {
#ifndef _MSC_VER	/* MSVC erroneously triggers warning warning C4113 */
	atexit(PerfStatistic);
#endif
	stats.perfFuncIsHooked = true;
    }
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * TkTextDeleteBreakInfoTableEntries --
 *
 *	Delete all cached break information. Normally this table will
 *	be empty when this function is called, but under some specific
 *	conditions the given table will not be empty - this will only
 *	happen if a tag redraw action has been interrupted, and this
 *	will be seldom the case.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some resources might be freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextDeleteBreakInfoTableEntries(
    Tcl_HashTable *breakInfoTable)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    assert(breakInfoTable);

    for (hPtr = Tcl_FirstHashEntry(breakInfoTable, &search); hPtr; hPtr = Tcl_NextHashEntry(&search)) {
	TkTextBreakInfo *breakInfo = Tcl_GetHashValue(hPtr);

	assert(breakInfo->brks);
	free(breakInfo->brks);
	free(breakInfo);
	DEBUG_ALLOC(tkTextCountDestroyBreakInfo++);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFreeDInfo --
 *
 *	This function is called to free up all of the private display
 *	information kept by this file for a text widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots of resources get freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFreeDInfo(
    TkText *textPtr)		/* Overall information for text widget. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextDispChunk *chunkPtr;
    TkTextDispChunkSection *sectionPtr;
    DLine *dlPtr;
    CharInfo *ciPtr;

    /*
     * Cancel pending events.
     */

    if (dInfoPtr->pendingUpdateLineMetricsFinished) {
	Tcl_CancelIdleCall(RunUpdateLineMetricsFinished, (ClientData) textPtr);
    }
    if (dInfoPtr->flags & REDRAW_PENDING) {
	Tcl_CancelIdleCall(DisplayText, textPtr);
    }

    /*
     * Be careful to free up styleTable *after* freeing up all the DLines, so
     * that the hash table is still intact to free up the style-related
     * information from the lines. Once the lines are all free then styleTable
     * will be empty.
     */

    FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK);
    FreeDLines(textPtr, dInfoPtr->savedDLinePtr, NULL, DLINE_FREE_TEMP);
    FreeDLines(textPtr, NULL, NULL, DLINE_CACHE);  /* release cached lines */
    FreeDLines(textPtr, NULL, NULL, DLINE_METRIC); /* release cached lines */

    if (dInfoPtr->copyGC != None) {
	Tk_FreeGC(textPtr->display, dInfoPtr->copyGC);
    }
    Tk_FreeGC(textPtr->display, dInfoPtr->scrollGC);
    if (dInfoPtr->insertFgGC != None) {
	Tk_FreeGC(textPtr->display, dInfoPtr->insertFgGC);
    }
    if (dInfoPtr->lineUpdateTimer) {
	Tcl_DeleteTimerHandler(dInfoPtr->lineUpdateTimer);
	textPtr->refCount -= 1;
	dInfoPtr->lineUpdateTimer = NULL;
    }
    if (dInfoPtr->scrollbarTimer) {
	Tcl_DeleteTimerHandler(dInfoPtr->scrollbarTimer);
	textPtr->refCount -= 1;
	dInfoPtr->scrollbarTimer = NULL;
    }
    if (dInfoPtr->repickTimer) {
	Tcl_DeleteTimerHandler(dInfoPtr->repickTimer);
	textPtr->refCount -= 1;
	dInfoPtr->repickTimer = NULL;
    }
    ciPtr = dInfoPtr->charInfoPoolPtr;
    while (ciPtr) {
	CharInfo *nextPtr = ciPtr->u.next;
	free(ciPtr);
	DEBUG_ALLOC(tkTextCountDestroyCharInfo++);
	ciPtr = nextPtr;
    }
    sectionPtr = dInfoPtr->sectionPoolPtr;
    while (sectionPtr) {
	TkTextDispChunkSection *nextPtr = sectionPtr->nextPtr;
	free(sectionPtr);
	DEBUG_ALLOC(tkTextCountDestroySection++);
	sectionPtr = nextPtr;
    }
    chunkPtr = dInfoPtr->chunkPoolPtr;
    while (chunkPtr) {
	TkTextDispChunk *nextPtr = chunkPtr->nextPtr;
	free(chunkPtr);
	DEBUG_ALLOC(tkTextCountDestroyChunk++);
	chunkPtr = nextPtr;
    }
    dlPtr = dInfoPtr->dLinePoolPtr;
    while (dlPtr) {
	DLine *nextPtr = dlPtr->nextPtr;
	free(dlPtr);
	DEBUG_ALLOC(tkTextCountDestroyDLine++);
	dlPtr = nextPtr;
    }
    if (dInfoPtr->defaultStyle) {
#if 0
	/*
	 * TODO: The following assertion sometimes fails. Luckily it doesn't matter,
	 * because it will be freed anyway, but why can it fail (and only sometimes)?
	 */
	 DEBUG_ALLOC(assert(dInfoPtr->defaultStyle->refCount == 1));
#endif
	FreeStyle(textPtr, dInfoPtr->defaultStyle);
    }
    Tcl_DeleteHashTable(&dInfoPtr->styleTable);
    TkRangeListDestroy(&dInfoPtr->lineMetricUpdateRanges);
    TkBTreeFreeSegment(dInfoPtr->endOfLineSegPtr);
    TkBTreeFreeSegment(dInfoPtr->endOfTextSegPtr);
    free(dInfoPtr->strBuffer);
    free(dInfoPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextResetDInfo --
 *
 *	This function will be called when the whole text has been deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Line metrics will be updated.
 *
 *----------------------------------------------------------------------
 */

void
TkTextResetDInfo(
    TkText *textPtr)	/* Overall information for text widget. */
{
    TextDInfo *dInfoPtr;
    TkSharedText *sharedTextPtr;
    TkTextIndex index1, index2;
    unsigned lineNo1, lineNo2;

    if (UseMonospacedLineHeights(textPtr)) {
	return; /* already synchronized */
    }

    dInfoPtr = textPtr->dInfoPtr;
    sharedTextPtr = textPtr->sharedTextPtr;

    TkTextIndexSetupToStartOfText(&index1, textPtr, sharedTextPtr->tree);
    TkTextIndexSetupToEndOfText(&index2, textPtr, sharedTextPtr->tree);
    TkTextChanged(sharedTextPtr, NULL, &index1, &index2);

    lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, textPtr, TkTextIndexGetLine(&index1), NULL);
    lineNo2 = TkBTreeLinesTo(sharedTextPtr->tree, textPtr, TkTextIndexGetLine(&index2), NULL);

    assert(lineNo1 < lineNo2);

    TkRangeListClear(dInfoPtr->lineMetricUpdateRanges);
    dInfoPtr->lineMetricUpdateRanges =
	    TkRangeListAdd(dInfoPtr->lineMetricUpdateRanges, lineNo1, lineNo2 - 1);
    dInfoPtr->lineMetricUpdateEpoch = 1;
    dInfoPtr->topLineNo = -1;
    dInfoPtr->topByteIndex = -1;

    if (textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	TkTextUpdateLineMetrics(textPtr, lineNo1, lineNo2);
    }

    FreeDLines(textPtr, NULL, NULL, DLINE_CACHE);  /* release cached lines */
    FreeDLines(textPtr, NULL, NULL, DLINE_METRIC); /* release cached lines */
}

/*
 *----------------------------------------------------------------------
 *
 * GetStyle --
 *
 *	This function creates all the information needed to display text at a
 *	particular location.
 *
 * Results:
 *	The return value is a pointer to a TextStyle structure that
 *	corresponds to *sValuePtr.
 *
 * Side effects:
 *	A new entry may be created in the style table for the widget.
 *
 *----------------------------------------------------------------------
 */

static TextStyle *
MakeStyle(
    TkText *textPtr,
    TkTextTag *tagPtr,
    bool containsSelection)
{
    StyleValues styleValues;
    TextStyle *stylePtr;
    Tcl_HashEntry *hPtr;
    XGCValues gcValues;
    unsigned long mask;
    bool isNew;

    /*
     * The variables below keep track of the highest-priority specification
     * that has occurred for each of the various fields of the StyleValues.
     */

    int borderPrio = -1, borderWidthPrio = -1, reliefPrio = -1;
    int bgStipplePrio = -1, indentBgPrio = -1;
    int fgPrio = -1, fontPrio = -1, fgStipplePrio = -1;
    int underlinePrio = -1, elidePrio = -1, justifyPrio = -1, offsetPrio = -1;
    int lMargin1Prio = -1, lMargin2Prio = -1, rMarginPrio = -1;
    int lMarginColorPrio = -1, rMarginColorPrio = -1;
    int spacing1Prio = -1, spacing2Prio = -1, spacing3Prio = -1;
    int overstrikePrio = -1, tabPrio = -1, tabStylePrio = -1;
    int wrapPrio = -1, langPrio = -1, hyphenRulesPrio = -1;
    int eolColorPrio = -1, hyphenColorPrio = -1;

    /*
     * Find out what tags are present for the character, then compute a
     * StyleValues structure corresponding to those tags (scan through all of
     * the tags, saving information for the highest-priority tag).
     */

    memset(&styleValues, 0, sizeof(StyleValues));
    styleValues.relief = TK_RELIEF_FLAT;
    styleValues.fgColor = textPtr->fgColor;
    styleValues.eolColor = textPtr->eolColor;
    styleValues.eotColor = textPtr->eotColor ? textPtr->eotColor : textPtr->eolColor;
    styleValues.hyphenColor = textPtr->hyphenColor;
    styleValues.underlineColor = textPtr->fgColor;
    styleValues.overstrikeColor = textPtr->fgColor;
    styleValues.tkfont = textPtr->tkfont;
    styleValues.justify = textPtr->justify;
    styleValues.spacing1 = textPtr->spacing1;
    styleValues.spacing2 = textPtr->spacing2;
    styleValues.spacing3 = textPtr->spacing3;
    styleValues.tabArrayPtr = textPtr->tabArrayPtr;
    styleValues.tabStyle = textPtr->tabStyle;
    styleValues.wrapMode = textPtr->wrapMode;
    styleValues.lang = textPtr->lang;
    styleValues.hyphenRules = textPtr->hyphenRulesPtr ? textPtr->hyphenRules : TK_TEXT_HYPHEN_MASK;

    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	Tk_3DBorder border;
        XColor *fgColor;
	int priority;

	border = tagPtr->border;
        fgColor = tagPtr->fgColor;
	priority = tagPtr->priority;

	/*
	 * If this is the selection tag, and inactiveSelBorder is NULL (the
	 * default on Windows), then we need to skip it if we don't have the
	 * focus.
	 */

	if (tagPtr == textPtr->selTagPtr && !(textPtr->flags & HAVE_FOCUS)) {
	    if (!textPtr->inactiveSelBorder) {
		continue;
	    }
#ifdef MAC_OSX_TK
	    /* Don't show inactive selection in disabled widgets. */
	    if (textPtr->state == TK_TEXT_STATE_DISABLED) {
		continue;
	    }
#endif
	    border = textPtr->inactiveSelBorder;
	    fgColor = textPtr->inactiveSelFgColorPtr;
	}
	if (containsSelection) {
	    if (tagPtr->selBorder) {
		border = tagPtr->selBorder;
	    }
	    if (tagPtr->selFgColor != None) {
		fgColor = tagPtr->selFgColor;
	    } else if (fgColor == None) {
		fgColor = textPtr->selFgColorPtr;
	    }
	}
	if (border && priority > borderPrio) {
	    styleValues.border = border;
	    borderPrio = priority;
	}
	if (tagPtr->borderWidthPtr
		&& Tcl_GetString(tagPtr->borderWidthPtr)[0] != '\0'
		&& priority > borderWidthPrio) {
	    styleValues.borderWidth = tagPtr->borderWidth;
	    borderWidthPrio = priority;
	}
	if (tagPtr->reliefPtr && priority > reliefPrio) {
	    if (!styleValues.border) {
		styleValues.border = textPtr->border;
	    }
	    assert(tagPtr->relief < 8);
	    styleValues.relief = tagPtr->relief;
	    reliefPrio = priority;
	}
	if (tagPtr->bgStipple != None && priority > bgStipplePrio) {
	    styleValues.bgStipple = tagPtr->bgStipple;
	    bgStipplePrio = priority;
	}
	if (tagPtr->indentBgString != None && priority > indentBgPrio) {
	    styleValues.indentBg = tagPtr->indentBg;
	    indentBgPrio = priority;
	}
	if (fgColor != None && priority > fgPrio) {
	    styleValues.fgColor = fgColor;
	    fgPrio = priority;
	}
	if (tagPtr->tkfont != None && priority > fontPrio) {
	    styleValues.tkfont = tagPtr->tkfont;
	    fontPrio = priority;
	}
	if (tagPtr->fgStipple != None && priority > fgStipplePrio) {
	    styleValues.fgStipple = tagPtr->fgStipple;
	    fgStipplePrio = priority;
	}
	if (tagPtr->justifyString && priority > justifyPrio) {
	    /* assert(tagPtr->justify < 8); always true due to range */
	    styleValues.justify = tagPtr->justify;
	    justifyPrio = priority;
	}
	if (tagPtr->lMargin1String && priority > lMargin1Prio) {
	    styleValues.lMargin1 = tagPtr->lMargin1;
	    lMargin1Prio = priority;
	}
	if (tagPtr->lMargin2String && priority > lMargin2Prio) {
	    styleValues.lMargin2 = tagPtr->lMargin2;
	    lMargin2Prio = priority;
	}
	if (tagPtr->lMarginColor && priority > lMarginColorPrio) {
	    styleValues.lMarginColor = tagPtr->lMarginColor;
	    lMarginColorPrio = priority;
	}
	if (tagPtr->offsetString && priority > offsetPrio) {
	    styleValues.offset = tagPtr->offset;
	    offsetPrio = priority;
	}
	if (tagPtr->overstrikeString && priority > overstrikePrio) {
	    styleValues.overstrike = tagPtr->overstrike;
	    overstrikePrio = priority;
            if (tagPtr->overstrikeColor != None) {
                 styleValues.overstrikeColor = tagPtr->overstrikeColor;
            } else if (fgColor != None) {
                 styleValues.overstrikeColor = fgColor;
            }
	}
	if (tagPtr->rMarginString && priority > rMarginPrio) {
	    styleValues.rMargin = tagPtr->rMargin;
	    rMarginPrio = priority;
	}
	if (tagPtr->rMarginColor && priority > rMarginColorPrio) {
	    styleValues.rMarginColor = tagPtr->rMarginColor;
	    rMarginColorPrio = priority;
	}
	if (tagPtr->spacing1String && priority > spacing1Prio) {
	    styleValues.spacing1 = tagPtr->spacing1;
	    spacing1Prio = priority;
	}
	if (tagPtr->spacing2String && priority > spacing2Prio) {
	    styleValues.spacing2 = tagPtr->spacing2;
	    spacing2Prio = priority;
	}
	if (tagPtr->spacing3String && priority > spacing3Prio) {
	    styleValues.spacing3 = tagPtr->spacing3;
	    spacing3Prio = priority;
	}
	if (tagPtr->tabStringPtr && priority > tabPrio) {
	    styleValues.tabArrayPtr = tagPtr->tabArrayPtr;
	    tabPrio = priority;
	}
	if (tagPtr->tabStyle != TK_TEXT_TABSTYLE_NONE && priority > tabStylePrio) {
	    assert(tagPtr->tabStyle < 8);
	    styleValues.tabStyle = tagPtr->tabStyle;
	    tabStylePrio = priority;
	}
	if (tagPtr->eolColor && priority > eolColorPrio) {
	    styleValues.eolColor = tagPtr->eolColor;
	    eolColorPrio = priority;
	}
	if (tagPtr->hyphenColor && priority > hyphenColorPrio) {
	    styleValues.hyphenColor = tagPtr->hyphenColor;
	    hyphenColorPrio = priority;
	}
	if (tagPtr->underlineString && priority > underlinePrio) {
	    styleValues.underline = tagPtr->underline;
	    underlinePrio = priority;
            if (tagPtr->underlineColor != None) {
		styleValues.underlineColor = tagPtr->underlineColor;
            } else if (fgColor != None) {
		styleValues.underlineColor = fgColor;
            }
	}
	if (tagPtr->elideString && priority > elidePrio) {
	    styleValues.elide = tagPtr->elide;
	    elidePrio = priority;
	}
	if (tagPtr->langPtr && priority > langPrio) {
	    styleValues.lang = tagPtr->lang;
	    langPrio = priority;
	}
	if (tagPtr->hyphenRulesPtr && priority > hyphenRulesPrio) {
	    styleValues.hyphenRules = tagPtr->hyphenRules;
	    hyphenRulesPrio = priority;
	}
	if (tagPtr->wrapMode != TEXT_WRAPMODE_NULL && priority > wrapPrio) {
	    /* assert(tagPtr->wrapMode < 8); always true due to range */
	    styleValues.wrapMode = tagPtr->wrapMode;
	    wrapPrio = priority;
	}
    }

    /*
     * Use an existing style if there's one around that matches.
     */

    hPtr = Tcl_CreateHashEntry(&textPtr->dInfoPtr->styleTable, (char *) &styleValues, (int *) &isNew);
    if (!isNew) {
	return Tcl_GetHashValue(hPtr);
    }

    /*
     * No existing style matched. Make a new one.
     */

    stylePtr = malloc(sizeof(TextStyle));
    stylePtr->refCount = 0;
    if (styleValues.border) {
	gcValues.foreground = Tk_3DBorderColor(styleValues.border)->pixel;
	mask = GCForeground;
	if (styleValues.bgStipple != None) {
	    gcValues.stipple = styleValues.bgStipple;
	    gcValues.fill_style = FillStippled;
	    mask |= GCStipple|GCFillStyle;
	}
	stylePtr->bgGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    } else {
	stylePtr->bgGC = None;
    }
    mask = GCFont;
    gcValues.font = Tk_FontId(styleValues.tkfont);
    mask |= GCForeground;
    if (styleValues.eolColor) {
	gcValues.foreground = styleValues.eolColor->pixel;
	stylePtr->eolGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    } else {
	stylePtr->eolGC = None;
    }
    if (styleValues.eotColor) {
	gcValues.foreground = styleValues.eotColor->pixel;
	stylePtr->eotGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    } else {
	stylePtr->eotGC = None;
    }
    if (styleValues.hyphenColor) {
	gcValues.foreground = styleValues.hyphenColor->pixel;
	stylePtr->hyphenGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    } else {
	stylePtr->hyphenGC = None;
    }
    gcValues.foreground = styleValues.fgColor->pixel;
    if (styleValues.fgStipple != None) {
	gcValues.stipple = styleValues.fgStipple;
	gcValues.fill_style = FillStippled;
	mask |= GCStipple|GCFillStyle;
    }
    stylePtr->fgGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    mask = GCForeground;
    gcValues.foreground = styleValues.underlineColor->pixel;
    stylePtr->ulGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    gcValues.foreground = styleValues.overstrikeColor->pixel;
    stylePtr->ovGC = Tk_GetGC(textPtr->tkwin, mask, &gcValues);
    stylePtr->sValuePtr = (StyleValues *) Tcl_GetHashKey(&textPtr->dInfoPtr->styleTable, hPtr);
    stylePtr->hPtr = hPtr;
    Tcl_SetHashValue(hPtr, stylePtr);
    DEBUG_ALLOC(tkTextCountNewStyle++);
    return stylePtr;
}

static TextStyle *
GetStyle(
    TkText *textPtr,		/* Overall information about text widget. */
    TkTextSegment *segPtr)	/* The text for which display information is wanted. */
{
    TextStyle *stylePtr;
    TkTextTag *tagPtr;
    bool containsSelection;

    if (segPtr && (tagPtr = TkBTreeGetSegmentTags(
		    textPtr->sharedTextPtr, segPtr, textPtr, &containsSelection))) {
	stylePtr = MakeStyle(textPtr, tagPtr, containsSelection);
    } else {
	/*
	 * Take into account that this function can be called before UpdateDefaultStyle
	 * has been called for the first time.
	 */
	if (!textPtr->dInfoPtr->defaultStyle) {
	    UpdateDefaultStyle(textPtr);
	}
	stylePtr = textPtr->dInfoPtr->defaultStyle;
    }

    stylePtr->refCount += 1;
    return stylePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateDefaultStyle --
 *
 *	This function is called if something has changed, and some DLines
 *	have to be updated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateDefaultStyle(
    TkText *textPtr)
{
    TextStyle *stylePtr = MakeStyle(textPtr, NULL, false);
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    if (stylePtr != dInfoPtr->defaultStyle) {
	if (dInfoPtr->defaultStyle) {
	    FreeStyle(textPtr, dInfoPtr->defaultStyle);
	}
	dInfoPtr->defaultStyle = stylePtr;
	stylePtr->refCount += 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeStyle --
 *
 *	This function is called when a TextStyle structure is no longer
 *	needed. It decrements the reference count and frees up the space for
 *	the style structure if the reference count is 0.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage and other resources associated with the style are freed up
 *	if no-one's still using it.
 *
 *----------------------------------------------------------------------
 */

static void
FreeStyle(
    TkText *textPtr,		/* Information about overall widget. */
    TextStyle *stylePtr)	/* Information about style to free. */
{
    assert(stylePtr);
    assert(stylePtr->refCount > 0);

    if (--stylePtr->refCount == 0) {
	if (stylePtr->bgGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->bgGC);
	}
	if (stylePtr->fgGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->fgGC);
	}
	if (stylePtr->ulGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->ulGC);
	}
	if (stylePtr->ovGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->ovGC);
	}
	if (stylePtr->eolGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->eolGC);
	}
	if (stylePtr->eotGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->eotGC);
	}
	if (stylePtr->hyphenGC != None) {
	    Tk_FreeGC(textPtr->display, stylePtr->hyphenGC);
	}
	Tcl_DeleteHashEntry(stylePtr->hPtr);
	free(stylePtr);
	DEBUG_ALLOC(tkTextCountDestroyStyle++);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * IsStartOfNotMergedLine --
 *
 *	This function checks whether the given index is the start of a
 *      logical line that is not merged with the previous logical line
 *      (due to elision of the eol of the previous line).
 *
 * Results:
 *	Returns whether the given index denotes the first index of a
 *      logical line not merged with its previous line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
IsStartOfNotMergedLine(
      const TkTextIndex *indexPtr)  /* Index to check. */
{
    return TkTextIndexGetLine(indexPtr)->logicalLine
	    ? TkTextIndexIsStartOfLine(indexPtr)
	    : TkTextIndexIsStartOfText(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * IsSameFGStyle --
 *
 *	Compare the foreground attributes of two styles. Specifically must
 *	consider: foreground color, font, font style and font decorations,
 *	elide, "offset" and foreground stipple. Do *not* consider: background
 *	color, border, relief or background stipple.
 *
 *	If we use TkpDrawCharsInContext, we also don't need to check
 *	foreground color, font decorations, elide, offset and foreground
 *	stipple, so all that is left is font (including font size and font
 *	style) and "offset".
 *
 * Results:
 *	'true' if the two styles match, 'false' otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#if TK_LAYOUT_WITH_BASE_CHUNKS

static bool
IsSameFGStyle(
    TextStyle *style1,
    TextStyle *style2)
{
    StyleValues *sv1;
    StyleValues *sv2;

    if (style1 == style2) {
	return true;
    }

    sv1 = style1->sValuePtr;
    sv2 = style2->sValuePtr;

    return sv1->tkfont == sv2->tkfont && sv1->offset == sv2->offset;
}

#endif /* TK_LAYOUT_WITH_BASE_CHUNKS */

/*
 *----------------------------------------------------------------------
 *
 * LayoutDLine --
 *
 *	This function generates a single DLine structure for a display line
 *	whose leftmost character is given by indexPtr.
 *
 * Results:
 *	The return value is a pointer to a DLine structure describing the
 *	display line. All fields are filled in and correct except for y and
 *	nextPtr.
 *
 * Side effects:
 *	Storage is allocated for the new DLine.
 *
 *	See the comments in 'GetYView' for some thoughts on what the side-
 *	effects of this call (or its callers) should be; the synchronisation
 *	of TkTextLine->pixelHeight with the sum of the results of this
 *	function operating on all display lines within each logical line.
 *	Ideally the code should be refactored to ensure the cached pixel
 *	height is never behind what is known when this function is called
 *	elsewhere.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
LayoutGetNextSegment(
    TkTextSegment *segPtr)
{
    while ((segPtr = segPtr->nextPtr)) {
	if (segPtr->typePtr == &tkTextCharType) {
	    return segPtr;
	}
	if (segPtr->typePtr == &tkTextBranchType) {
	    segPtr = segPtr->body.branch.nextPtr;
	}
    }
    return NULL;
}

static TkTextDispChunk *
LayoutGetNextCharChunk(
    TkTextDispChunk *chunkPtr)
{
    while ((chunkPtr = chunkPtr->nextPtr)) {
	switch (chunkPtr->layoutProcs->type) {
	case TEXT_DISP_CHAR:	return chunkPtr;
	case TEXT_DISP_WINDOW:	/* fallthru */
	case TEXT_DISP_IMAGE:	return NULL;
	case TEXT_DISP_HYPHEN:	/* fallthru */
	case TEXT_DISP_ELIDED:	/* fallthru */
	case TEXT_DISP_CURSOR:	break;
	}
    }
    return NULL;
}

static void
LayoutSetupDispLineInfo(
    TkTextPixelInfo *pixelInfo)
{
    TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;
    unsigned oldNumDispLines = TkBTreeGetNumberOfDisplayLines(pixelInfo);

    if (!dispLineInfo) {
	dispLineInfo = malloc(TEXT_DISPLINEINFO_SIZE(2));
	DEBUG(memset(dispLineInfo, 0xff, TEXT_DISPLINEINFO_SIZE(2)));
	DEBUG_ALLOC(tkTextCountNewDispInfo++);
	pixelInfo->dispLineInfo = dispLineInfo;
    }

    dispLineInfo->numDispLines = 1;
    /* remember old display line count, see TkBTreeGetNumberOfDisplayLines */
    dispLineInfo->entry[1].pixels = oldNumDispLines;
}

static void
LayoutUpdateLineHeightInformation(
    const LayoutData *data,
    DLine *dlPtr,
    TkTextLine *linePtr,	/* The corresponding logical line. */
    bool finished,		/* Did we finish the layout of a complete logical line? */
    int hyphenRule)		/* Applied hyphen rule; zero if no rule has been applied. */
{
    TkText *textPtr = data->textPtr;
    unsigned epoch = textPtr->dInfoPtr->lineMetricUpdateEpoch;
    TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);
    unsigned oldNumDispLines = TkBTreeGetNumberOfDisplayLines(pixelInfo);
    TkTextDispLineInfo *dispLineInfo;
    TkTextLine *nextLogicalLinePtr;

    assert(dlPtr->byteCount > 0);
    assert(linePtr->logicalLine);
    assert(linePtr == TkBTreeGetLogicalLine(
	    textPtr->sharedTextPtr, textPtr, TkTextIndexGetLine(&dlPtr->index)));

    if (pixelInfo->epoch == epoch) {
	int lineNo = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, NULL);

	if (TkRangeListContains(textPtr->dInfoPtr->lineMetricUpdateRanges, lineNo)) {
	    int mergedLines = 1;

	    nextLogicalLinePtr = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
	    if (linePtr->nextPtr != nextLogicalLinePtr) {
		mergedLines = TkBTreeCountLines(textPtr->sharedTextPtr->tree, linePtr,
			nextLogicalLinePtr) - 1;
	    }
	    TkRangeListRemove(textPtr->dInfoPtr->lineMetricUpdateRanges, lineNo, lineNo + mergedLines);
	}

	return; /* already up-to-date */
    }

    TK_TEXT_DEBUG(LogTextHeightCalc(textPtr, &dlPtr->index));
    dispLineInfo = pixelInfo->dispLineInfo;
    dlPtr->hyphenRule = hyphenRule;

    if (dlPtr->displayLineNo > 0) {
	TkTextDispLineEntry *dispLineEntry;

	assert(dispLineInfo);
	assert(data->byteOffset == dispLineInfo->entry[dlPtr->displayLineNo].byteOffset);

	if (dlPtr->displayLineNo >= dispLineInfo->numDispLines
		&& !IsPowerOf2(dlPtr->displayLineNo + 2)) {
	    unsigned size = NextPowerOf2(dlPtr->displayLineNo + 2);
	    dispLineInfo = realloc(dispLineInfo, TEXT_DISPLINEINFO_SIZE(size));
	    DEBUG(memset(dispLineInfo->entry + dlPtr->displayLineNo + 1, 0xff,
		    (size - dlPtr->displayLineNo - 1)*sizeof(dispLineInfo->entry[0])));
	    pixelInfo->dispLineInfo = dispLineInfo;
	}
	dispLineInfo->numDispLines = dlPtr->displayLineNo + 1;
	dispLineEntry = dispLineInfo->entry + dlPtr->displayLineNo;
	(dispLineEntry + 1)->byteOffset = data->byteOffset + dlPtr->byteCount;
	(dispLineEntry + 1)->pixels = oldNumDispLines;
	dispLineEntry->height = dlPtr->height;
	dispLineEntry->pixels = (dispLineEntry - 1)->pixels + dlPtr->height;
	dispLineEntry->byteOffset = data->byteOffset;
	dispLineEntry->hyphenRule = hyphenRule;
    } else if (!finished) {
	LayoutSetupDispLineInfo(pixelInfo);
	dispLineInfo = pixelInfo->dispLineInfo;
	dispLineInfo->entry[0].height = dlPtr->height;
	dispLineInfo->entry[0].pixels = dlPtr->height;
	dispLineInfo->entry[0].byteOffset = data->byteOffset;
	dispLineInfo->entry[0].hyphenRule = hyphenRule;
	dispLineInfo->entry[1].byteOffset = data->byteOffset + dlPtr->byteCount;
    }

    assert(finished || dispLineInfo);

    if (finished) {
	TkTextLine *nextLogicalLinePtr;
	unsigned lineHeight, mergedLines, lineNo, numDispLines, i;

	if (dlPtr->displayLineNo > 0) {
	    lineHeight = dispLineInfo->entry[dispLineInfo->numDispLines - 1].pixels;
	    numDispLines = dispLineInfo->numDispLines;
	} else {
	    lineHeight = dlPtr->height;
	    numDispLines = lineHeight > 0;
	}
	assert(linePtr->nextPtr);
	nextLogicalLinePtr = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
	mergedLines = TkBTreeCountLines(textPtr->sharedTextPtr->tree, linePtr, nextLogicalLinePtr);
	if (mergedLines > 0) {
	    mergedLines -= 1; /* subtract first line */
	}
	if (pixelInfo->height != lineHeight || mergedLines > 0 || numDispLines != oldNumDispLines) {
	    /*
	     * Do this B-Tree update before updating the epoch, because this action
	     * needs the old values.
	     */
	    TkBTreeAdjustPixelHeight(textPtr, linePtr, lineHeight, mergedLines, numDispLines);
	}
	if (dispLineInfo && dlPtr->displayLineNo == 0) {
	    /*
	     * This is the right place to destroy the superfluous dispLineInfo. Don't do
	     * this before TkBTreeAdjustPixelHeight has been called, because the latter
	     * function needs the old display line count.
	     */
	    free(dispLineInfo);
	    DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	    pixelInfo->dispLineInfo = NULL;
	}
	textPtr->dInfoPtr->lineMetricUpdateCounter += 1;
	pixelInfo->epoch = epoch;
	lineNo = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, NULL);
	for (i = 0; i < mergedLines; ++i) {
	    pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr = linePtr->nextPtr);
	    pixelInfo->epoch = epoch;
	    if (pixelInfo->dispLineInfo) {
		free(pixelInfo->dispLineInfo);
		DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
		pixelInfo->dispLineInfo = NULL;
	    }
	}
	TkRangeListRemove(textPtr->dInfoPtr->lineMetricUpdateRanges, lineNo, lineNo + mergedLines);
    } else {
	/*
	 * This line is wrapping into several display lines. We mark it as already
	 * up-to-date, even with a partial computation. This is the right way to
	 * handle very long lines efficiently, because with very long lines the chance
	 * will be high that the lookup into the cache succeeds even with a partial
	 * computation. (If the lookup fails, because the cache for this line is not
	 * yet complete, then the required remaining lines will be computed, and the
	 * result will also be stored in the cache, because all metric computation
	 * will be done with LayoutDLine, and this function is caching any computation.)
	 */

	pixelInfo->epoch = epoch | PARTIAL_COMPUTED_BIT;
    }
}

static unsigned
LayoutComputeBreakLocations(
    LayoutData *data)
{
    unsigned totalSize = 0;
    TkText *textPtr = data->textPtr;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextSegment *segPtr = data->logicalLinePtr->segPtr;
    bool useUniBreak = data->textPtr->useUniBreak;
    char const *lang = useUniBreak ? textPtr->lang : NULL;
    char const *nextLang = NULL;
    unsigned capacity = dInfoPtr->strBufferSize;
    char *str = dInfoPtr->strBuffer;
    char *brks = textPtr->brksBuffer;

    /*
     * The codepoint line break computation requires the whole logical line (due to a
     * poor design of libunibreak), but separated by languages, because this line break
     * algorithm is in general language dependent.
     */

    while (segPtr) {
	unsigned size = 0;
	unsigned newTotalSize;

	for ( ; segPtr; segPtr = segPtr->nextPtr) {
	    switch ((int) segPtr->typePtr->group) {
	    case SEG_GROUP_CHAR: {
		unsigned newSize;

		if (useUniBreak) {
		    const char *myLang = TkBTreeGetLang(textPtr, segPtr);

		    if (myLang[0] != lang[0] || myLang[1] != lang[1]) {
			nextLang = myLang;
			break;
		    }
		}
		if ((newSize = size + segPtr->size) >= capacity) {
		    capacity = MAX(2*capacity, newSize + 1);
		    str = realloc(str, newSize);
		}
		memcpy(str + size, segPtr->body.chars, segPtr->size);
		size = newSize;
		break;
	    }
	    case SEG_GROUP_HYPHEN:
		if (useUniBreak) {
		    const char *myLang = TkBTreeGetLang(textPtr, segPtr);

		    if (myLang[0] != lang[0] || myLang[1] != lang[1]) {
			nextLang = myLang;
			break;
		    }
		}
		if (size + 1 >= capacity) {
		    assert(2*capacity > size + 1);
		    str = realloc(str, capacity *= 2);
		}

		/*
		 * Use TAB (U+0009) instead of SHY (U+00AD), because SHY needs two bytes,
		 * but TAB needs only one byte, and this corresponds to the byte size of
		 * a hyphen segment. The TAB character has the same character class as
		 * the SHY character, so it's a proper substitution.
		 *
		 * NOTE: Do not use '-' (U+002D) for substitution, because the meaning
		 * of this character is contextual.
		 */

		str[size++] = '\t';
		break;
	    case SEG_GROUP_IMAGE:
	    case SEG_GROUP_WINDOW:
		/* The language variable doesn't matter here. */
		if (size + 1 >= capacity) {
		    assert(2*capacity > size + 1);
		    str = realloc(str, capacity *= 2);
		}
		/* Substitute with a TAB, so we can break at this point. */
		str[size++] = '\t';
		break;
	    case SEG_GROUP_BRANCH:
		segPtr = segPtr->body.branch.nextPtr;
	    	break;
	    }
	}
	if (size > 0) {
	    newTotalSize = totalSize + size;

	    if (newTotalSize > textPtr->brksBufferSize) {
		/*
		 * Take into account that the buffer must be a bit larger, because we need
		 * one additional byte for trailing NUL (see below).
		 */
		textPtr->brksBufferSize = MAX(newTotalSize, textPtr->brksBufferSize + 512);
		textPtr->brksBuffer = realloc(textPtr->brksBuffer, textPtr->brksBufferSize + 1);
		brks = textPtr->brksBuffer;
	    }

	    str[size] = '\0'; /* TkTextComputeBreakLocations expects traling nul */
	    TkTextComputeBreakLocations(data->textPtr->interp, str, size,
		    lang ? (*lang ? lang : "en") : NULL, brks + totalSize);
	    totalSize = newTotalSize;
	}
	lang = nextLang;
    }

    dInfoPtr->strBuffer = str;
    dInfoPtr->strBufferSize = capacity;

    return totalSize;
}

static void
LayoutLookAheadChars(
    TkTextDispChunk *chunkPtr,
    const char *str,
    unsigned numChars,
    char *buf)
{
    TkTextSegment *segPtr = ((CharInfo *) chunkPtr->clientData)->segPtr;

    for ( ; numChars > 0; --numChars) {
	if (!*str) {
	    segPtr = LayoutGetNextSegment(segPtr);
	    if (!segPtr) {
		memset(buf, '\0', numChars);
		return;
	    }
	    str = segPtr->body.chars;
	}
	*buf++ = *str++;
    }
}

static void
LayoutApplyHyphenRules(
    LayoutData *data,
    TkTextDispChunk *prevCharChunkPtr,
    TkTextDispChunk *hyphenChunkPtr,
    TkTextDispChunk *nextCharChunkPtr)
{
    TkTextSegment *hyphenPtr = hyphenChunkPtr->clientData;
    const StyleValues *sValPtr = hyphenChunkPtr->stylePtr->sValuePtr;
    int hyphenRules = sValPtr->hyphenRules & hyphenChunkPtr->hyphenRules;

    data->increaseNumBytes = 0;
    data->decreaseNumBytes = 0;
    SetupHyphenChars(hyphenPtr, 0);
    hyphenRules = FilterHyphenRules(hyphenRules, sValPtr->lang);

    if (hyphenRules) {
	const CharInfo *prevCiPtr;
	const CharInfo *nextCiPtr;
	const char *prevCharPtr;
	const char *nextCharPtr;
	unsigned char prevChar;
	unsigned char nextChar;
	char lookAhead[3];

	if (hyphenRules & (1 << TK_TEXT_HYPHEN_REPEAT)) {
	    data->increaseNumBytes = -1;
	    data->hyphenRule = TK_TEXT_HYPHEN_REPEAT;
	    return;
	}

	if (!IsCharChunk(prevCharChunkPtr)) {
	    return;
	}

	while ((prevCiPtr = prevCharChunkPtr->clientData)->numBytes == 0) {
	    if (!(prevCharChunkPtr = prevCharChunkPtr->prevCharChunkPtr)
		    || !IsCharChunk(prevCharChunkPtr)) {
		return;
	    }
	}
	prevCharPtr = prevCiPtr->u.chars + prevCiPtr->baseOffset + prevCiPtr->numBytes - 1;

	/*
	 * We know that we have to inspect only Latin-1 characters, either
	 * from ASCII code page (< 0x80), or starting with 0xc3.
	 */

	if (UCHAR(prevCharPtr[0]) < 0x80) {
	    prevChar = UCHAR(prevCharPtr[0]);
	} else if (prevCiPtr->numBytes > 1 && UCHAR(prevCharPtr[-1]) == 0xc3) {
	    prevChar = ConvertC3Next(prevCharPtr[1]);
	} else {
	    return;
	}

	if (hyphenRules & (1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL)) {
	    /* op(aa-)tje  -> op(a-)tje */
	    /* caf(ee-)tje -> caf(é-)tje */
	    if (IsVowel(prevChar)) {
		char secondPrevChar = '\0';

		if (prevCiPtr->numBytes > 1) {
		    secondPrevChar = prevCharPtr[-1];
		} else {
		    const TkTextDispChunk *chunkPtr = prevCharChunkPtr->prevCharChunkPtr;
		    if (chunkPtr && IsCharChunk(chunkPtr)) {
			const TkTextSegment *segPtr = CHAR_CHUNK_GET_SEGMENT(chunkPtr);
			secondPrevChar = segPtr->body.chars[segPtr->size - 1];
		    }
		}
		if (prevChar == secondPrevChar) {
		    if (prevChar == 'e') {
			char *s = hyphenPtr->body.chars; /* this avoids warnings */
			data->decreaseNumBytes = 2;
			s[0] = 0xc3; s[1] = 0xa9; /* 'é' = U+00E9 */
			SetupHyphenChars(hyphenPtr, 2);
		    } else {
			data->decreaseNumBytes = 1;
		    }
		    data->hyphenRule = TK_TEXT_HYPHEN_DOUBLE_VOWEL;
		    return;
		}
	    }
	}

	if (!IsCharChunk(nextCharChunkPtr)) {
	    return;
	}
	if ((nextCiPtr = nextCharChunkPtr->clientData)->numBytes == 0) {
	    TkTextSegment *segPtr = LayoutGetNextSegment(nextCharChunkPtr->clientData);
	    if (!segPtr) {
		return;
	    }
	    nextCharPtr = segPtr->body.chars;
	} else {
	    nextCharPtr = nextCiPtr->u.chars + nextCiPtr->baseOffset;
	}
	if (UCHAR(nextCharPtr[0]) < 0x80) {
	    nextChar = UCHAR(nextCharPtr[0]);
	} else if (UCHAR(nextCharPtr[0]) == 0xc3) {
	    nextChar = ConvertC3Next(nextCharPtr[1]);
	} else {
	    return;
	}

	if (hyphenRules & (1 << TK_TEXT_HYPHEN_CK)) {
	    /* Dru(c-k)er -> Dru(k-k)er */
	    if (prevChar == UCHAR('c') && nextChar == UCHAR('k')) {
		data->decreaseNumBytes = 1;
		hyphenPtr->body.chars[0] = 'k';
		SetupHyphenChars(hyphenPtr, 1);
		data->hyphenRule = TK_TEXT_HYPHEN_CK;
		return;
	    }
	}
	if (hyphenRules & (1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH)) {
	    /* vi(s-sz)a -> vi(sz-sz)a */
	    if (prevChar == nextChar) {
		LayoutLookAheadChars(nextCharChunkPtr, nextCharPtr + 1, 1, lookAhead);
		if (lookAhead[0] && IsDoubleDigraph(prevChar, lookAhead[0])) {
		    hyphenPtr->body.chars[0] = lookAhead[0];
		    SetupHyphenChars(hyphenPtr, 1);
		    data->hyphenRule = TK_TEXT_HYPHEN_DOUBLE_DIGRAPH;
		    return;
		}
	    }
	}
	if (hyphenRules & (1 << TK_TEXT_HYPHEN_TREMA)) {
	    /* r(e-ëe)l -> r(e-ee)l */
	    if (IsVowel(prevChar) && IsUmlaut(nextChar)) {
		data->hyphenRule = TK_TEXT_HYPHEN_TREMA;
		return;
	    }
	}
	if (hyphenRules & (1 << TK_TEXT_HYPHEN_GEMINATION)) {
	    /* para(-l·l)el -> para(l-l)el */
	    if (tolower(nextChar) == 'l') {
		LayoutLookAheadChars(nextCharChunkPtr, nextCharPtr + 1, 3, lookAhead);
		/* test for U+00B7 = MIDDOT */
		if (UCHAR(lookAhead[0]) == 0xc2
			&& UCHAR(lookAhead[1]) == 0xb7
			&& lookAhead[2] == nextChar) {
		    data->increaseNumBytes = 3;
		    hyphenPtr->body.chars[0] = nextChar;
		    SetupHyphenChars(hyphenPtr, 1);
		    data->hyphenRule = TK_TEXT_HYPHEN_GEMINATION;
		    return;
		}
	    }
	}
    }
}

static unsigned
LayoutMakeCharInfo(
    LayoutData *data,
    TkTextSegment *segPtr,
    int byteOffset,
    int maxBytes)
{
    char const *p = segPtr->body.chars + byteOffset;
    CharInfo *ciPtr = AllocCharInfo(data->textPtr);

    assert(data->chunkPtr);
    assert(!data->chunkPtr->clientData);

    /*
     * Take into account that maxBytes == 0 is possible.
     */

    if (data->trimSpaces && maxBytes > 0 && p[maxBytes - 1] == ' ') {
	while (maxBytes > 1 && p[maxBytes - 2] == ' ') {
	    maxBytes -= 1;
	}
    }

#if TK_LAYOUT_WITH_BASE_CHUNKS

    if (data->baseChunkPtr
	    && (!IsSameFGStyle(data->baseChunkPtr->stylePtr, data->chunkPtr->stylePtr)
	    	|| (data->lastCharChunkPtr && data->lastCharChunkPtr->numSpaces > 0))) {
	data->baseChunkPtr = NULL;
    }

    if (!data->baseChunkPtr) {
	data->baseChunkPtr = data->chunkPtr;
	Tcl_DStringInit(&data->chunkPtr->baseChars);
	DEBUG_ALLOC(tkTextCountNewBaseChars++);
    }

    data->chunkPtr->baseChunkPtr = data->baseChunkPtr;
    ciPtr->baseOffset = Tcl_DStringLength(&data->baseChunkPtr->baseChars);
    ciPtr->u.chars = Tcl_DStringAppend(&data->baseChunkPtr->baseChars, p, maxBytes);

#else

    ciPtr->baseOffset = 0;
    ciPtr->u.chars = p;

#endif

    /*
     * Keep the char segment, otherwise a split may invalidate our string. This segment
     * is also used for hyphenation support.
     */

    segPtr->refCount += 1;
    ciPtr->segPtr = segPtr;
    ciPtr->numBytes = maxBytes;
    data->chunkPtr->clientData = ciPtr;

    return maxBytes;
}

static void
LayoutFinalizeCharInfo(
    LayoutData *data,
    bool gotTab)
{
    CharInfo *ciPtr = data->chunkPtr->clientData;

    assert(data->trimSpaces ?
	    (int) data->chunkPtr->numBytes >= ciPtr->numBytes :
	    (int) data->chunkPtr->numBytes == ciPtr->numBytes);

    /*
     * Update the character information. Take into account that we don't want
     * to display the newline character.
     */

    if (ciPtr->u.chars[ciPtr->baseOffset + ciPtr->numBytes - 1] == '\n') {
	ciPtr->numBytes -= 1;
    }

#if TK_LAYOUT_WITH_BASE_CHUNKS

    assert(data->chunkPtr->baseChunkPtr);

    /*
     * Final update for the current base chunk data.
     */

    Tcl_DStringSetLength(&data->baseChunkPtr->baseChars, ciPtr->baseOffset + ciPtr->numBytes);
    data->baseChunkPtr->baseWidth =
	    data->chunkPtr->width + (data->chunkPtr->x - data->baseChunkPtr->x);

    /*
     * Finalize the base chunk if this chunk ends in a tab, which definitly breaks the context.
     */

    if (gotTab) {
	data->baseChunkPtr = NULL;
    }

#endif
}

static void
LayoutUndisplay(
    LayoutData *data,
    TkTextDispChunk *chunkPtr)
{
    assert(chunkPtr->layoutProcs);

    if (chunkPtr->layoutProcs->undisplayProc) {
	chunkPtr->layoutProcs->undisplayProc(data->textPtr, chunkPtr);
    }
#if TK_LAYOUT_WITH_BASE_CHUNKS
    if (chunkPtr == data->baseChunkPtr) {
	data->baseChunkPtr = NULL;
    }
#endif
}

static void
LayoutReleaseChunk(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr)
{
    if (chunkPtr->layoutProcs) {
	if (chunkPtr->layoutProcs->type == TEXT_DISP_IMAGE) {
	    textPtr->dInfoPtr->countImages -= 1;
	} else if (chunkPtr->layoutProcs->type == TEXT_DISP_WINDOW) {
	    textPtr->dInfoPtr->countWindows -= 1;
	}
    }
    FreeStyle(textPtr, chunkPtr->stylePtr);
}

static void
LayoutFreeChunk(
    LayoutData *data)
{
    TextDInfo *dInfoPtr = data->textPtr->dInfoPtr;
    TkTextDispChunk *chunkPtr = data->chunkPtr;

    assert(chunkPtr);
    assert(data->lastChunkPtr != chunkPtr);
    assert(data->lastCharChunkPtr != chunkPtr);
    assert(!chunkPtr->sectionPtr);

    if (chunkPtr->layoutProcs) {
	LayoutUndisplay(data, chunkPtr);
    }

    LayoutReleaseChunk(data->textPtr, chunkPtr);
    DEBUG(chunkPtr->stylePtr = NULL);
    assert(!chunkPtr->clientData);
    data->numBytesSoFar -= chunkPtr->numBytes;
    chunkPtr->nextPtr = dInfoPtr->chunkPoolPtr;
    dInfoPtr->chunkPoolPtr = chunkPtr;
    dInfoPtr->chunkPoolPtr->prevPtr = NULL;
    data->chunkPtr = NULL;
    assert(data->countChunks > 0);
    data->countChunks -= 1;
}

static void
LayoutDoWidthAdjustmentForContextDrawing(
    LayoutData *data)
{
#if TK_LAYOUT_WITH_BASE_CHUNKS && TK_DRAW_IN_CONTEXT
    TkTextDispChunk *chunkPtr = data->chunkPtr;

    if (chunkPtr->prevPtr) {
	chunkPtr->x += chunkPtr->prevPtr->xAdjustment;
    }

    if (IsCharChunk(chunkPtr)) {
	int newWidth;

	CharChunkMeasureChars(chunkPtr, NULL, 0, 0, -1, 0, -1, 0, &newWidth);
	chunkPtr->xAdjustment = newWidth - chunkPtr->width;
	chunkPtr->width = newWidth;
    }
#endif
}

static void
LayoutFinalizeChunk(
    LayoutData *data)
{
    const TkTextDispChunkProcs *layoutProcs;

    if (!data->chunkPtr) {
	return;
    }

    layoutProcs = data->chunkPtr->layoutProcs;

    if (!layoutProcs) {
	assert(data->chunkPtr->numBytes == 0);
	assert(!data->chunkPtr->clientData);
	LayoutFreeChunk(data);
	return;
    }

    if (layoutProcs->type & TEXT_DISP_CONTENT) {
	data->lastCharChunkPtr = data->chunkPtr;
	if (!data->firstCharChunkPtr) {
	    data->firstCharChunkPtr = data->chunkPtr;
	}
	if (layoutProcs->type & TEXT_DISP_TEXT) {
	    LayoutDoWidthAdjustmentForContextDrawing(data);
	}
    }
    if (data->chunkPtr->breakIndex > 0) {
	data->breakChunkPtr = data->chunkPtr;
    }
    if (!data->firstChunkPtr) {
	assert(!data->lastChunkPtr);
	data->firstChunkPtr = data->chunkPtr;
    } else {
	assert(data->lastChunkPtr);
	data->lastChunkPtr->nextPtr = data->chunkPtr;
    }
    data->lastChunkPtr = data->chunkPtr;
    data->dispLineOffset += data->chunkPtr->numBytes;
    data->chunkPtr = NULL;
}

static TkTextDispChunkSection *
LayoutNewSection(
    TextDInfo *dInfoPtr)
{
    TkTextDispChunkSection *sectionPtr = dInfoPtr->sectionPoolPtr;

    if (sectionPtr) {
	dInfoPtr->sectionPoolPtr = dInfoPtr->sectionPoolPtr->nextPtr;
    } else {
	DEBUG_ALLOC(tkTextCountNewSection++);
	sectionPtr = malloc(sizeof(TkTextDispChunkSection));
    }

    memset(sectionPtr, 0, sizeof(TkTextDispChunkSection));
    return sectionPtr;
}

static void
LayoutMakeNewChunk(
    LayoutData *data)
{
    TkTextDispChunk *newChunkPtr;
    TextDInfo *dInfoPtr = data->textPtr->dInfoPtr;

    LayoutFinalizeChunk(data);
    if ((newChunkPtr = dInfoPtr->chunkPoolPtr)) {
	dInfoPtr->chunkPoolPtr = newChunkPtr->nextPtr;
    } else {
	newChunkPtr = malloc(sizeof(TkTextDispChunk));
	DEBUG_ALLOC(tkTextCountNewChunk++);
    }
    memset(newChunkPtr, 0, sizeof(TkTextDispChunk));
    newChunkPtr->dlPtr = data->dlPtr;
    newChunkPtr->uniqID = dInfoPtr->chunkCounter++;
    newChunkPtr->prevPtr = data->lastChunkPtr;
    newChunkPtr->prevCharChunkPtr = data->lastCharChunkPtr;
    newChunkPtr->stylePtr = GetStyle(data->textPtr, NULL);
    newChunkPtr->x = data->x;
    newChunkPtr->byteOffset = data->dispLineOffset;
    data->chunkPtr = newChunkPtr;
    data->countChunks += 1;
}

static void
LayoutSkipBytes(
    LayoutData *data,
    DLine *dlPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2)
{
    LayoutMakeNewChunk(data);
    data->chunkPtr->layoutProcs = &layoutElideProcs;
    data->chunkPtr->numBytes = TkTextIndexCountBytes(indexPtr1, indexPtr2);
}

static void
LayoutSetupChunk(
    LayoutData *data,
    TkTextSegment *segPtr)
{
    TkTextDispChunk *chunkPtr = data->chunkPtr;
    TkText *textPtr = data->textPtr;
    TextStyle *stylePtr;

    assert(segPtr->tagInfoPtr);
    assert(chunkPtr->stylePtr == textPtr->dInfoPtr->defaultStyle);
    assert(chunkPtr->stylePtr->refCount > 1);

    chunkPtr->stylePtr->refCount -= 1;
    chunkPtr->stylePtr = stylePtr = GetStyle(textPtr, segPtr);

    if (data->wrapMode == TEXT_WRAPMODE_CODEPOINT) {
	const TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, data->logicalLinePtr);

	if (!data->brks) {
	    Tcl_HashEntry *hPtr;
	    TkTextBreakInfo *breakInfo;
	    int new;

	    hPtr = Tcl_CreateHashEntry(&textPtr->sharedTextPtr->breakInfoTable,
		    (void *) data->logicalLinePtr, &new);

	    if (new) {
		breakInfo = malloc(sizeof(TkTextBreakInfo));
		breakInfo->refCount = 1;
		breakInfo->brks = NULL;
		data->logicalLinePtr->changed = false;
		Tcl_SetHashValue(hPtr, breakInfo);
		DEBUG_ALLOC(tkTextCountNewBreakInfo++);
	    } else {
		breakInfo = Tcl_GetHashValue(hPtr);
		breakInfo->refCount += 1;

		/*
		 * We have to avoid repeated computations of line break information,
		 * so we use the 'changed' flag of the logical line for the determination
		 * whether a recomputation has to be performed. This is the only purpose
		 * of flag 'changed', and required because our current line break
		 * information algorithm has to process the whole logical line. If this
		 * behavior will change - for example a switch to the ICU library - then
		 * flag 'changed' has no use anymore and can be removed. But currently
		 * all line modifications have to update this flag.
		 */

		if (data->logicalLinePtr->changed) {
		    new = true;
		    data->logicalLinePtr->changed = false;
		}
	    }

	    if (new) {
		unsigned brksSize;

		/*
		 * In this case we have to parse the whole logical line for the computation
		 * of the break locations.
		 */

		brksSize = LayoutComputeBreakLocations(data);
		breakInfo->brks = realloc(breakInfo->brks, brksSize);
		memcpy(breakInfo->brks, textPtr->brksBuffer, brksSize);
		DEBUG(stats.breakInfo += 1);
	    }

	    data->breakInfo = breakInfo;
	    data->brks = breakInfo->brks;
	}

	if (segPtr->sectionPtr) {
	    chunkPtr->brks = data->brks;
	    if (data->displayLineNo > 0) {
		assert(pixelInfo->dispLineInfo);
		chunkPtr->brks += pixelInfo->dispLineInfo->entry[data->displayLineNo].byteOffset;
	    } else {
		/* Consider that inside peers the line may start after byte index zero. */
		chunkPtr->brks += data->byteOffset;
	    }
	    chunkPtr->brks += data->dispLineOffset;
	} else {
	    /* This is an artificial chunk for the realization of spelling changes. */
	    assert(chunkPtr->numBytes <= sizeof(doNotBreakAtAll));
	    chunkPtr->brks = doNotBreakAtAll;
	}
    }

    if (data->numBytesSoFar == 0) {
	const TextDInfo *dInfoPtr = textPtr->dInfoPtr;
	const StyleValues *sValuePtr = stylePtr->sValuePtr;

	data->tabArrayPtr = sValuePtr->tabArrayPtr;
	data->tabStyle = sValuePtr->tabStyle;
	data->justify = sValuePtr->justify;
	data->rMargin = sValuePtr->rMargin;
	data->wrapMode = sValuePtr->wrapMode;
	data->x = data->paragraphStart ? sValuePtr->lMargin1 : sValuePtr->lMargin2;
	data->width = dInfoPtr->maxX - dInfoPtr->x - data->rMargin;
	data->maxX = data->wrapMode == TEXT_WRAPMODE_NONE ? -1 : MAX(data->width, data->x);

	chunkPtr->x = data->x;

	if (data->cursorChunkPtr) {
	    data->cursorChunkPtr->x = data->x;
	}
    }
}

static bool
LayoutChars(
    LayoutData *data,
    TkTextSegment *segPtr,
    int size,
    int byteOffset)
{
    const char *base = segPtr->body.chars + byteOffset;
    TkTextDispChunk *chunkPtr;
    bool gotTab = false;
    unsigned maxBytes;
    unsigned numBytes;

    assert(size - byteOffset > 0); /* this will ensure maxBytes > 0 */
    assert(byteOffset < size);
    assert(segPtr->typePtr->layoutProc);

    LayoutMakeNewChunk(data);
    LayoutSetupChunk(data, segPtr);

    chunkPtr = data->chunkPtr;
    maxBytes = size - byteOffset;

    if (data->textPtr->showEndOfLine
	    && base[maxBytes - 1] == '\n'
	    && (data->textPtr->showEndOfText
		|| segPtr->sectionPtr->linePtr->nextPtr != TkBTreeGetLastLine(data->textPtr))) {
	maxBytes -= 1; /* now may beome zero */
    }

    if (maxBytes == 0) {
	/*
	 * Can only happen if we are at end of logical line.
	 */

	if (segPtr->sectionPtr->linePtr->nextPtr != TkBTreeGetLastLine(data->textPtr)) {
	    segPtr = data->textPtr->dInfoPtr->endOfLineSegPtr;
	} else {
	    segPtr = data->textPtr->dInfoPtr->endOfTextSegPtr;
	}
	base = segPtr->body.chars;
	maxBytes = segPtr->size;
	chunkPtr->endOfLineSymbol = 1;
	byteOffset = 0;
    } else if (segPtr->typePtr != &tkTextHyphenType
		&& segPtr->sectionPtr) { /* ignore artifical segments (spelling changes) */
	if (data->wrapMode == TEXT_WRAPMODE_CODEPOINT) {
	    const char *brks = chunkPtr->brks;
	    unsigned i;

	    assert(brks);

	    for (i = 1; i < maxBytes; ++i) {
		if (brks[i] == LINEBREAK_MUSTBREAK) {
		    if (i < maxBytes - 2 && base[i] != '\n') {
			maxBytes = i + 1;
		    }
		    break;
		}
	    }
	}

	if (data->textPtr->hyphenate) {
	    const char *p = base;

	    /*
	     * Check whether the "tripleconsonant" rule has to be applied. This rule is
	     * very special, because in this case we are virtually doing the opposite.
	     * Instead of doing a spelling change when hyphenating, we are doing a spelling
	     * change when *not* hyphenating.
	     */

	    if (IsConsonant(*p)
		    && data->lastCharChunkPtr
		    && data->lastCharChunkPtr->prevCharChunkPtr
		    && data->lastChunkPtr
		    && data->lastChunkPtr->layoutProcs
		    && data->lastChunkPtr->layoutProcs->type == TEXT_DISP_HYPHEN
		    && *p == GetLastCharInChunk(data->lastCharChunkPtr->prevCharChunkPtr)
		    && *p == GetSecondLastCharInChunk(data->lastCharChunkPtr->prevCharChunkPtr)) {
		const char *nextCharPtr;

		if (maxBytes > 1) {
		    nextCharPtr = p + 1;
		} else {
		    const TkTextSegment *nextCharSegPtr = LayoutGetNextSegment(segPtr);
		    nextCharPtr = nextCharSegPtr ? nextCharSegPtr->body.chars : NULL;
		}

		/* For Norwegian it's required to consider 'j' as a vowel. */
		if (nextCharPtr && (nextCharPtr[0] == 'j' || IsUmlautOrVowel(nextCharPtr))) {
		    /*
		     * Probably we have to apply hyphen rule "tripleconsonant" to the first
		     * character after possible (but unapplied) hyphenation point.
		     */

		    const StyleValues *sValPtr = data->lastChunkPtr->stylePtr->sValuePtr;
		    int hyphenRules = FilterHyphenRules(sValPtr->hyphenRules, sValPtr->lang);

		    if (hyphenRules & (1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT)) {
			/* Schi(ff-f)ahrt -> Schi(ff)ahrt */
			byteOffset += 1;
			base += 1;
			maxBytes -= 1; /* now may become zero */
			chunkPtr->skipFirstChar = true;
		    }
		}
	    }
	}

	if (data->trimSpaces) {
	    unsigned i;

	    for (i = 0; i < maxBytes; ++i) {
		if (base[i] == ' ' && base[i + 1] == ' ') {
		    while (base[i] == ' ') {
			++i;
		    }
		    maxBytes = i;
		    data->skipSpaces = true;
		    break;
		}
	    }
	}

	/*
	 * See if there is a tab in the current chunk; if so, only layout
	 * characters up to (and including) the tab.
	 */

	if (data->justify == TK_TEXT_JUSTIFY_LEFT) {
	    const char *p = base;
	    unsigned i;

	    /* TODO: also TK_TEXT_JUSTIFY_RIGHT should support tabs */
	    /* TODO: direction of tabs should depend on gravity of insert mark?! */

	    for (i = 0; i < maxBytes; ++i, ++p) {
		if (*p == '\t') {
		    maxBytes = i + 1;
		    gotTab = true;
		    break;
		}
	    }
	} else if (data->justify == TK_TEXT_JUSTIFY_FULL) {
	    const char *p = base;
	    const char *e = p + maxBytes;

	    for ( ; p < e && !IsExpandableSpace(p); ++p) {
		if (*p == '\t') {
		    chunkPtr->numSpaces = 0;
		    maxBytes = p - base + 1;
		    gotTab = true;
		    break;
		}
	    }
	    if (!gotTab && p < e) {
		assert(IsExpandableSpace(p));

		do {
		    chunkPtr->numSpaces += 1;

		    if (*p == '\t'
			    && (!data->tabArrayPtr || data->tabIndex < data->tabArrayPtr->numTabs)) {
			/*
			 * Don't expand spaces if we have numeric tabs.
			 */
			chunkPtr->numSpaces = 0;
			gotTab = true;
			p += 1;
			break;
		    }

		    p = Tcl_UtfNext(p);
		} while (IsExpandableSpace(p));

		maxBytes = p - base;
	    }
	}
    }

    if (maxBytes == 0) {
	/*
	 * In seldom cases, if hyphenation is activated, we may have an empty
	 * chunk here, caused by the "tripleconsonant" rule. This chunk has to
	 * consume one character.
	 */

	assert(size == 1);
	assert(chunkPtr->skipFirstChar);
	data->chunkPtr->layoutProcs = &layoutElideProcs;
	data->chunkPtr->numBytes = 1;
	return true;
    }

    numBytes = LayoutMakeCharInfo(data, segPtr, byteOffset, maxBytes);

    if (segPtr->typePtr->layoutProc(&data->index, segPtr, byteOffset,
	    data->maxX - data->tabSize, numBytes, data->numBytesSoFar == 0,
	    data->wrapMode, data->textPtr->spaceMode, chunkPtr) == 0) {
	/*
	 * No characters from this segment fit in the window: this means
	 * we're at the end of the display line.
	 */

	chunkPtr->numSpaces = 0;
	return false;
    }

    if (numBytes == chunkPtr->numBytes) {
	chunkPtr->numBytes = maxBytes;
	assert(maxBytes > 0);

	if (data->trimSpaces && base[maxBytes - 1] == ' ') {
	    data->skipSpaces = true;
	}
    }

    assert(chunkPtr->numBytes + chunkPtr->skipFirstChar > 0);

    LayoutFinalizeCharInfo(data, gotTab);
    data->x += chunkPtr->width;

    if (segPtr == data->textPtr->dInfoPtr->endOfLineSegPtr) {
	chunkPtr->numBytes = (chunkPtr->numBytes == maxBytes) ? 1 : 0;
	chunkPtr->breakIndex = chunkPtr->numBytes;
	maxBytes = 1;
    } else {
	chunkPtr->numBytes += chunkPtr->skipFirstChar;
    }

    data->numBytesSoFar += chunkPtr->numBytes;
    data->numSpaces += chunkPtr->numSpaces;

    if (chunkPtr->numBytes != maxBytes + chunkPtr->skipFirstChar) {
	return false;
    }

    /*
     * If we're at a new tab, adjust the layout for all the chunks pertaining to the
     * previous tab. Also adjust the amount of space left in the line to account for
     * space that will be eaten up by the tab.
     */

    if (gotTab) {
	if (data->tabIndex >= 0) {
	    data->lastChunkPtr->nextPtr = data->chunkPtr; /* we need the complete chain. */
	    AdjustForTab(data);
	    data->lastChunkPtr->nextPtr = NULL; /* restore */
	    data->x = chunkPtr->x + chunkPtr->width;
	}
	data->tabChunkPtr = chunkPtr;
	ComputeSizeOfTab(data);
	if (data->maxX >= 0 && data->tabSize >= data->maxX - data->x) {
	    return false; /* end of line reached */
	}
    }

    return true;
}

static bool
LayoutHyphen(
    LayoutData *data,
    TkTextSegment *segPtr)
{
    bool rc;

    assert(segPtr->sectionPtr); /* don't works with artificial segments */

    if (data->textPtr->hyphenate) {
	LayoutMakeNewChunk(data);
	LayoutSetupChunk(data, segPtr);
	data->numBytesSoFar += segPtr->size;
	segPtr->body.hyphen.textSize = 0;
	data->chunkPtr->layoutProcs = &layoutHyphenProcs;
	data->chunkPtr->clientData = segPtr;
	data->chunkPtr->breakIndex = -1;
	data->chunkPtr->numBytes = segPtr->size;
	data->chunkPtr->hyphenRules = segPtr->body.hyphen.rules;
	segPtr->refCount += 1;
	rc = true;
    } else {
	SetupHyphenChars(segPtr, 0);
	rc = LayoutChars(data, segPtr, segPtr->body.hyphen.textSize, 0);
	data->chunkPtr->numBytes = MIN(1u, data->chunkPtr->numBytes);
    }

    data->chunkPtr->breakIndex = data->chunkPtr->numBytes;
    return rc;
}

static bool
LayoutEmbedded(
    LayoutData *data,
    TkTextSegment *segPtr)
{
    assert(segPtr->typePtr->layoutProc);

    LayoutMakeNewChunk(data);

    if (segPtr->typePtr->layoutProc(&data->index, segPtr, 0, data->maxX - data->tabSize, 0,
	    data->numBytesSoFar == 0, data->wrapMode, data->textPtr->spaceMode, data->chunkPtr) != 1) {
	return false;
    }

#if TK_LAYOUT_WITH_BASE_CHUNKS
    data->baseChunkPtr = NULL;
#endif
    LayoutSetupChunk(data, segPtr);
    data->numBytesSoFar += data->chunkPtr->numBytes;
    data->x += data->chunkPtr->width;

    if (segPtr->typePtr->group == SEG_GROUP_IMAGE) {
	data->textPtr->dInfoPtr->countImages += 1;
    } else {
	data->textPtr->dInfoPtr->countWindows += 1;
    }

    return true;
}

static bool
LayoutMark(
    LayoutData *data,
    TkTextSegment *segPtr)
{
    assert(segPtr->typePtr->layoutProc);

    if (segPtr != data->textPtr->insertMarkPtr) {
	return false;
    }
    LayoutMakeNewChunk(data);
    segPtr->typePtr->layoutProc(&data->index, segPtr, 0, data->maxX - data->tabSize, 0,
	    data->numBytesSoFar == 0, data->wrapMode, data->textPtr->spaceMode, data->chunkPtr);
    return true;
}

static bool
LayoutLogicalLine(
    LayoutData *data,
    DLine *dlPtr)
{
    TkTextSegment *segPtr, *endPtr;
    int byteIndex, byteOffset;

    assert(!TkTextIsElided(&data->index));

    byteIndex = TkTextIndexGetByteIndex(&data->index);

    if (data->textPtr->hyphenate && data->displayLineNo > 0) {
	const TkTextDispLineInfo *dispLineInfo;
	int byteOffset;
	int hyphenRule;

	segPtr = TkTextIndexGetContentSegment(&data->index, &byteOffset);
	dispLineInfo = TkBTreeLinePixelInfo(data->textPtr, data->logicalLinePtr)->dispLineInfo;
	assert(dispLineInfo);
	hyphenRule = dispLineInfo->entry[data->displayLineNo - 1].hyphenRule;

	switch (hyphenRule) {
	case TK_TEXT_HYPHEN_REPEAT:
	case TK_TEXT_HYPHEN_TREMA:
	case TK_TEXT_HYPHEN_DOUBLE_DIGRAPH: {
	    int numBytes = 0; /* prevents compiler warning */
	    TkTextSegment *nextCharSegPtr;
	    char buf[1];
	    bool cont;

	    /*
	     * We have to realize spelling changes.
	     */

	    switch (hyphenRule) {
	    case TK_TEXT_HYPHEN_REPEAT:
		buf[0] = '-';
		numBytes = 1;
		break;
	    case TK_TEXT_HYPHEN_TREMA:
		assert(UCHAR(segPtr->body.chars[byteOffset]) == 0xc3);
		buf[0] = UmlautToVowel(ConvertC3Next(segPtr->body.chars[byteOffset + 1]));
		numBytes = 2;
		break;
	    case TK_TEXT_HYPHEN_DOUBLE_DIGRAPH:
		buf[0] = segPtr->body.chars[0];
		numBytes = 1;
		break;
	    }
	    nextCharSegPtr = TkBTreeMakeCharSegment(buf, 1, segPtr->tagInfoPtr);
	    cont = LayoutChars(data, nextCharSegPtr, 1, 0);
	    TkBTreeFreeSegment(nextCharSegPtr);
	    data->chunkPtr->numBytes = numBytes;
	    if (!cont) {
		LayoutFinalizeChunk(data);
		return false;
	    }
	    TkTextIndexForwBytes(data->textPtr, &data->index, data->chunkPtr->numBytes, &data->index);
	    byteIndex += data->chunkPtr->numBytes;
	    break;
	}
	}
    }

    segPtr = TkTextIndexGetFirstSegment(&data->index, &byteOffset);
    endPtr = data->textPtr->endMarker;

    if (segPtr->typePtr == &tkTextLinkType) {
	segPtr = segPtr->nextPtr;
    }

    /*
     * Each iteration of the loop below creates one TkTextDispChunk for the
     * new display line. The line will always have at least one chunk (for the
     * newline character at the end, if there's nothing else available).
     */

    while (true) {
	if (segPtr->typePtr == &tkTextCharType) {
	    if (data->skipSpaces) {
		if (segPtr->body.chars[byteOffset] == ' ') {
		    TkTextIndex index = data->index;
		    int offset = byteOffset;

		    while (segPtr->body.chars[byteOffset] == ' ') {
			byteOffset += 1;
		    }
		    TkTextIndexForwBytes(data->textPtr, &index, byteOffset - offset, &data->index);
		    LayoutSkipBytes(data, dlPtr, &index, &data->index);
		    byteIndex = TkTextIndexGetByteIndex(&data->index);
		}
		data->skipSpaces = false;
	    }
	    if (segPtr->size > byteOffset) {
		if (!LayoutChars(data, segPtr, segPtr->size, byteOffset)) {
		    /* finished with this display line */
		    LayoutFinalizeChunk(data);
		    return false;
		}
		assert(data->chunkPtr);
		byteIndex += data->chunkPtr->numBytes;
		/* NOTE: byteOffset may become larger than segPtr->size because of end of line symbol. */
		if ((byteOffset += data->chunkPtr->numBytes) >= segPtr->size) {
		    segPtr = segPtr->nextPtr;
		    byteOffset = 0;
		}
	    } else {
		assert(segPtr->size == byteOffset);
		segPtr = segPtr->nextPtr;
		byteOffset = 0;
	    }
	} else {
	    switch (segPtr->typePtr->group) {
	    case SEG_GROUP_HYPHEN:
		if (!LayoutHyphen(data, segPtr)) {
		    /* finished with this display line */
		    LayoutFinalizeChunk(data);
		    return false;
		}
		byteIndex += segPtr->size;
		data->skipSpaces = false;
		break;
	    case SEG_GROUP_IMAGE:
	    case SEG_GROUP_WINDOW:
		if (!LayoutEmbedded(data, segPtr)) {
		    /* finished with this display line */
		    LayoutFinalizeChunk(data);
		    return false;
		}
		byteIndex += segPtr->size;
		data->skipSpaces = false;
		break;
	    case SEG_GROUP_MARK:
		if (segPtr == endPtr) {
		    /*
		     * We need a final chunk containing the final newline, otherwise x position
		     * lookup will not work. Here we will not use LayoutSkipBytes() for the bytes
		     * between current position and last char in line, because this would require
		     * an inconsistent index, and it's easier to avoid this. It's only a single
		     * newline which terminates the line, so no bad things will happen if we omit
		     * this skip-chunk.
		     */
		    segPtr = segPtr->sectionPtr->linePtr->lastPtr;
		    LayoutChars(data, segPtr, segPtr->size, segPtr->size - 1);
		} else {
		    if (LayoutMark(data, segPtr)) {
			data->cursorChunkPtr = data->chunkPtr;
		    }
		    assert(segPtr->size == 0);
		}
		break;
	    case SEG_GROUP_BRANCH: {
		TkTextIndex index = data->index;
		assert(segPtr->typePtr == &tkTextBranchType);
		assert(segPtr->size == 0);
		TkTextIndexSetSegment(&data->index, segPtr = segPtr->body.branch.nextPtr);
		LayoutSkipBytes(data, dlPtr, &index, &data->index);
		byteIndex = TkTextIndexGetByteIndex(&data->index);
		break;
	    }
	    case SEG_GROUP_PROTECT:
	    case SEG_GROUP_TAG:
	    case SEG_GROUP_CHAR:
		assert(!"unexpected segment type");
		break;
	    }
	    segPtr = segPtr->nextPtr;
	    byteOffset = 0;
	}
	if (!segPtr) {
	    LayoutFinalizeChunk(data);
	    return true;
	}
	TkTextIndexSetPosition(&data->index, byteIndex, segPtr);
    }

    return false; /* never reached */
}

static void
LayoutDestroyChunks(
    LayoutData *data)
{
    TkTextDispChunk *chunkPtr = data->lastChunkPtr;
    TextDInfo *dInfoPtr;

    if (chunkPtr == data->breakChunkPtr) {
	return;
    }

    dInfoPtr = data->textPtr->dInfoPtr;

    /*
     * We have to destroy the chunks backward, because the context support
     * is expecting this.
     */

    for ( ; chunkPtr != data->breakChunkPtr; chunkPtr = chunkPtr->prevPtr) {
	assert(chunkPtr != data->firstCharChunkPtr);
	assert(chunkPtr->layoutProcs);
	assert(!chunkPtr->sectionPtr);

	data->numSpaces -= chunkPtr->numSpaces;
	data->dispLineOffset -= chunkPtr->numBytes;
	data->numBytesSoFar -= chunkPtr->numBytes;
	data->countChunks -= 1;

	if (chunkPtr == data->cursorChunkPtr) {
	    data->cursorChunkPtr = NULL;
	} else if (chunkPtr == data->lastCharChunkPtr) {
	    data->lastCharChunkPtr = chunkPtr->prevCharChunkPtr;
	}
	if (chunkPtr->layoutProcs->type == TEXT_DISP_IMAGE) {
	    dInfoPtr->countImages -= 1;
	} else if (chunkPtr->layoutProcs->type == TEXT_DISP_WINDOW) {
	    dInfoPtr->countWindows -= 1;
	}
	LayoutUndisplay(data, chunkPtr);
	LayoutReleaseChunk(data->textPtr, chunkPtr);
	DEBUG(chunkPtr->stylePtr = NULL);
    }

    data->lastChunkPtr->nextPtr = dInfoPtr->chunkPoolPtr;
    dInfoPtr->chunkPoolPtr = data->breakChunkPtr->nextPtr;
    dInfoPtr->chunkPoolPtr->prevPtr = NULL;
    data->breakChunkPtr->nextPtr = NULL;
    data->lastChunkPtr = data->breakChunkPtr;
    data->chunkPtr = NULL;
    data->x = data->lastChunkPtr->x + data->lastChunkPtr->width;
#if TK_LAYOUT_WITH_BASE_CHUNKS
    data->baseChunkPtr = data->breakChunkPtr->baseChunkPtr;
#endif
}

static void
LayoutBreakLine(
    LayoutData *data,
    const TkTextIndex *indexPtr)	/* Index of display line start. */
{
    if (!data->breakChunkPtr) {
	/*
	 * This code makes sure that we don't accidentally display chunks with
	 * no characters at the end of the line (such as the insertion
	 * cursor). These chunks belong on the next line. So, throw away
	 * everything after the last chunk that has characters in it.
	 */

	data->breakChunkPtr = data->lastCharChunkPtr;
    }

    while (IsHyphenChunk(data->breakChunkPtr)) {
	TkTextDispChunk *hyphenChunkPtr;
	TkTextDispChunk *prevChunkPtr;
	TkTextDispChunk *nextChunkPtr;

	/*
	 * This can only happen if the breaking chunk is a hyphen segment.
	 * So try to hyphenate at this point. Normally this will succeed,
	 * but in seldom cases the hyphenation does not fit, then we have
	 * to search back for the next breaking chunk.
	 */

	hyphenChunkPtr = data->breakChunkPtr;
	prevChunkPtr = hyphenChunkPtr->prevCharChunkPtr;
	nextChunkPtr = LayoutGetNextCharChunk(hyphenChunkPtr);

	if (prevChunkPtr && nextChunkPtr) {
	    TkTextSegment *hyphenSegPtr = hyphenChunkPtr->clientData;

	    LayoutApplyHyphenRules(data, prevChunkPtr, hyphenChunkPtr, nextChunkPtr);
	    data->breakChunkPtr = prevChunkPtr;
	    LayoutDestroyChunks(data);

	    if (data->decreaseNumBytes > 0) {
		TkTextIndex index = *indexPtr;
		TkTextSegment *segPtr;
		unsigned newNumBytes = 0;
		unsigned numBytes;

		/*
		 * We need a new layout of the preceding char chunk because of possible
		 * spelling changes.
		 */

		while (data->decreaseNumBytes >= prevChunkPtr->numBytes
			&& prevChunkPtr != data->firstCharChunkPtr) {
		    data->decreaseNumBytes -= prevChunkPtr->numBytes;
		    newNumBytes += prevChunkPtr->numBytes;
		    prevChunkPtr = prevChunkPtr->prevPtr;
		}

		data->breakChunkPtr = prevChunkPtr;
		LayoutDestroyChunks(data);
		newNumBytes += prevChunkPtr->numBytes;

		if (data->decreaseNumBytes > 0) {
		    segPtr = CHAR_CHUNK_GET_SEGMENT(prevChunkPtr);
		    prevChunkPtr->numBytes -= data->decreaseNumBytes;
		    numBytes = prevChunkPtr->numBytes;
		    assert(prevChunkPtr->layoutProcs);
		    LayoutUndisplay(data, prevChunkPtr);
		    data->chunkPtr = prevChunkPtr;
		    LayoutMakeCharInfo(data, segPtr, prevChunkPtr->segByteOffset, numBytes);
		    TkTextIndexForwBytes(data->textPtr, &index, prevChunkPtr->byteOffset, &index);
		    segPtr->typePtr->layoutProc(&index, segPtr, prevChunkPtr->segByteOffset,
			    data->maxX, numBytes, 0, data->wrapMode, data->textPtr->spaceMode,
			    prevChunkPtr);
		    LayoutFinalizeCharInfo(data, false); /* second parameter doesn't matter here */

		    if (prevChunkPtr->numBytes != numBytes && prevChunkPtr != data->firstCharChunkPtr) {
			/*
			 * The content doesn't fits into the display line (but it must fit if
			 * this is the first char chunk).
			 */
			hyphenSegPtr = NULL;
		    }
		}

		prevChunkPtr->numBytes = newNumBytes;
		data->chunkPtr = NULL;
	    }

	    if (hyphenSegPtr) {
		int maxX = data->maxX;
		bool fits;

		data->x = prevChunkPtr->x + prevChunkPtr->width;
		if (prevChunkPtr == data->firstCharChunkPtr && prevChunkPtr->breakIndex <= 0) {
		    data->maxX = INT_MAX; /* The hyphen must be shown. */
		}
		fits = LayoutChars(data, hyphenSegPtr, hyphenSegPtr->body.hyphen.textSize, 0);
		assert(!fits || (int) data->chunkPtr->numBytes == hyphenSegPtr->body.hyphen.textSize);
		hyphenChunkPtr = data->chunkPtr;
		data->maxX = maxX;

		if (fits) {
		    /* The hyphen fits, so we're done. */
		    LayoutFinalizeChunk(data);
		    hyphenChunkPtr->numBytes = 1 + data->increaseNumBytes;
		    return;
		}

		LayoutFreeChunk(data);
		data->hyphenRule = 0;
	    }
	}

	/*
	 * We couldn't hyphenate, so search for next candidate for wrapping.
	 */

	if (IsHyphenChunk(data->breakChunkPtr)) {
	    if (!(data->breakChunkPtr = data->breakChunkPtr->prevPtr)) {
		return;
	    }
	}
	if (data->breakChunkPtr->breakIndex <= 0) {
	    do {
		if (!(data->breakChunkPtr = data->breakChunkPtr->prevPtr)) {
		    return;
		}
	    } while (data->breakChunkPtr->breakIndex <= 0 && !IsHyphenChunk(data->breakChunkPtr));
	}

	data->chunkPtr = NULL;
    }

    /*
     * Now check if we must break because the line length have been exceeded. At this point
     * hyphenation is not involved.
     */

    if (data->breakChunkPtr
	    && (data->lastChunkPtr != data->breakChunkPtr
		|| (data->lastChunkPtr->breakIndex > 0
		    && data->lastChunkPtr->breakIndex != (int) data->lastChunkPtr->numBytes))) {
	unsigned addNumBytes = 0;

	LayoutDestroyChunks(data);

	if (data->breakChunkPtr->breakIndex > 0 && data->breakChunkPtr->numSpaces > 0) {
	    const TkTextDispChunk *breakChunkPtr = data->breakChunkPtr;
	    const CharInfo *ciPtr = breakChunkPtr->clientData;
	    const char *p = ciPtr->u.chars + ciPtr->baseOffset + breakChunkPtr->breakIndex;
	    const char *q = Tcl_UtfPrev(p, ciPtr->u.chars + ciPtr->baseOffset);

	    if (IsExpandableSpace(q)
		    && !(breakChunkPtr->wrappedAtSpace
			&& breakChunkPtr->breakIndex == (int) breakChunkPtr->numBytes)) {
		addNumBytes = p - q;
		data->breakChunkPtr->breakIndex -= addNumBytes;
		data->breakChunkPtr->numSpaces -= 1;
		data->numSpaces -= 1;
	    }
	}

	if (data->breakChunkPtr->breakIndex != (int) data->breakChunkPtr->numBytes) {
	    TkTextSegment *segPtr;
	    TkTextDispChunk *chunkPtr = data->breakChunkPtr;
	    TkTextIndex index = *indexPtr;

	    LayoutUndisplay(data, chunkPtr);
	    data->chunkPtr = chunkPtr;
	    TkTextIndexForwBytes(data->textPtr, &index, chunkPtr->byteOffset, &index);
	    segPtr = TkTextIndexGetContentSegment(&index, NULL);
	    LayoutMakeCharInfo(data, segPtr, chunkPtr->segByteOffset, data->breakChunkPtr->breakIndex);
	    segPtr->typePtr->layoutProc(&index, segPtr, chunkPtr->segByteOffset, data->maxX,
		    data->breakChunkPtr->breakIndex, 0, data->wrapMode, data->textPtr->spaceMode,
		    chunkPtr);
	    LayoutFinalizeCharInfo(data, false); /* second parameter doesn't matter here */
	    LayoutDoWidthAdjustmentForContextDrawing(data);
	    chunkPtr->numBytes += addNumBytes;

	    if (chunkPtr->skipFirstChar) {
		chunkPtr->numBytes += 1;
	    }
	}
    }

    /*
     * Remove all the empty chunks at end of line. In this way we avoid to have
     * an insert cursur chunk at end of line, which should belong to the next line.
     */

    if (data->lastChunkPtr->numBytes == 0) {
	data->breakChunkPtr = data->breakChunkPtr->prevPtr;
	assert(data->breakChunkPtr);
	while (data->breakChunkPtr->numBytes == 0) {
	    data->breakChunkPtr = data->breakChunkPtr->prevPtr;
	    assert(data->breakChunkPtr);
	}
	LayoutDestroyChunks(data);
    }
}

static void
LayoutFullJustification(
    LayoutData *data,
    DLine *dlPtr)
{
    TkTextDispChunk *chunkPtr;
    TkTextDispChunk *nextChunkPtr;
    unsigned numSpaces;
    int remainingPixels;
    int shiftX;

    numSpaces = data->numSpaces;
    remainingPixels = data->maxX - dlPtr->length;

    if (numSpaces == 0 || remainingPixels <= 0) {
	return;
    }

    shiftX = 0;
    chunkPtr = dlPtr->chunkPtr;

    while ((nextChunkPtr = chunkPtr->nextPtr)) {
	if (chunkPtr->numSpaces > 0) {
	    unsigned expand = 0;
	    unsigned i;

	    assert(IsCharChunk(chunkPtr));

	    for (i = 0; i < chunkPtr->numSpaces; ++i) {
		unsigned space;

		assert(numSpaces > 0);
		space = (remainingPixels + numSpaces - 1)/numSpaces;
		expand += space;
		remainingPixels -= space;
		numSpaces -= 1;
	    }

	    shiftX += expand;
	    chunkPtr->width += expand;
	    chunkPtr->additionalWidth = expand;
	}

	nextChunkPtr->x += shiftX;
	chunkPtr = nextChunkPtr;
    }
}

static bool
LayoutPrevDispLineEndsWithSpace(
    const TkText *textPtr,
    const TkTextSegment *segPtr,
    int offset)
{
    assert(segPtr);
    assert(offset < segPtr->size);

    if (TkTextSegmentIsElided(textPtr, segPtr)) {
	if (!(segPtr = TkBTreeFindStartOfElidedRange(textPtr->sharedTextPtr, textPtr, segPtr))) {
	    return false;
	}
	offset = -1;
    }

    if (offset == -1) {
	while (true) {
	    if (!(segPtr = segPtr->prevPtr)) {
		return false;
	    }
	    switch ((int) segPtr->typePtr->group) {
	    case SEG_GROUP_CHAR:
		return segPtr->body.chars[segPtr->size - 1] == ' ';
	    case SEG_GROUP_BRANCH:
		if (segPtr->typePtr == &tkTextLinkType) {
		    segPtr = segPtr->body.link.prevPtr;
		}
	    	break;
	    case SEG_GROUP_MARK:
		/* skip */
		break;
	    case SEG_GROUP_HYPHEN:
	    case SEG_GROUP_IMAGE:
	    case SEG_GROUP_WINDOW:
	    	return false;
	    }
	}
    }

    return segPtr->typePtr == &tkTextCharType && segPtr->body.chars[offset] == ' ';
}

static DLine *
LayoutDLine(
    const TkTextIndex *indexPtr,/* Beginning of display line. May not necessarily point to
    				 * a character segment. */
    unsigned displayLineNo)	/* Display line number of logical line, needed for caching. */
{
    TextDInfo *dInfoPtr;
    DLine *dlPtr;
    TkText *textPtr;
    StyleValues *sValPtr;
    TkTextDispChunk *chunkPtr;
    TkTextDispChunkSection *sectionPtr;
    TkTextDispChunkSection *prevSectionPtr;
    TkTextSegment *segPtr;
    LayoutData data;
    bool endOfLogicalLine;
    bool isStartOfLine;
    int ascent, descent, leading, jIndent;
    unsigned countChunks;
    unsigned chunksPerSection;
    int length, offset;

    assert(displayLineNo >= 0);
    assert((displayLineNo == 0) ==
	    (IsStartOfNotMergedLine(indexPtr) || TkTextIndexIsStartOfText(indexPtr)));

    DEBUG(stats.numLayouted += 1);

    textPtr = indexPtr->textPtr;
    assert(textPtr);
    dInfoPtr = textPtr->dInfoPtr;

    /*
     * Create and initialize a new DLine structure.
     */

    if (dInfoPtr->dLinePoolPtr) {
	dlPtr = dInfoPtr->dLinePoolPtr;
	dInfoPtr->dLinePoolPtr = dlPtr->nextPtr;
    } else {
	dlPtr = malloc(sizeof(DLine));
	DEBUG_ALLOC(tkTextCountNewDLine++);
    }
    dlPtr = memset(dlPtr, 0, sizeof(DLine));
    dlPtr->flags = NEW_LAYOUT|OLD_Y_INVALID;
    dlPtr->index = *indexPtr;
    dlPtr->displayLineNo = displayLineNo;
    TkTextIndexToByteIndex(&dlPtr->index);
    isStartOfLine = TkTextIndexIsStartOfLine(&dlPtr->index);

    /*
     * Initialize layout data.
     */

    memset(&data, 0, sizeof(data));
    data.dlPtr = dlPtr;
    data.index = dlPtr->index;
    data.justify = textPtr->justify;
    data.tabIndex = -1;
    data.tabStyle = TK_TEXT_TABSTYLE_TABULAR;
    data.wrapMode = textPtr->wrapMode;
    data.paragraphStart = displayLineNo == 0;
    data.trimSpaces = textPtr->spaceMode == TEXT_SPACEMODE_TRIM;
    data.displayLineNo = displayLineNo;
    data.textPtr = textPtr;

    if (data.paragraphStart) {
	dlPtr->flags |= PARAGRAPH_START;
	data.logicalLinePtr = TkTextIndexGetLine(indexPtr);
	data.byteOffset = TkTextIndexGetByteIndex(indexPtr);
    } else {
	TkTextLine *linePtr = TkTextIndexGetLine(indexPtr);
	TkTextIndex index2 = *indexPtr;

	data.logicalLinePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
	DEBUG(TkTextIndexSetPeer(&index2, NULL)); /* allow index outside of peer */
	TkTextIndexSetByteIndex2(&index2, data.logicalLinePtr, 0);
	data.byteOffset = TkTextIndexCountBytes(&index2, indexPtr);
    }

    segPtr = TkTextIndexGetContentSegment(indexPtr, &offset);
    data.skipSpaces = data.trimSpaces && LayoutPrevDispLineEndsWithSpace(textPtr, segPtr, offset - 1);

    /*
     * Skip elided region.
     */

    if (TkTextSegmentIsElided(textPtr, segPtr)) {
	segPtr = TkBTreeFindEndOfElidedRange(textPtr->sharedTextPtr, textPtr, segPtr);
	TkTextIndexSetSegment(&data.index, segPtr);
	LayoutSkipBytes(&data, dlPtr, indexPtr, &data.index);

	/*
	 * NOTE: it is possible that we have reached now the end of text. This is
	 * the only case that an empty display line can be produced, which will be
	 * linked into the list of display lines. It's only a single superfluous
	 * line, so we can live with that.
	 */

	if (!textPtr->showEndOfText && TkTextIndexIsEndOfText(&data.index)) {
	    assert(data.chunkPtr);
	    assert(!data.chunkPtr->nextPtr);
	    dlPtr->byteCount = data.chunkPtr->numBytes;
	    LayoutFreeChunk(&data);
	    LayoutUpdateLineHeightInformation(&data, dlPtr, data.logicalLinePtr, true, 0);
	    return dlPtr;
	}
    }

    endOfLogicalLine = LayoutLogicalLine(&data, dlPtr);
    assert(data.numBytesSoFar > 0);

    /*
     * We're at the end of the display line. Throw away everything after the
     * most recent word break, if there is one; this may potentially require
     * the last chunk to be layed out again. Also perform hyphenation, if
     * enabled, this probably requires the re-layout of a few chunks at the
     * end of the line.
     */

    if (!endOfLogicalLine) {
	LayoutBreakLine(&data, &dlPtr->index);
    }

    if (data.textPtr->hyphenate) {
	TkTextDispChunk *chunkPtr = data.firstChunkPtr->nextPtr;

	/*
	 * Remove all unused hyphen segments, this will speed up the display process,
	 * because this removal will be done only one time, but the display process
	 * may iterate over the chunks several times.
	 */

	while (chunkPtr) {
	    TkTextDispChunk *nextChunkPtr = chunkPtr->nextPtr;

	    if (nextChunkPtr && chunkPtr->width == 0 && chunkPtr != data.cursorChunkPtr) {
		chunkPtr->prevPtr->numBytes += chunkPtr->numBytes;

		if ((chunkPtr->prevPtr->nextPtr = nextChunkPtr)) {
		    nextChunkPtr->prevPtr = chunkPtr->prevPtr;
		    data.chunkPtr = chunkPtr;
		    LayoutFreeChunk(&data);
		}
	    }

	    chunkPtr = nextChunkPtr;
	}
    }

    /*
     * This has to be done after LayoutBreakLine.
     */

    dlPtr->chunkPtr = data.firstChunkPtr;
    dlPtr->lastChunkPtr = data.lastChunkPtr;
    dlPtr->cursorChunkPtr = data.cursorChunkPtr;
    dlPtr->firstCharChunkPtr = data.firstCharChunkPtr;
    dlPtr->breakInfo = data.breakInfo;

    /*
     * Make tab adjustments for the last tab stop, if there is one.
     */

    if (data.tabIndex >= 0) {
	assert(data.tabChunkPtr);
	AdjustForTab(&data);
    }

    /*
     * Make one more pass over the line to recompute various things like its
     * height, length, and total number of bytes. Also modify the x-locations
     * of chunks to reflect justification.
     */

    if (data.wrapMode == TEXT_WRAPMODE_NONE) {
	data.maxX = dInfoPtr->maxX - dInfoPtr->x - data.rMargin;
    }
    length = dlPtr->length = data.lastChunkPtr->x + data.lastChunkPtr->width;
    if (data.wrapMode != TEXT_WRAPMODE_NONE) {
	length = MIN(length, data.maxX);
    }

    jIndent = 0;

    switch (data.justify) {
    case TK_TEXT_JUSTIFY_LEFT:
    	/* no action */
	break;
    case TK_TEXT_JUSTIFY_RIGHT:
	jIndent = data.maxX - length;
	break;
    case TK_TEXT_JUSTIFY_FULL:
	if (!endOfLogicalLine) {
	    LayoutFullJustification(&data, dlPtr);
	}
	break;
    case TK_TEXT_JUSTIFY_CENTER:
	jIndent = (data.maxX - length)/2;
	break;
    }

    ascent = descent = 0;
    sectionPtr = prevSectionPtr = NULL;
    chunksPerSection = (data.countChunks + MAX_SECTIONS_PER_LINE - 1)/MAX_SECTIONS_PER_LINE;
    chunksPerSection = MAX(chunksPerSection, MIN_CHUNKS_PER_SECTION);
    countChunks = chunksPerSection - 1;

    for (chunkPtr = dlPtr->chunkPtr; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
	if (++countChunks == chunksPerSection) {
	    /*
	     * Create next section.
	     */
	    sectionPtr = LayoutNewSection(dInfoPtr);
	    if (prevSectionPtr) {
		prevSectionPtr->nextPtr = sectionPtr;
	    }
	    sectionPtr->chunkPtr = chunkPtr;
	    prevSectionPtr = sectionPtr;
	    countChunks = 0;
	}
	chunkPtr->sectionPtr = sectionPtr;
	sectionPtr->numBytes += chunkPtr->numBytes;
	dlPtr->byteCount += chunkPtr->numBytes;
	chunkPtr->x += jIndent;
	ascent = MAX(ascent, chunkPtr->minAscent);
	descent = MAX(descent, chunkPtr->minDescent);
	dlPtr->height = MAX(dlPtr->height, chunkPtr->minHeight);
	sValPtr = chunkPtr->stylePtr->sValuePtr;
	if (sValPtr->borderWidth > 0 && sValPtr->relief != TK_RELIEF_FLAT) {
	    dlPtr->flags |= HAS_3D_BORDER;
	}
    }

    leading = ascent + descent;

    if (dlPtr->height < leading) {
	dlPtr->height = leading;
	dlPtr->baseline = ascent;
    } else {
	dlPtr->baseline = ascent + (dlPtr->height - leading)/2;
    }

    sValPtr = dlPtr->chunkPtr->stylePtr->sValuePtr;

    dlPtr->spaceAbove = isStartOfLine ? sValPtr->spacing1 : (sValPtr->spacing2 + 1)/2;
    dlPtr->spaceBelow = endOfLogicalLine ? sValPtr->spacing3 : sValPtr->spacing2/2;
    dlPtr->height += dlPtr->spaceAbove + dlPtr->spaceBelow;
    dlPtr->baseline += dlPtr->spaceAbove;
    /* line length may have changed because of justification */
    dlPtr->length = data.lastChunkPtr->x + jIndent + data.lastChunkPtr->width;

    LayoutUpdateLineHeightInformation(&data, dlPtr, data.logicalLinePtr,
	    endOfLogicalLine, data.hyphenRule);

    return dlPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * CheckIfLineMetricIsUpToDate --
 *
 *	This function wil be invoked after update of line metric calculations.
 *	It checks whether the all line metrics are up-to-date, and will
 *	invoke the appropriate actions.
 *
 * Results:
 *	Returns true if the widget has not been deleted by receiver of the
 *	triggered callback.
 *
 * Side effects:
 *	Firing the <<WidgetViewSync>> event, scrollbar update, and resetting
 *	some states.
 *
 *----------------------------------------------------------------------
 */

static bool
TriggerWatchCursor(
    TkText *textPtr)
{
    if (textPtr->watchCmd) {
	TextDInfo *dInfoPtr = textPtr->dInfoPtr;
	char buf[2][2*TK_POS_CHARS + 2];

	if (memcmp(&dInfoPtr->curPixelPos, &dInfoPtr->prevPixelPos, sizeof(PixelPos)) != 0) {
	    textPtr->sharedTextPtr->triggerWatchCmd = false;
	    snprintf(buf[0], sizeof(buf[0]), "@%d,%d",
		    dInfoPtr->curPixelPos.xFirst, dInfoPtr->curPixelPos.yFirst);
	    snprintf(buf[1], sizeof(buf[1]), "@%d,%d",
		    dInfoPtr->curPixelPos.xLast, dInfoPtr->curPixelPos.yLast);
	    TkTextTriggerWatchCmd(textPtr, "view", buf[0], buf[1], NULL, false);
	    memcpy(&textPtr->dInfoPtr->prevPixelPos, &textPtr->dInfoPtr->curPixelPos, sizeof(PixelPos));
	    textPtr->sharedTextPtr->triggerWatchCmd = true;
	}
    }

    return !(textPtr->flags & DESTROYED);
}

static void
UpdateLineMetricsFinished(
    TkText *textPtr,
    bool sendImmediately)
{
    assert(TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges));

    textPtr->dInfoPtr->flags &= ~(ASYNC_UPDATE|ASYNC_PENDING);
    textPtr->dInfoPtr->pendingUpdateLineMetricsFinished = false;

    TkTextRunAfterSyncCmd(textPtr);

    /*
     * Fire the <<WidgetViewSync>> event since the widget view is in sync
     * with its internal data (actually it will be after the next trip
     * through the event loop, because the widget redraws at idle-time).
     */

    TkTextGenerateWidgetViewSyncEvent(textPtr, sendImmediately);
}

static void
RunUpdateLineMetricsFinished(
    ClientData clientData)
{
    TkText *textPtr = (TkText *) clientData;

    if (!(textPtr->flags & DESTROYED)) {
	textPtr->dInfoPtr->pendingUpdateLineMetricsFinished = false;
	if (TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges)) {
	    UpdateLineMetricsFinished(textPtr, true);
	}
    }
}

static void
CheckIfLineMetricIsUpToDate(
    TkText *textPtr)
{
    if (textPtr->sharedTextPtr->allowUpdateLineMetrics
	    && TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges)) {
	/*
	 * Remove the async handler.
	 */

	if (textPtr->dInfoPtr->lineUpdateTimer) {
	    Tcl_DeleteTimerHandler(textPtr->dInfoPtr->lineUpdateTimer);
	    textPtr->refCount -= 1;
	    textPtr->dInfoPtr->lineUpdateTimer = NULL;
	}

	/*
	 * If we have a full update, then also update the scrollbar.
	 */

	GetYView(textPtr->interp, textPtr, true);

	if (!(TriggerWatchCursor(textPtr))) {
	    return; /* the widget has been deleted */
	}

	/*
	 * Report finish of full update.
	 */

	if (!textPtr->dInfoPtr->pendingUpdateLineMetricsFinished) {
	    textPtr->dInfoPtr->pendingUpdateLineMetricsFinished = true;
	    Tcl_DoWhenIdle(RunUpdateLineMetricsFinished, (ClientData) textPtr);
	}

	if (tkBTreeDebug) {
	    CheckLineMetricConsistency(textPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SaveDisplayLines --
 *
 *	Save the display lines, produced during line metric computation,
 *	for displaying. So UpdateDisplayInfo eventually must not re-compute
 *	these lines. This function will only be called if it is sure that
 *	DisplayText will be triggered afterwards, because UpdateDisplayInfo
 *	(called by DisplayText) is responsible for releasing the unused lines.
 *	The caller is responsible that the display will be saved in order from
 *	top to bottom, without gaps.
 *
 *	Saving the produced display lines is an important speed improvement
 *	(especially on Mac). Consider the following use case:
 *
 *	1. The text widget will be created.
 *	2. Content will be inserted.
 *	3. The view will be changed (for example to the end of the file).
 *
 *	In this case MeasureDown will produce display lines for line metric
 *	calculation, needed for the change of the view, and afterwards
 *	UpdateDisplayInfo needs (some of) these lines for displaying.
 *
 *	Note that no more lines will be saved than fitting into the widget,
 *	all surplus lines will be released immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The display lines will be saved in TextDInfo. Function UpdateDisplayInfo
 *	is responsible to release the unused lines. The cached lines in
 *	argument 'info' will be taken over, this means that 'info->dLinePtr'
 *	is NULL after this function has done his work.
 *
 *----------------------------------------------------------------------
 */

static void
SaveDisplayLines(
    TkText *textPtr,
    DisplayInfo *info,
    bool append)	/* Append to previously saved lines if 'true', otherwise prepend. */
{
    TextDInfo *dInfoPtr;
    DLine *firstPtr, *lastPtr;
    int height, viewHeight;

    if (!(firstPtr = info->dLinePtr)) {
	return;
    }

    assert(info->lastDLinePtr);
    lastPtr = info->lastDLinePtr;
    dInfoPtr = textPtr->dInfoPtr;
    height = dInfoPtr->savedDisplayLinesHeight + info->heightOfCachedLines;
    viewHeight = Tk_Height(textPtr->tkwin) - 2*textPtr->highlightWidth;
    /* we need some overhead, because the widget may show lines only partially */
    viewHeight += info->dLinePtr->height;

    if (append) {
	if (dInfoPtr->lastSavedDLinePtr) {
	    dInfoPtr->lastSavedDLinePtr->nextPtr = firstPtr;
	    firstPtr->prevPtr = dInfoPtr->lastSavedDLinePtr;
	} else {
	    dInfoPtr->savedDLinePtr = firstPtr;
	}
	dInfoPtr->lastSavedDLinePtr = lastPtr;
	firstPtr = lastPtr = dInfoPtr->savedDLinePtr;
	while (lastPtr->nextPtr && height >= viewHeight - lastPtr->height) {
	    height -= lastPtr->height;
	    lastPtr = lastPtr->nextPtr;
	}
	if (firstPtr != lastPtr) {
	    FreeDLines(textPtr, firstPtr, lastPtr, DLINE_FREE_TEMP);
	    assert(dInfoPtr->savedDLinePtr == lastPtr);
	}
    } else {
	if (dInfoPtr->savedDLinePtr) {
	    lastPtr->nextPtr = dInfoPtr->savedDLinePtr;
	    dInfoPtr->savedDLinePtr->prevPtr = lastPtr;
	} else {
	    dInfoPtr->lastSavedDLinePtr = lastPtr;
	}
	dInfoPtr->savedDLinePtr = firstPtr;
	firstPtr = lastPtr = dInfoPtr->lastSavedDLinePtr;
	while (firstPtr->prevPtr && height >= viewHeight - firstPtr->height) {
	    height -= firstPtr->height;
	    firstPtr = firstPtr->prevPtr;
	}
	if (firstPtr != lastPtr) {
	    FreeDLines(textPtr, firstPtr->nextPtr, NULL, DLINE_FREE_TEMP);
	    assert(!firstPtr->nextPtr);
	    dInfoPtr->lastSavedDLinePtr = firstPtr;
	}
    }

    dInfoPtr->savedDisplayLinesHeight = height;
    info->dLinePtr = info->lastDLinePtr = NULL;
    info->numCachedLines = 0;
    info->heightOfCachedLines = 0;
    assert(!dInfoPtr->savedDLinePtr == !dInfoPtr->lastSavedDLinePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeDisplayLineInfo --
 *
 *	This functions computes the display line information for struct
 *	DisplayInfo. If the cached information is still incomplete for
 *	this computation then LayoutDLine will be used for the computation
 *	of the missing display lines.
 *
 *	If additional display line computation is required for the line
 *	metric computation, then these lines will ba cached, but only
 *	the last produced lines which can fit into the widget (this means:
 *	no more lines than fitting into the widget will be cached).
 *
 * Results:
 *	The attributes of 'info' will be set. The return value is the
 *	corresponding logical line.
 *
 * Side effects:
 *	The cache may be filled with more line metric information.
 *	Furthermore some of the produced display lines will be cached,
 *	the caller is responsible to release these lines.
 *
 *----------------------------------------------------------------------
 */

static TkTextDispLineEntry *
SearchDispLineEntry(
    TkTextDispLineEntry *first,
    const TkTextDispLineEntry *last,
    unsigned byteOffset)
{
    /*
     * NOTE: here 'last' is the last entry (not the pointer after the last
     * element as usual).
     */

    if (byteOffset >= last->byteOffset) {
	return (TkTextDispLineEntry *) last; /* frequent case */
    }

    while (first != last) {
	TkTextDispLineEntry *mid = first + (last - first)/2;

	if (byteOffset >= (mid + 1)->byteOffset) {
	    first = mid + 1;
	} else {
	    last = mid;
	}
    }

    return first;
}

static void
InsertDLine(
    TkText *textPtr,
    DisplayInfo *info,
    DLine *dlPtr,
    unsigned viewHeight)
{
    DLine *firstPtr = info->dLinePtr;

    assert(!dlPtr->nextPtr);
    assert(!dlPtr->prevPtr);

    info->heightOfCachedLines += dlPtr->height;

    if (firstPtr && info->heightOfCachedLines >= viewHeight + firstPtr->height) {
	info->heightOfCachedLines -= firstPtr->height;
	if ((info->dLinePtr = firstPtr->nextPtr)) {
	    info->dLinePtr->prevPtr = NULL;
	} else {
	    info->lastDLinePtr = NULL;
	}
	firstPtr->nextPtr = NULL;
	FreeDLines(textPtr, firstPtr, NULL, DLINE_FREE_TEMP);
    } else {
	info->numCachedLines += 1;
    }
    if (info->lastDLinePtr) {
	assert(info->dLinePtr);
	info->lastDLinePtr->nextPtr = dlPtr;
	dlPtr->prevPtr = info->lastDLinePtr;
    } else {
	assert(!info->dLinePtr);
	info->dLinePtr = dlPtr;
    }
    info->lastDLinePtr = dlPtr;
}

static TkTextLine *
ComputeDisplayLineInfo(
    TkText *textPtr,
    const TkTextIndex *indexPtr,
    DisplayInfo *info)
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextPixelInfo *pixelInfo;
    TkTextDispLineInfo *dispLineInfo;
    TkTextDispLineEntry *entry;
    TkTextLine *logicalLinePtr;
    TkTextLine *linePtr;
    unsigned byteOffset;
    unsigned startByteOffset;
    unsigned viewHeight;

    assert(info);

    linePtr = TkTextIndexGetLine(indexPtr);
    logicalLinePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
    pixelInfo = TkBTreeLinePixelInfo(textPtr, logicalLinePtr);
    dispLineInfo = pixelInfo->dispLineInfo;
    info->index = *indexPtr;
    TkTextIndexSetToStartOfLine2(&info->index, logicalLinePtr);
    startByteOffset = TkTextIndexGetByteIndex(&info->index);
    byteOffset = TkTextIndexCountBytes(&info->index, indexPtr);
    byteOffset += TkTextIndexGetByteIndex(&info->index);

    info->pixelInfo = pixelInfo;
    info->displayLineNo = 0;
    info->numDispLines = 1;
    info->entry = info->entryBuffer;
    info->dLinePtr = info->lastDLinePtr = NULL;
    info->nextByteOffset = -1;
    info->numCachedLines = 0;
    info->heightOfCachedLines = 0;
    info->linePtr = linePtr;

    if (dInfoPtr->lineMetricUpdateEpoch == (pixelInfo->epoch & EPOCH_MASK)) {
	if (!dispLineInfo) {
	    TkTextLine *nextLogicalLinePtr =
		    TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, logicalLinePtr);

	    entry = info->entryBuffer;
	    if (logicalLinePtr->nextPtr == nextLogicalLinePtr
		    && TkTextIndexIsStartOfLine(&info->index)) {
		info->nextByteOffset = logicalLinePtr->size - byteOffset;
		entry->byteOffset = 0;
		(entry + 1)->byteOffset = logicalLinePtr->size;
	    } else {
		TkTextIndex index2 = info->index;
		TkTextIndexSetToStartOfLine2(&index2, nextLogicalLinePtr);
		info->nextByteOffset = TkTextIndexCountBytes(&info->index, &index2);
		entry->byteOffset = TkTextIndexGetByteIndex(&info->index);
		(entry + 1)->byteOffset = entry->byteOffset + info->nextByteOffset;
	    }
	    info->byteOffset = byteOffset;
	    info->isComplete = true;
	    info->pixels = pixelInfo->height;
	    entry->height = pixelInfo->height;
	    entry->pixels = pixelInfo->height;
	    byteOffset = (entry + 1)->byteOffset - startByteOffset;
	    TkTextIndexForwBytes(textPtr, &info->index, byteOffset, &info->index);
	    return logicalLinePtr;
	}

	if (dispLineInfo->numDispLines > 0) {
	    const TkTextDispLineEntry *last;
	    unsigned nextByteOffset;

	    last = dispLineInfo->entry + dispLineInfo->numDispLines;
	    entry = SearchDispLineEntry(dispLineInfo->entry, last, byteOffset);

	    if (entry != last) {
		info->entry = entry;
		info->byteOffset = byteOffset - entry->byteOffset;
		info->nextByteOffset = (entry + 1)->byteOffset - byteOffset;
		info->displayLineNo = entry - dispLineInfo->entry;
		info->numDispLines = dispLineInfo->numDispLines;
		info->pixels = (last - 1)->pixels;
		info->isComplete = (dInfoPtr->lineMetricUpdateEpoch == pixelInfo->epoch);
		byteOffset = last->byteOffset - startByteOffset;
		TkTextIndexForwBytes(textPtr, &info->index, byteOffset, &info->index);
		return logicalLinePtr;
	    }

	    /*
	     * If we reach this point, then we need more information than already
	     * computed for this line.
	     */

	    info->displayLineNo = dispLineInfo->numDispLines;
	    nextByteOffset = last->byteOffset - dispLineInfo->entry[0].byteOffset;
	    TkBTreeMoveForward(&info->index, nextByteOffset);
	    byteOffset -= nextByteOffset;
	}
    }

    /*
     * Compute missing line metric information. Don't throw away the produced display
     * lines, probably the caller might use it. But do not cache more than fitting into
     * the widget.
     */

    viewHeight = Tk_Height(textPtr->tkwin) - 2*textPtr->highlightWidth;
    /* we need some overhead, because the widget may show lines only partially */
    viewHeight += dInfoPtr->dLinePtr ? dInfoPtr->dLinePtr->height : 20;

    while (true) {
	DLine *dlPtr;

	if (dInfoPtr->lastMetricDLinePtr
		&& pixelInfo->epoch == dInfoPtr->lineMetricUpdateEpoch
		&& TkTextIndexIsEqual(&info->index, &dInfoPtr->lastMetricDLinePtr->index)) {
	    dlPtr = dInfoPtr->lastMetricDLinePtr;
	    dInfoPtr->lastMetricDLinePtr = NULL;
	    assert(dlPtr->displayLineNo == info->displayLineNo);
	} else {
	    dlPtr = LayoutDLine(&info->index, info->displayLineNo);
	}
	InsertDLine(textPtr, info, dlPtr, viewHeight);
	TkTextIndexForwBytes(textPtr, &info->index, dlPtr->byteCount, &info->index);
	if (dInfoPtr->lineMetricUpdateEpoch == pixelInfo->epoch || byteOffset < dlPtr->byteCount) {
	    info->byteOffset = byteOffset;
	    info->nextByteOffset = dlPtr->byteCount - byteOffset;
	    info->isComplete = (dInfoPtr->lineMetricUpdateEpoch == pixelInfo->epoch);
	    break;
	}
	byteOffset -= dlPtr->byteCount;
	info->displayLineNo += 1;
    }

    /*
     * Note that LayoutDLine may re-allocate 'pixelInfo->dispLineInfo',
     * so variable 'dispLineInfo' is in general not valid anymore.
     */

    dispLineInfo = pixelInfo->dispLineInfo;

    if (dispLineInfo) {
	info->numDispLines = dispLineInfo->numDispLines;
	info->entry = dispLineInfo->entry + info->displayLineNo;
	info->pixels = dispLineInfo->entry[dispLineInfo->numDispLines - 1].pixels;
    } else {
	info->pixels = pixelInfo->height;
	info->entryBuffer[0].height = pixelInfo->height;
	info->entryBuffer[0].pixels = pixelInfo->height;
	info->entryBuffer[0].byteOffset = byteOffset;
	info->entryBuffer[1].byteOffset = info->nextByteOffset + info->byteOffset;
    }

    return logicalLinePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeMissingMetric --
 *
 *	This functions is continuing the computation of an unfinished
 *	display line metric, which can only happen if a logical line
 *	is wrapping into several display lines. It may not be required
 *	to compute all missing display lines, the computation stops
 *	until the threshold has been reached. But the compuation will
 *	always stop at the end of the logical line.
 *
 *	Possible threshold types are THRESHOLD_BYTE_OFFSET,
 *	THRESHOLD_PIXEL_DISTANCE, and THRESHOLD_LINE_OFFSET. The two
 *	former thresholds will be specified absolute (but relative to
 *	start of logical line), and the latter thresholds will be specified
 *	relative to info->displayLineNo.
 *
 *	If additional display line computation is required for the line
 *	metric computation, then these lines will ba cached, but only
 *	the last produced lines which can fit into the widget (this means:
 *	no more lines than fitting into the widget will be cached).
 *
 *	It is important that this function is only computing the relevant
 *	line metric. It may happen that a logical line may wrap into
 *	thousands of display lines, but if only the first 100 (following
 *	lines) are needed, then only the first 100 should be computed here,
 *	not more.
 *
 * Results:
 *	The attributes of 'info' will be updated.
 *
 * Side effects:
 *	The cache may be filled with more line metric information.
 *	Furthermore some of the produced display lines will be cached,
 *	the caller is responsible to release these lines.
 *
 *----------------------------------------------------------------------
 */

static void
ComputeMissingMetric(
    TkText *textPtr,
    DisplayInfo *info,
    Threshold thresholdType,
    int threshold)
{
    int byteOffset, additionalLines;
    unsigned displayLineNo;
    int *metricPtr = NULL; /* avoids compiler warning */
    unsigned viewHeight;
    TkTextIndex index;

    assert(threshold >= 0);

    if (info->isComplete) {
	return;
    }

    additionalLines = info->numDispLines - info->displayLineNo;
    assert(additionalLines > 0);
    byteOffset = info->entry[additionalLines].byteOffset;
    displayLineNo = info->numDispLines;
    viewHeight = Tk_Height(textPtr->tkwin) - 2*textPtr->highlightWidth;
    /* we need some overhead, because the widget may show lines only partially */
    viewHeight += textPtr->dInfoPtr->dLinePtr ? textPtr->dInfoPtr->dLinePtr->height : 20;
    TkTextIndexForwBytes(textPtr, &info->index,
	    byteOffset - info->entry[additionalLines - 1].byteOffset, &index);

    switch (thresholdType) {
    case THRESHOLD_BYTE_OFFSET:    metricPtr = &byteOffset; break;
    case THRESHOLD_LINE_OFFSET:    metricPtr = &additionalLines; break;
    case THRESHOLD_PIXEL_DISTANCE: metricPtr = &info->pixels; break;
    }

    while (threshold >= *metricPtr) {
	DLine *dlPtr = LayoutDLine(&info->index, displayLineNo++);
	info->pixels += dlPtr->height;
	byteOffset += dlPtr->byteCount;
	info->numDispLines += 1;
	additionalLines -= 1;
	TkTextIndexForwBytes(textPtr, &info->index, dlPtr->byteCount, &info->index);
	InsertDLine(textPtr, info, dlPtr, viewHeight);

	if (IsStartOfNotMergedLine(&info->index)) {
	    info->isComplete = true;
	    break;
	}
    }

    info->entry = info->pixelInfo->dispLineInfo->entry + info->displayLineNo;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateDisplayInfo --
 *
 *	This function is invoked to recompute some or all of the DLine
 *	structures for a text widget. At the time it is called the DLine
 *	structures still left in the widget are guaranteed to be correct
 *	except that (a) the y-coordinates aren't necessarily correct, (b)
 *	there may be missing structures (the DLine structures get removed as
 *	soon as they are potentially out-of-date), and (c) DLine structures
 *	that don't start at the beginning of a line may be incorrect if
 *	previous information in the same line changed size in a way that moved
 *	a line boundary (DLines for any info that changed will have been
 *	deleted, but not DLines for unchanged info in the same text line).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Upon return, the DLine information for textPtr correctly reflects the
 *	positions where characters will be displayed. However, this function
 *	doesn't actually bring the display up-to-date.
 *
 *----------------------------------------------------------------------
 */

static bool
LineIsUpToDate(
    TkText *textPtr,
    DLine *dlPtr,
    unsigned lineMetricUpdateEpoch)
{
    const TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, TkTextIndexGetLine(&dlPtr->index));
    const TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;
    unsigned epoch = pixelInfo->epoch;

    assert(!(epoch & PARTIAL_COMPUTED_BIT) || dispLineInfo);

    return (epoch & EPOCH_MASK) == lineMetricUpdateEpoch
	    && (!dispLineInfo || dlPtr->displayLineNo < dispLineInfo->numDispLines);
}

static void
UpdateDisplayInfo(
    TkText *textPtr)	/* Text widget to update. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr;
    DLine *topLine;
    DLine *bottomLine;
    DLine *newTopLine;
    DLine *savedDLine;		/* usable saved display lines */
    DLine *prevSavedDLine;	/* last old unfreed display line */
    TkTextIndex index;
    TkTextLine *lastLinePtr;
    TkTextLine *linePtr;
    DisplayInfo info;
    int y, maxY, xPixelOffset, maxOffset;
    unsigned displayLineNo;
    unsigned epoch;

    if (!(dInfoPtr->flags & DINFO_OUT_OF_DATE)) {
	return;
    }
    dInfoPtr->flags &= ~DINFO_OUT_OF_DATE;

    /*
     * At first, update the default style, and reset cached chunk.
     */

    UpdateDefaultStyle(textPtr);
    dInfoPtr->currChunkPtr = NULL;

    /*
     * Release the cached display lines.
     */

    FreeDLines(textPtr, NULL, NULL, DLINE_CACHE);

    /*
     * Delete any DLines that are now above the top of the window.
     */

    index = textPtr->topIndex;
    prevSavedDLine = NULL;
    savedDLine = dInfoPtr->savedDLinePtr;

    if ((dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &index))) {
	/*
	 * Don't throw away unused display lines immediately, because it may happen
	 * that we will reuse some of them later.
	 */

	prevSavedDLine = FreeDLines(textPtr, dInfoPtr->dLinePtr, dlPtr, DLINE_SAVE);
    }

    /*
     * Scan through the contents of the window from top to bottom, recomputing
     * information for lines that are missing.
     */

    linePtr = TkTextIndexGetLine(&index);
    lastLinePtr = TkBTreeGetLastLine(textPtr);
    dlPtr = dInfoPtr->dLinePtr;
    topLine = bottomLine = NULL;
    y = dInfoPtr->y - dInfoPtr->newTopPixelOffset;
    maxY = dInfoPtr->maxY;
    newTopLine = NULL;
    epoch = dInfoPtr->lineMetricUpdateEpoch;
    dInfoPtr->maxLength = 0;

    if (IsStartOfNotMergedLine(&index)) {
	displayLineNo = 0;
    } else {
	ComputeDisplayLineInfo(textPtr, &index, &info);
	TkTextIndexBackBytes(textPtr, &index, info.byteOffset, &index);
	displayLineNo = info.displayLineNo;

	if (info.lastDLinePtr) {
	    /*
	     * Keep last produced display line, probably we can re-use it for new top line.
	     */

	    newTopLine = info.lastDLinePtr;
	    if (newTopLine->prevPtr) {
		newTopLine->prevPtr->nextPtr = NULL;
		newTopLine->prevPtr = NULL;
	    } else {
		assert(info.dLinePtr == info.lastDLinePtr);
		info.dLinePtr = info.lastDLinePtr = NULL;
	    }
	    assert(!newTopLine->nextPtr);
	}
	FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
    }

    /*
     * If we have saved display lines from a previous metric computation,
     * then we will search for the first re-usable line.
     */

    while (savedDLine && TkTextIndexCompare(&savedDLine->index, &index) < 0) {
	savedDLine = savedDLine->nextPtr;
    }

    /*
     * If we have a cached top-line, then insert this line into the list of saved lines.
     */

    if (newTopLine) {
	/*
	 * If we have a cached top-line, then it's not possible that this line
	 * is also in the list of saved lines (because ComputeDisplayLineInfo
	 * cannot produce a display line if the metric of this line is already
	 * known, and the line metric is known as soon as the line has been
	 * computed). This means that we can prepend this top-line to the list
	 * of saved lines.
	 */

	assert(!savedDLine || TkTextIndexCompare(&savedDLine->index, &newTopLine->index) > 0);

	if ((newTopLine->nextPtr = savedDLine)) {
	    newTopLine->prevPtr = savedDLine->prevPtr;
	    savedDLine->prevPtr = newTopLine;
	} else if (dInfoPtr->savedDLinePtr) {
	    dInfoPtr->lastSavedDLinePtr->nextPtr = newTopLine;
	    newTopLine->prevPtr = dInfoPtr->lastSavedDLinePtr;
	    dInfoPtr->lastSavedDLinePtr = newTopLine;
	}
	if (dInfoPtr->savedDLinePtr == savedDLine) {
	    dInfoPtr->savedDLinePtr = newTopLine;
	}
	if (!dInfoPtr->lastSavedDLinePtr) {
	    dInfoPtr->lastSavedDLinePtr = newTopLine;
	}

	savedDLine = newTopLine;
    } else {
	newTopLine = savedDLine;
    }

    if (newTopLine && !prevSavedDLine) {
	prevSavedDLine = newTopLine->prevPtr;
    }

    while (linePtr != lastLinePtr) {
	int cmp;

	/*
	 * There are three possibilities right now:
	 *
	 * (a) the next DLine (dlPtr) corresponds exactly to the next
	 *     information we want to display: just use it as-is.
	 *
	 * (b) the next DLine corresponds to a different line, or to a segment
	 *     that will be coming later in the same line: leave this DLine
	 *     alone in the hopes that we'll be able to use it later, then
	 *     create a new DLine in front of it.
	 *
	 * (c) the next DLine corresponds to a segment in the line we want,
	 *     but it's a segment that has already been processed or will
	 *     never be processed. Delete the DLine and try again.
	 *
	 * One other twist on all this. It's possible for 3D borders to
	 * interact between lines (see DisplayLineBackground) so if a line is
	 * relayed out and has styles with 3D borders, its neighbors have to
	 * be redrawn if they have 3D borders too, since the interactions
	 * could have changed (the neighbors don't have to be relayed out,
	 * just redrawn).
	 */

	if (!dlPtr
		|| TkTextIndexGetLine(&dlPtr->index) != linePtr
		|| !LineIsUpToDate(textPtr, dlPtr, epoch)
		|| (cmp = TkTextIndexCompare(&index, &dlPtr->index)) < 0) {
	    /*
	     * Case (b) -- must make new DLine.
	     */

	    TK_TEXT_DEBUG(LogTextRelayout(textPtr, &index));
	    if (savedDLine && TkTextIndexIsEqual(&index, &savedDLine->index)) {
		dlPtr = savedDLine;
		savedDLine = savedDLine->nextPtr;
		if (dInfoPtr->savedDLinePtr == dlPtr) {
		    dInfoPtr->savedDLinePtr = dlPtr->nextPtr;
		}
		if (dInfoPtr->lastSavedDLinePtr == dlPtr) {
		    dInfoPtr->lastSavedDLinePtr = dlPtr->prevPtr;
		}
		if (dlPtr->prevPtr) {
		    dlPtr->prevPtr->nextPtr = dlPtr->nextPtr;
		}
		if (dlPtr->nextPtr) {
		    dlPtr->nextPtr->prevPtr = dlPtr->prevPtr;
		}
		dlPtr->prevPtr = dlPtr->nextPtr = NULL;
		DEBUG(stats.numReused++);
	    } else {
		dlPtr = LayoutDLine(&index, displayLineNo);
	    }
	    assert(!(dlPtr->flags & (LINKED|CACHED|DELETED)));
	    if (!bottomLine) {
		if ((dlPtr->nextPtr = dInfoPtr->dLinePtr)) {
		    dInfoPtr->dLinePtr->prevPtr = dlPtr;
		}
		dInfoPtr->dLinePtr = dlPtr;
	    } else {
		if ((dlPtr->nextPtr = bottomLine->nextPtr)) {
		    bottomLine->nextPtr->prevPtr = dlPtr;
		}
		bottomLine->nextPtr = dlPtr;
		dlPtr->prevPtr = bottomLine;

		if (bottomLine->flags & HAS_3D_BORDER) {
		    bottomLine->flags |= OLD_Y_INVALID;
		}
	    }
	    DEBUG(dlPtr->flags |= LINKED);
	} else if (cmp == 0) {
	    /*
	     * Case (a) - can use existing display line as-is.
	     */

	    if (bottomLine && (dlPtr->flags & HAS_3D_BORDER) && (bottomLine->flags & NEW_LAYOUT)) {
		dlPtr->flags |= OLD_Y_INVALID;
	    }
	    assert(dlPtr->displayLineNo == displayLineNo);
	} else /* if (cmp > 0) */ {
	    /*
	     * Case (c) - dlPtr is useless. Discard it and start again with the next display line.
	     */

	    DLine *nextPtr = dlPtr->nextPtr;
	    FreeDLines(textPtr, dlPtr, nextPtr, DLINE_UNLINK);
	    dlPtr = nextPtr;
	    continue;
	}

	/*
	 * Advance to the start of the next display line.
	 */

	dlPtr->y = y;
	y += dlPtr->height;
	TkTextIndexForwBytes(textPtr, &index, dlPtr->byteCount, &index);
	linePtr = TkTextIndexGetLine(&index);

	if (linePtr->logicalLine && TkTextIndexIsStartOfLine(&index)) {
	    displayLineNo = 0;
	} else {
	    displayLineNo += 1;
	}

	bottomLine = dlPtr;
	dlPtr = dlPtr->nextPtr;

	/*
	 * It's important to have the following check here rather than in the
	 * while statement for the loop, so that there's always at least one
	 * DLine generated, regardless of how small the window is. This keeps
	 * a lot of other code from breaking.
	 */

	if (y >= maxY) {
	    break;
	}
    }

    /* Delete any DLine structures that don't fit on the screen. */
    FreeDLines(textPtr, dlPtr, NULL, DLINE_UNLINK);
    topLine = dInfoPtr->dLinePtr;

    /*
     * If there is extra space at the bottom of the window (because we've hit
     * the end of the text), then bring in more lines at the top of the
     * window, if there are any, to fill in the view.
     *
     * Since the top line may only be partially visible, we try first to
     * simply show more pixels from that line (newTopPixelOffset). If that
     * isn't enough, we have to layout more lines.
     */

    if (y < maxY) {
	/*
	 * This counts how many vertical pixels we have left to fill by
	 * pulling in more display pixels either from the first currently
	 * displayed, or the lines above it.
	 */

	int spaceLeft = maxY - y;

	if (spaceLeft <= dInfoPtr->newTopPixelOffset) {
	    /*
	     * We can fill up all the needed space just by showing more of the
	     * current top line.
	     */

	    dInfoPtr->newTopPixelOffset -= spaceLeft;
	    y += spaceLeft;
	    spaceLeft = 0;
	} else {
	    TkTextLine *linePtr;
	    TkTextLine *firstLinePtr;

	    /*
	     * Add in all of the current top line, which won't be enough to bring y
	     * up to maxY (if it was we would be in the 'if' block above).
	     */

	    y += dInfoPtr->newTopPixelOffset;
	    dInfoPtr->newTopPixelOffset = 0;
	    spaceLeft = maxY - y;

	    /*
	     * Layout an entire text line (potentially > 1 display line), then
	     * link in as many display lines as fit without moving the bottom
	     * line out of the window. Repeat this until all the extra space
	     * has been used up or we've reached the beginning of the text.
	     */

	    if (spaceLeft > 0) {
		firstLinePtr = TkBTreeGetStartLine(textPtr)->prevPtr;
		index = topLine ? topLine->index : textPtr->topIndex;
		savedDLine = prevSavedDLine;
		if (TkTextIndexBackBytes(textPtr, &index, 1, &index) == 1) {
		    firstLinePtr = linePtr = NULL; /* we are already at start of text */
		} else {
		    linePtr = TkTextIndexGetLine(&index);
		}

		for ( ; linePtr != firstLinePtr && spaceLeft > 0; linePtr = linePtr->prevPtr) {
		    if (linePtr != TkTextIndexGetLine(&index)) {
			TkTextIndexSetToLastChar2(&index, linePtr);
		    }
		    linePtr = ComputeDisplayLineInfo(textPtr, &index, &info);

		    do {
			if (info.lastDLinePtr) {
			    dlPtr = info.lastDLinePtr;
			    if (dlPtr->prevPtr) {
				dlPtr->prevPtr->nextPtr = NULL;
				info.lastDLinePtr = dlPtr->prevPtr;
				dlPtr->prevPtr = NULL;
				assert(dlPtr != info.dLinePtr);
			    } else {
				assert(info.dLinePtr == info.lastDLinePtr);
				info.dLinePtr = info.lastDLinePtr = NULL;
			    }
			} else {
			    TkTextIndexSetToStartOfLine2(&index, linePtr);
			    TkTextIndexForwBytes(textPtr, &index, info.entry->byteOffset, &index);
			    if (savedDLine && TkTextIndexIsEqual(&index, &savedDLine->index)) {
				dlPtr = savedDLine;
				savedDLine = savedDLine->prevPtr;
				if (dlPtr->prevPtr) {
				    dlPtr->prevPtr->nextPtr = dlPtr->nextPtr;
				} else {
				    dInfoPtr->savedDLinePtr = dlPtr->nextPtr;
				}
				if (dlPtr->nextPtr) {
				    dlPtr->nextPtr->prevPtr = dlPtr->prevPtr;
				} else {
				    dInfoPtr->lastSavedDLinePtr = dlPtr->prevPtr;
				}
				dlPtr->prevPtr = dlPtr->nextPtr = NULL;
			    } else {
				dlPtr = LayoutDLine(&index, info.displayLineNo);
			    }
			}
			if ((dlPtr->nextPtr = topLine)) {
			    topLine->prevPtr = dlPtr;
			} else {
			    bottomLine = dlPtr;
			}
			topLine = dlPtr;
			DEBUG(dlPtr->flags |= LINKED);
			TK_TEXT_DEBUG(LogTextRelayout(textPtr, &dlPtr->index));
			spaceLeft -= dlPtr->height;
			if (info.displayLineNo == 0) {
			    break;
			}
			info.displayLineNo -= 1;
			info.entry -= 1;
		    } while (spaceLeft > 0);

		    dInfoPtr->dLinePtr = topLine;
		    /* Delete remaining cached lines. */
		    FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
		}
	    }

	    /*
	     * We've either filled in the space we wanted to or we've run out
	     * of display lines at the top of the text. Note that we already
	     * set dInfoPtr->newTopPixelOffset to zero above.
	     */

	    if (spaceLeft < 0) {
		/*
		 * We've laid out a few too many vertical pixels at or above
		 * the first line. Therefore we only want to show part of the
		 * first displayed line, so that the last displayed line just
		 * fits in the window.
		 */

		dInfoPtr->newTopPixelOffset = -spaceLeft;

		/*
		 * If the entire first line we laid out is shorter than the new offset:
		 * this should not occur and would indicate a bad problem in the logic above.
		 */

		assert(dInfoPtr->newTopPixelOffset < topLine->height);
	    }
	}

	/*
	 * Now we're all done except that the y-coordinates in all the DLines
	 * are wrong and the top index for the text is wrong. Update them.
	 */

	if (topLine) {
	    dInfoPtr->dLinePtr = topLine;
	    y = dInfoPtr->y - dInfoPtr->newTopPixelOffset;
	    for (dlPtr = topLine; dlPtr; dlPtr = dlPtr->nextPtr) {
		assert(y <= dInfoPtr->maxY);
		dlPtr->y = y;
		y += dlPtr->height;
	    }
	}
    }

    /*
     * If the old top or bottom line has scrolled elsewhere on the screen, we
     * may not be able to re-use its old contents by copying bits (e.g., a
     * beveled edge that was drawn when it was at the top or bottom won't be
     * drawn when the line is in the middle and its neighbor has a matching
     * background). Similarly, if the new top or bottom line came from
     * somewhere else on the screen, we may not be able to copy the old bits.
     *
     * And don't forget to update the top index.
     */

    if (topLine) {
	textPtr->topIndex = topLine->index;
	assert(textPtr->topIndex.textPtr);
	TkTextIndexToByteIndex(&textPtr->topIndex);
	dInfoPtr->maxLength = MAX(dInfoPtr->maxLength, topLine->length);

	if ((topLine->flags & (TOP_LINE|HAS_3D_BORDER)) == HAS_3D_BORDER) {
	    topLine->flags |= OLD_Y_INVALID;
	}
	if ((bottomLine->flags & (BOTTOM_LINE|HAS_3D_BORDER)) == HAS_3D_BORDER) {
	    bottomLine->flags |= OLD_Y_INVALID;
	}

	if (topLine != bottomLine) {
	    topLine->flags &= ~BOTTOM_LINE;
	    bottomLine->flags &= ~TOP_LINE;

	    for (dlPtr = topLine->nextPtr; dlPtr != bottomLine; dlPtr = dlPtr->nextPtr) {
		dInfoPtr->maxLength = MAX(dInfoPtr->maxLength, dlPtr->length);

		if ((topLine->flags & HAS_3D_BORDER) && (dlPtr->flags & (TOP_LINE|BOTTOM_LINE))) {
		    dlPtr->flags |= OLD_Y_INVALID;
		}

		/*
		 * If the old top-line was not completely showing (i.e. the
		 * pixelOffset is non-zero) and is no longer the top-line, then we
		 * must re-draw it.
		 */

		if ((dlPtr->flags & TOP_LINE) && dInfoPtr->topPixelOffset != 0) {
		    dlPtr->flags |= OLD_Y_INVALID;
		}

		dlPtr->flags &= ~(TOP_LINE|BOTTOM_LINE);
	    }

	    dInfoPtr->maxLength = MAX(dInfoPtr->maxLength, bottomLine->length);
	}

	topLine->flags |= TOP_LINE;
	bottomLine->flags |= BOTTOM_LINE;

	dInfoPtr->topPixelOffset = dInfoPtr->newTopPixelOffset;
	dInfoPtr->curYPixelOffset = GetYPixelCount(textPtr, topLine);
	dInfoPtr->curYPixelOffset += dInfoPtr->topPixelOffset;
    } else {
	TkTextIndexSetupToStartOfText(&textPtr->topIndex, textPtr, textPtr->sharedTextPtr->tree);
    }

    dInfoPtr->lastDLinePtr = bottomLine;

    /*
     * Delete remaining saved lines.
     */

    FreeDLines(textPtr, dInfoPtr->savedDLinePtr, NULL, DLINE_FREE_TEMP);

    /*
     * Arrange for scrollbars to be updated.
     */

    textPtr->flags |= UPDATE_SCROLLBARS;

    /*
     * Deal with horizontal scrolling:
     * 1. If there's empty space to the right of the longest line, shift the
     *	  screen to the right to fill in the empty space.
     * 2. If the desired horizontal scroll position has changed, force a full
     *	  redisplay of all the lines in the widget.
     * 3. If the wrap mode isn't "none" then re-scroll to the base position.
     */

    maxOffset = dInfoPtr->maxLength - (dInfoPtr->maxX - dInfoPtr->x);
    xPixelOffset = MAX(0, MIN(dInfoPtr->newXPixelOffset, maxOffset));

    /*
     * Here's a problem: see the tests textDisp-29.2.1-4
     *
     * If the widget is being created, but has not yet been configured it will
     * have a maxY of 1 above, and we won't have examined all the lines
     * (just the first line, in fact), and so maxOffset will not be a true
     * reflection of the widget's lines. Therefore we must not overwrite the
     * original newXPixelOffset in this case.
     */

    if (!(((Tk_FakeWin *) (textPtr->tkwin))->flags & TK_NEED_CONFIG_NOTIFY)) {
	dInfoPtr->newXPixelOffset = xPixelOffset;
    }

    if (xPixelOffset != dInfoPtr->curXPixelOffset) {
	dInfoPtr->curXPixelOffset = xPixelOffset;
	for (dlPtr = topLine; dlPtr; dlPtr = dlPtr->nextPtr) {
	    dlPtr->flags |= OLD_Y_INVALID;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeDLines --
 *
 *	This function is called to free up all of the resources associated
 *	with one or more DLine structures.
 *
 * Results:
 *	Returns the last unfreed line if action is DLINE_SAVE, otherwise
 *	NULL will be returned.
 *
 * Side effects:
 *	Memory gets freed and various other resources are released.
 *
 *----------------------------------------------------------------------
 */

static bool
LineIsOutsideOfPeer(
    const TkText *textPtr,
    const TkTextIndex *indexPtr)
{
    const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;

    if (textPtr->startMarker != sharedTextPtr->startMarker) {
	const TkTextLine *linePtr = textPtr->startMarker->sectionPtr->linePtr;
	int no1 = TkTextIndexGetLineNumber(indexPtr, NULL);
	int no2 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr, NULL);

	if (no1 < no2) {
	    return true;
	}
    }
    if (textPtr->endMarker != sharedTextPtr->endMarker) {
	const TkTextLine *linePtr = textPtr->endMarker->sectionPtr->linePtr;
	int no1 = TkTextIndexGetLineNumber(indexPtr, NULL);
	int no2 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr, NULL);

	if (no1 > no2) {
	    return true;
	}
    }
    return false;
}

static void
ReleaseLines(
    TkText *textPtr,
    DLine *firstPtr,
    DLine *lastPtr,
    FreeDLineAction action)
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr, *lastDeletedPtr = NULL; /* avoids compiler warning */

    assert(firstPtr);
    assert(firstPtr != lastPtr);

    for (dlPtr = firstPtr; dlPtr != lastPtr; dlPtr = dlPtr->nextPtr) {
	TkTextDispChunk *chunkPtr;

	assert(!(dlPtr->flags & DELETED));
	assert((action == DLINE_UNLINK || action == DLINE_UNLINK_KEEP_BRKS)
		== !!(dlPtr->flags & LINKED));
	assert((action == DLINE_CACHE) == !!(dlPtr->flags & CACHED));
	assert(dlPtr != dInfoPtr->savedDLinePtr || dlPtr == firstPtr);
	assert(dlPtr->chunkPtr || (!dlPtr->lastChunkPtr && !dlPtr->breakInfo));

	if (dlPtr->lastChunkPtr) {
	    TkTextDispChunkSection *sectionPtr = NULL;

	    /*
	     * We have to destroy the chunks backward, because the context support
	     * is expecting this.
	     */

	    for (chunkPtr = dlPtr->lastChunkPtr; chunkPtr; chunkPtr = chunkPtr->prevPtr) {
		if (chunkPtr->layoutProcs->undisplayProc) {
		    chunkPtr->layoutProcs->undisplayProc(textPtr, chunkPtr);
		}
		LayoutReleaseChunk(textPtr, chunkPtr);
		DEBUG(chunkPtr->stylePtr = NULL);

		if (chunkPtr->sectionPtr != sectionPtr) {
		    sectionPtr = chunkPtr->sectionPtr;
		    sectionPtr->nextPtr = dInfoPtr->sectionPoolPtr;
		    dInfoPtr->sectionPoolPtr = sectionPtr;
		}
	    }

	    if (dlPtr->breakInfo
		    && (action != DLINE_UNLINK_KEEP_BRKS || LineIsOutsideOfPeer(textPtr, &dlPtr->index))
		    && --dlPtr->breakInfo->refCount == 0) {
		assert(dlPtr->breakInfo->brks);
		free(dlPtr->breakInfo->brks);
		free(dlPtr->breakInfo);
		Tcl_DeleteHashEntry(Tcl_FindHashEntry(
			&textPtr->sharedTextPtr->breakInfoTable,
			(void *) TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr,
			    TkTextIndexGetLine(&dlPtr->index))));
		DEBUG_ALLOC(tkTextCountDestroyBreakInfo++);
	    }

	    dlPtr->lastChunkPtr->nextPtr = dInfoPtr->chunkPoolPtr;
	    dInfoPtr->chunkPoolPtr = dlPtr->chunkPtr;
	    assert(!dInfoPtr->chunkPoolPtr->prevPtr);
	}

	lastDeletedPtr = dlPtr;
	DEBUG(dlPtr->flags = DELETED);
    }

    assert(lastDeletedPtr);
    lastDeletedPtr->nextPtr = dInfoPtr->dLinePoolPtr;
    dInfoPtr->dLinePoolPtr = firstPtr;

    if (lastPtr) {
	lastPtr->prevPtr = firstPtr->prevPtr;
    }
    if (firstPtr->prevPtr) {
	firstPtr->prevPtr->nextPtr = lastPtr;
    }
}

static DLine *
FreeDLines(
    TkText *textPtr,		/* Information about overall text widget. */
    DLine *firstPtr,		/* Pointer to first DLine to free up. */
    DLine *lastPtr,		/* Pointer to DLine just after last one to free (NULL means
    				 * everything starting with firstPtr). */
    FreeDLineAction action)	/* DLINE_UNLINK means DLines are currently linked into the list
    				 * rooted at textPtr->dInfoPtr->dLinePtr and they have to be
				 * unlinked. DLINE_FREE_TEMP, and DLINE_CACHE means the DLine given
				 * is just a temporary one and we shouldn't invalidate anything for
				 * the overall widget. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    switch (action) {
    case DLINE_CACHE:
	assert(!lastPtr);
	if (firstPtr) {
	    DLine *prevPtr = firstPtr->prevPtr;

	    assert(!(firstPtr->flags & LINKED));
	    assert(!(firstPtr->flags & CACHED));
	    assert(!(firstPtr->flags & DELETED));
	    assert(firstPtr != dInfoPtr->savedDLinePtr);

	    /*
	     * Firstly unlink this line from chain.
	     */

	    if (firstPtr == dInfoPtr->dLinePtr) {
		dInfoPtr->dLinePtr = firstPtr->nextPtr;
	    }
	    if (firstPtr == dInfoPtr->lastDLinePtr) {
		dInfoPtr->lastDLinePtr = prevPtr;
	    }
	    if (prevPtr) {
		prevPtr->nextPtr = firstPtr->nextPtr;
	    }
	    if (firstPtr->nextPtr) {
		firstPtr->nextPtr->prevPtr = prevPtr;
	    }
	    firstPtr->prevPtr = NULL;

	    /*
	     * Then link into the chain of cached lines.
	     */

	    if ((firstPtr->nextPtr = dInfoPtr->cachedDLinePtr)) {
		dInfoPtr->cachedDLinePtr->prevPtr = firstPtr;
	    } else {
		dInfoPtr->lastCachedDLinePtr = firstPtr;
	    }
	    dInfoPtr->cachedDLinePtr = firstPtr;

	    DEBUG(firstPtr->flags &= ~LINKED);
	    DEBUG(firstPtr->flags |= CACHED);
	    DEBUG(stats.numCached += 1);

	    if (dInfoPtr->numCachedLines < MAX_CACHED_DISPLAY_LINES) {
		dInfoPtr->numCachedLines += 1;
		return NULL;
	    }

	    /*
	     * Release oldest cached display line.
	     */

	    if ((firstPtr = dInfoPtr->lastCachedDLinePtr)) {
		firstPtr->prevPtr->nextPtr = NULL;
	    }
	    dInfoPtr->lastCachedDLinePtr = dInfoPtr->lastCachedDLinePtr->prevPtr;
	} else {
	    if (!(firstPtr = dInfoPtr->cachedDLinePtr)) {
		return NULL;
	    }
	    dInfoPtr->cachedDLinePtr = dInfoPtr->lastCachedDLinePtr = NULL;
	    dInfoPtr->numCachedLines = 0;
	}
	ReleaseLines(textPtr, firstPtr, lastPtr, action);
	break;
    case DLINE_METRIC:
	assert(!lastPtr);
	if (dInfoPtr->lastMetricDLinePtr) {
	    ReleaseLines(textPtr, dInfoPtr->lastMetricDLinePtr, NULL, DLINE_FREE_TEMP);
	    dInfoPtr->lastMetricDLinePtr = NULL;
	}
	if (firstPtr) {
	    assert(!firstPtr->nextPtr);
	    assert(!(firstPtr->flags & (LINKED|CACHED|DELETED)));
	    dInfoPtr->lastMetricDLinePtr = firstPtr;
	    if (firstPtr->prevPtr) {
		firstPtr->prevPtr->nextPtr = NULL;
		firstPtr->prevPtr = NULL;
	    }
	}
    	break;
    case DLINE_FREE_TEMP:
	if (!firstPtr || firstPtr == lastPtr) {
	    return NULL;
	}
	DEBUG(stats.lineHeightsRecalculated += 1);
	assert(!(firstPtr->flags & LINKED));
	assert(!(firstPtr->flags & CACHED));
	if (firstPtr == dInfoPtr->savedDLinePtr) {
	    dInfoPtr->savedDLinePtr = NULL;
	    if (!lastPtr) {
		dInfoPtr->lastSavedDLinePtr = NULL;
	    } else {
		dInfoPtr->savedDLinePtr = lastPtr;
	    }
	} else {
	    assert(!lastPtr || lastPtr != dInfoPtr->lastSavedDLinePtr);
	}
	assert(!dInfoPtr->savedDLinePtr == !dInfoPtr->lastSavedDLinePtr);
	ReleaseLines(textPtr, firstPtr, lastPtr, action);
	break;
    case DLINE_UNLINK:
    case DLINE_UNLINK_KEEP_BRKS:
	if (!firstPtr || firstPtr == lastPtr) {
	    return NULL;
	}
	assert(firstPtr->flags & LINKED);
	assert(firstPtr != dInfoPtr->savedDLinePtr);
	if (dInfoPtr->dLinePtr == firstPtr) {
	    if ((dInfoPtr->dLinePtr = lastPtr)) {
		lastPtr->prevPtr = NULL;
	    }
	} else {
	    DLine *prevPtr = firstPtr->prevPtr;

	    if (prevPtr && (prevPtr->nextPtr = lastPtr)) {
		lastPtr->prevPtr = prevPtr;
	    }
	}
	if (!lastPtr) {
	    dInfoPtr->lastDLinePtr = firstPtr->prevPtr;
	}
	dInfoPtr->dLinesInvalidated = true;
	assert(!dInfoPtr->dLinePtr || !dInfoPtr->dLinePtr->prevPtr);
	ReleaseLines(textPtr, firstPtr, lastPtr, action);
	break;
    case DLINE_SAVE: {
	unsigned epoch;
	DLine *dlPtr;

	if (!firstPtr || firstPtr == lastPtr) {
	    return NULL;
	}
	assert(firstPtr == dInfoPtr->dLinePtr);
	assert(lastPtr);

	epoch = dInfoPtr->lineMetricUpdateEpoch;

	assert(lastPtr->prevPtr);
	dInfoPtr->dLinePtr = lastPtr;

	/*
	 * Free all expired lines, we will only save valid lines.
	 */

	dlPtr = firstPtr;
	while (dlPtr != lastPtr) {
	    DLine *nextPtr = dlPtr->nextPtr;

	    assert(dlPtr->flags & LINKED);

	    if (LineIsUpToDate(textPtr, dlPtr, epoch)) {
		DEBUG(dlPtr->flags &= ~LINKED);
	    } else {
		if (dlPtr == firstPtr) {
		    firstPtr = nextPtr;
		}
		ReleaseLines(textPtr, dlPtr, nextPtr, DLINE_UNLINK);
	    }

	    dlPtr = nextPtr;
	}

	assert(!firstPtr->prevPtr);

	if (firstPtr == lastPtr) {
	    dInfoPtr->savedDLinePtr = NULL;
	    dInfoPtr->lastSavedDLinePtr = NULL;
	    return NULL;
	}

	lastPtr = lastPtr->prevPtr;
	lastPtr->nextPtr->prevPtr = NULL;
	lastPtr->nextPtr = NULL;

	if (!dInfoPtr->savedDLinePtr) {
	    dInfoPtr->savedDLinePtr = firstPtr;
	    dInfoPtr->lastSavedDLinePtr = lastPtr;
	} else if (TkTextIndexCompare(&lastPtr->index, &dInfoPtr->savedDLinePtr->index) < 0) {
	    lastPtr->nextPtr = dInfoPtr->savedDLinePtr;
	    dInfoPtr->savedDLinePtr->prevPtr = lastPtr;
	    dInfoPtr->savedDLinePtr = firstPtr;
	} else {
	    assert(TkTextIndexCompare(&firstPtr->index, &dInfoPtr->lastSavedDLinePtr->index) > 0);
	    firstPtr->prevPtr = dInfoPtr->lastSavedDLinePtr;
	    dInfoPtr->lastSavedDLinePtr->nextPtr = firstPtr;
	    dInfoPtr->lastSavedDLinePtr = lastPtr;
	}

	return lastPtr;
    }
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayDLine --
 *
 *	This function is invoked to draw a single line on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The line given by dlPtr is drawn at its correct position in textPtr's
 *	window. Note that this is one *display* line, not one *text* line.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayDLine(
    TkText *textPtr,	/* Text widget in which to draw line. */
    DLine *dlPtr,	/* Information about line to draw. */
    DLine *prevPtr,	/* Line just before one to draw, or NULL if dlPtr is the top line. */
    Pixmap pixmap)	/* Pixmap to use for double-buffering. Caller must make sure
			 * it's large enough to hold line. */
{
    TkTextDispChunk *chunkPtr;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    Display *display;
    StyleValues *sValuePtr;
    int lineHeight, yOffs;
    int yBase, height, baseline, screenY, xOffs;
    int extent1, extent2;
    int xIndent, rMargin;
    bool delayBlockCursorDrawing;

    if (!dlPtr->chunkPtr) {
	return;
    }

    display = Tk_Display(textPtr->tkwin);
    delayBlockCursorDrawing = false;

    lineHeight = dlPtr->height;
    if (lineHeight + dlPtr->y > dInfoPtr->maxY) {
	lineHeight = dInfoPtr->maxY - dlPtr->y;
    }
    if (dlPtr->y < dInfoPtr->y) {
	yOffs = dInfoPtr->y - dlPtr->y;
	lineHeight -= yOffs;
    } else {
	yOffs = 0;
    }

    /*
     * First, clear the area of the line to the background color for the text widget.
     */

    Tk_Fill3DRectangle(textPtr->tkwin, pixmap, textPtr->border, 0, 0,
	    Tk_Width(textPtr->tkwin), dlPtr->height, 0, TK_RELIEF_FLAT);

    /*
     * Second, draw background information for the whole line.
     */

    DisplayLineBackground(textPtr, dlPtr, prevPtr, pixmap);

    /*
     * Third, draw the background color of the left and right margins.
     */

    sValuePtr = dlPtr->firstCharChunkPtr->stylePtr->sValuePtr;
    rMargin = (sValuePtr->wrapMode == TEXT_WRAPMODE_NONE) ? 0 : sValuePtr->rMargin;
    xIndent = GetLeftLineMargin(dlPtr, sValuePtr);

    if (sValuePtr->lMarginColor != NULL) {
        Tk_Fill3DRectangle(textPtr->tkwin, pixmap, sValuePtr->lMarginColor, 0, 0,
                xIndent + dInfoPtr->x - dInfoPtr->curXPixelOffset,
                dlPtr->height, 0, TK_RELIEF_FLAT);
    }
    if (sValuePtr->rMarginColor != NULL) {
        Tk_Fill3DRectangle(textPtr->tkwin, pixmap, sValuePtr->rMarginColor,
                dInfoPtr->maxX - rMargin + dInfoPtr->curXPixelOffset,
                0, rMargin, dlPtr->height, 0, TK_RELIEF_FLAT);
    }

    yBase = dlPtr->spaceAbove;
    height = dlPtr->height - dlPtr->spaceAbove - dlPtr->spaceBelow;
    baseline = dlPtr->baseline - dlPtr->spaceAbove;
    screenY = dlPtr->y + dlPtr->spaceAbove;
    xOffs = dInfoPtr->x - dInfoPtr->curXPixelOffset;

    /*
     * Redraw the insertion cursor, if it is visible on this line. Must do it here rather
     * than in the foreground pass below because otherwise a wide insertion cursor will
     * obscure the character to its left.
     *
     * If the user has specified a foreground color for characters "behind" the block cursor,
     * we have to draw the cursor after the text has been drawn, see below.
     */

    if (dlPtr->cursorChunkPtr && textPtr->state == TK_TEXT_STATE_NORMAL) {
	delayBlockCursorDrawing = dInfoPtr->insertFgGC && TkTextDrawBlockCursor(textPtr);

	if (!delayBlockCursorDrawing) {
	    dlPtr->cursorChunkPtr->layoutProcs->displayProc(textPtr, dlPtr->cursorChunkPtr,
		    dlPtr->cursorChunkPtr->x + xOffs, yBase, height, baseline, display, pixmap, screenY);
	}
    }

    /*
     * Iterate through all of the chunks to redraw all of foreground information.
     */

    for (chunkPtr = dlPtr->chunkPtr; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
	if (chunkPtr == dlPtr->cursorChunkPtr) {
	    /* Don't display the insertion cursor, this will be done separately. */
	    continue;
	}

	if (chunkPtr->layoutProcs->displayProc) {
	    int x = chunkPtr->x + xOffs;

	    if (x + chunkPtr->width <= 0 || dInfoPtr->maxX <= x) {
		/*
		 * Note: we have to call the displayProc even for chunks that are off-screen.
		 * This is needed, for example, so that embedded windows can be unmapped in
		 * this case. Display the chunk at a coordinate that can be clearly identified
		 * by the displayProc as being off-screen to the left (the displayProc may not
		 * be able to tell if something is off to the right).
		 */

		x = -chunkPtr->width;
	    }

	    chunkPtr->layoutProcs->displayProc(textPtr, chunkPtr, x, yBase, height,
		    baseline, display, pixmap, screenY);

	    if (dInfoPtr->dLinesInvalidated) {
		/*
		 * The display process has invalidated any display line, so terminate,
		 * because the display process will be repeated with valid display lines.
		 */
		return;
	    }
	}
    }

    if (delayBlockCursorDrawing) {
	/*
	 * Draw the block cursor and redraw the characters "behind" the block cursor.
	 */

	int cxMin, cxMax, cWidth, cOffs;
	GC bgGC;

	assert(dInfoPtr->insertFgGC != None);

	cxMin = dlPtr->cursorChunkPtr->x + xOffs;
	cWidth = TkTextGetCursorWidth(textPtr, &cxMin, &cOffs);

	if (cWidth > 0) {
	    if ((bgGC = dlPtr->cursorChunkPtr->stylePtr->bgGC) == None) {
		Tk_3DBorder border;

		if (!(border = dlPtr->cursorChunkPtr->stylePtr->sValuePtr->border)) {
		    border = textPtr->border;
		}
		bgGC = Tk_GCForColor(Tk_3DBorderColor(border), Tk_WindowId(textPtr->tkwin));
	    }

	    cxMin += cOffs;
	    cxMax = cxMin + cWidth;

#if CLIPPING_IS_WORKING
	    /*
	     * This is the right implementation if XSetClipRectangles would work; still untested.
	     */
	    {
		XRectangle crect;

		crect.x = cxMin;
		crect.y = yBase;
		crect.width = cWidth;
		crect.height = height;

		XFillRectangle(display, pixmap, bgGC, crect.x, crect.y, crect.width, crect.height);
		dlPtr->cursorChunkPtr->layoutProcs->displayProc(textPtr, chunkPtr, cxMin, yBase, height,
			baseline, display, pixmap, screenY);

		/* for any reason this doesn't work with the Tk lib even under X11 */
		XSetClipRectangles(display, dInfoPtr->insertFgGC, 0, 0, &crect, 1, Unsorted);

		for (chunkPtr = dlPtr->chunkPtr; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
		    int x = chunkPtr->x + xOffs;

		    if (x >= cxMax) {
			break;
		    }
		    if (IsCharChunk(chunkPtr) && cxMin <= x + chunkPtr->width) {
			GC fgGC = chunkPtr->stylePtr->fgGC;
			XGCValues gcValues;
			unsigned long mask;

			/* Setup graphic context with font of this chunk. */
			mask = GCFont;
			gcValues.font = Tk_FontId(chunkPtr->stylePtr->sValuePtr->tkfont);
			XChangeGC(Tk_Display(textPtr->tkwin), dInfoPtr->insertFgGC, mask, &gcValues);

			chunkPtr->stylePtr->fgGC = dInfoPtr->insertFgGC;
			chunkPtr->layoutProcs->displayProc(textPtr, chunkPtr, x, yBase, height,
				baseline, display, pixmap, screenY);
			chunkPtr->stylePtr->fgGC = fgGC;
		    }
		}
	    }
#else /* if !CLIPPING_IS_WORKING */
	    /*
	     * We don't have clipping, so we need a different approach.
	     */
	    {
		Pixmap pm = Tk_GetPixmap(display, Tk_WindowId(textPtr->tkwin),
			cWidth, height, Tk_Depth(textPtr->tkwin));

		XFillRectangle(display, pm, bgGC, 0, 0, cWidth, height);
		chunkPtr = dlPtr->cursorChunkPtr;

		/* we are using a (pointer) hack in TkTextInsertDisplayProc */
		chunkPtr->layoutProcs->displayProc(textPtr, MarkPointer(chunkPtr),
			cxMin, yBase, height, baseline, display, pm, screenY);

		while (chunkPtr->prevPtr && chunkPtr->x + xOffs + chunkPtr->width > cxMin) {
		    chunkPtr = chunkPtr->prevPtr;
		}
		for ( ; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
		    int x = chunkPtr->x + xOffs;

		    if (x >= cxMax) {
			break;
		    }
		    if (IsCharChunk(chunkPtr)) {
			GC fgGC = chunkPtr->stylePtr->fgGC;
			GC eolGC = chunkPtr->stylePtr->eolGC;
			GC eotGC = chunkPtr->stylePtr->eotGC;
			XGCValues gcValues;
			unsigned long mask;

			/* Setup graphic context with font of this chunk. */
			mask = GCFont;
			gcValues.font = Tk_FontId(chunkPtr->stylePtr->sValuePtr->tkfont);
			XChangeGC(Tk_Display(textPtr->tkwin), dInfoPtr->insertFgGC, mask, &gcValues);

			chunkPtr->stylePtr->fgGC = dInfoPtr->insertFgGC;
			chunkPtr->stylePtr->eolGC = dInfoPtr->insertFgGC;
			chunkPtr->stylePtr->eotGC = dInfoPtr->insertFgGC;
			chunkPtr->layoutProcs->displayProc(textPtr, chunkPtr, x - cxMin, 0,
				height, baseline, display, pm, screenY);
			chunkPtr->stylePtr->fgGC = fgGC;
			chunkPtr->stylePtr->eolGC = eolGC;
			chunkPtr->stylePtr->eotGC = eotGC;
		    }
		}

		XCopyArea(display, pm, pixmap, dInfoPtr->copyGC, 0, 0, cWidth, height, cxMin, yBase);
		Tk_FreePixmap(display, pm);
	    }
#endif /* CLIPPING_IS_WORKING */
	}
    }

    /*
     * Copy the pixmap onto the screen. If this is the first or last line on
     * the screen then copy a piece of the line, so that it doesn't overflow
     * into the border area.
     *
     * Another special trick: consider the padding area left/right of the line;
     * this is because the insertion cursor sometimes overflows onto that area
     * and we want to get as much of the cursor as possible.
     */

    extent1 = MIN(textPtr->padX, textPtr->insertWidth/2);
    extent2 = MIN(textPtr->padX, (textPtr->insertWidth + 1)/2);
    XCopyArea(display, pixmap, Tk_WindowId(textPtr->tkwin), dInfoPtr->copyGC,
	    dInfoPtr->x - extent1, yOffs, dInfoPtr->maxX - dInfoPtr->x + extent1 + extent2, lineHeight,
	    dInfoPtr->x - extent1, dlPtr->y + yOffs);

    DEBUG(stats.linesRedrawn += 1);
}

/*
 *--------------------------------------------------------------
 *
 * DisplayLineBackground --
 *
 *	This function is called to fill in the background for a display line.
 *	It draws 3D borders cleverly so that adjacent chunks with the same
 *	style (whether on the same line or different lines) have a single 3D
 *	border around the whole region.
 *
 * Results:
 *	There is no return value. Pixmap is filled in with background
 *	information for dlPtr.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

/*
 * The following function determines whether two styles have the same background
 * so that, for example, no beveled border should be drawn between them.
 */

static bool
SameBackground(
    const TextStyle *s1,
    const TextStyle *s2)
{
    return s1->sValuePtr->border == s2->sValuePtr->border
	&& s1->sValuePtr->borderWidth == s2->sValuePtr->borderWidth
	&& s1->sValuePtr->relief == s2->sValuePtr->relief
	&& s1->sValuePtr->bgStipple == s2->sValuePtr->bgStipple
	&& s1->sValuePtr->indentBg == s2->sValuePtr->indentBg;
}

static void
DisplayLineBackground(
    TkText *textPtr,		/* Text widget containing line. */
    DLine *dlPtr,		/* Information about line to draw. */
    DLine *prevPtr,		/* Line just above dlPtr, or NULL if dlPtr is the top-most line in
    				 * the window. */
    Pixmap pixmap)		/* Pixmap to use for double-buffering. Caller must make sure it's
    				 * large enough to hold line. Caller must also have filled it with
				 * the background color for the widget. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextDispChunk *chunkPtr;	/* Pointer to chunk in the current line. */
    TkTextDispChunk *chunkPtr2;	/* Pointer to chunk in the line above or below the current one. NULL
    				 * if we're to the left of or to the right of the chunks in the line. */
    TkTextDispChunk *nextPtr2;	/* Next chunk after chunkPtr2 (it's not the same as chunkPtr2->nextPtr
    				 * in the case where chunkPtr2 is NULL because the line is indented). */
    int leftX;			/* The left edge of the region we're currently working on. */
    int leftXIn;		/* 0 means beveled edge at leftX slopes right as it goes down,
    				 * 1 means it slopes left as it goes down. */
    int rightX;			/* Right edge of chunkPtr. */
    int rightX2;		/* Right edge of chunkPtr2. */
    int matchLeft;		/* Does the style of this line match that of its neighbor just to the
    				 * left of the current x coordinate? */
    int matchRight;		/* Does line's style match its neighbor just to the right of the
    				 * current x-coord? */
    int minX, maxX, xOffset, xIndent, borderWidth;
    StyleValues *sValuePtr;
    Display *display;
    const int y = 0;

    /*
     * Pass 1: scan through dlPtr from left to right. For each range of chunks
     * with the same style, draw the main background for the style plus the
     * vertical parts of the 3D borders (the left and right edges).
     */

    display = Tk_Display(textPtr->tkwin);
    minX = dInfoPtr->curXPixelOffset;
    xOffset = dInfoPtr->x - minX;
    maxX = minX + dInfoPtr->maxX - dInfoPtr->x;
    chunkPtr = dlPtr->chunkPtr;
    xIndent = 0;

    /*
     * Note A: in the following statement, and a few others later in this file
     * marked with "See Note A above", the right side of the assignment was
     * replaced with 0 on 6/18/97. This has the effect of highlighting the
     * empty space to the left of a line whenever the leftmost character of
     * the line is highlighted. This way, multi-line highlights always line up
     * along their left edges. However, this may look funny in the case where
     * a single word is highlighted. To undo the change, replace "leftX = 0"
     * with "leftX = chunkPtr->x" and "rightX2 = 0" with "rightX2 =
     * nextPtr2->x" here and at all the marked points below. This restores the
     * old behavior where empty space to the left of a line is not
     * highlighted, leaving a ragged left edge for multi-line highlights.
     */

    for (leftX = 0; leftX < maxX; chunkPtr = chunkPtr->nextPtr) {
	if (chunkPtr->nextPtr && SameBackground(chunkPtr->nextPtr->stylePtr, chunkPtr->stylePtr)) {
	    continue;
	}
	sValuePtr = chunkPtr->stylePtr->sValuePtr;
	rightX = chunkPtr->x + chunkPtr->width;
	if (!chunkPtr->nextPtr && rightX < maxX) {
	    rightX = maxX;
	}
	if (chunkPtr->stylePtr->bgGC != None) {
	    int indent = 0;

	    /*
	     * Not visible - bail out now.
	     */

	    if (rightX + xOffset <= 0) {
		leftX = rightX;
		continue;
	    }

	    /*
	     * Compute additional offset if -indentbackground is set.
	     */

	    if (leftX == 0 && sValuePtr->indentBg) {
		xIndent = GetLeftLineMargin(dlPtr, sValuePtr);
		if (leftX + xIndent > rightX) {
		    xIndent = rightX - leftX;
		}
		indent = xIndent;
	    }

	    /*
	     * Trim the start position for drawing to be no further away than -borderWidth.
	     * The reason is that on many X servers drawing from -32768 (or less) to
	     * +something simply does not display correctly. [Patch #541999]
	     */

	    if (leftX + xOffset + indent < -sValuePtr->borderWidth) {
		leftX = -sValuePtr->borderWidth - xOffset - indent;
	    }
	    if (rightX - leftX - indent > 32767) {
		rightX = leftX + indent + 32767;
	    }

            /*
             * Prevent the borders from leaking on adjacent characters,
             * which would happen for too large border width.
             */

            borderWidth = sValuePtr->borderWidth;
            if (leftX + sValuePtr->borderWidth > rightX) {
                borderWidth = rightX - leftX;
            }

	    XFillRectangle(display, pixmap, chunkPtr->stylePtr->bgGC,
		    leftX + xOffset + indent, y, rightX - leftX - indent, dlPtr->height);
	    if (sValuePtr->relief != TK_RELIEF_FLAT) {
		Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
			leftX + xOffset + indent, y, borderWidth,
			dlPtr->height, 1, sValuePtr->relief);
		Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
			rightX - borderWidth + xOffset, y, borderWidth,
			dlPtr->height, 0, sValuePtr->relief);
	    }
	}
	leftX = rightX;
    }

    /*
     * Pass 2: draw the horizontal bevels along the top of the line. To do
     * this, scan through dlPtr from left to right while simultaneously
     * scanning through the line just above dlPtr. ChunkPtr2 and nextPtr2
     * refer to two adjacent chunks in the line above.
     */

    chunkPtr = dlPtr->chunkPtr;
    leftX = 0;				/* See Note A above. */
    leftXIn = 1;
    rightX = chunkPtr->x + chunkPtr->width;
    if (!chunkPtr->nextPtr && rightX < maxX) {
	rightX = maxX;
    }
    chunkPtr2 = NULL;
    if (prevPtr && prevPtr->chunkPtr) {
	/*
	 * Find the chunk in the previous line that covers leftX.
	 */

	nextPtr2 = prevPtr->chunkPtr;
	rightX2 = 0;			/* See Note A above. */
	while (rightX2 <= leftX) {
	    if (!(chunkPtr2 = nextPtr2)) {
		break;
	    }
	    nextPtr2 = chunkPtr2->nextPtr;
	    rightX2 = chunkPtr2->x + chunkPtr2->width;
	    if (!nextPtr2) {
		rightX2 = INT_MAX;
	    }
	}
    } else {
	nextPtr2 = NULL;
	rightX2 = INT_MAX;
    }

    while (leftX < maxX) {
	matchLeft = chunkPtr2 && SameBackground(chunkPtr2->stylePtr, chunkPtr->stylePtr);
	sValuePtr = chunkPtr->stylePtr->sValuePtr;
	if (rightX <= rightX2) {
	    /*
	     * The chunk in our line is about to end. If its style changes
	     * then draw the bevel for the current style.
	     */

	    if (!chunkPtr->nextPtr
		    || !SameBackground(chunkPtr->stylePtr, chunkPtr->nextPtr->stylePtr)) {
		if (!matchLeft && sValuePtr->relief != TK_RELIEF_FLAT) {
		    int indent = (leftX == 0) ? xIndent : 0;
		    Tk_3DHorizontalBevel(textPtr->tkwin, pixmap,
			    sValuePtr->border, leftX + xOffset + indent, y,
			    rightX - leftX - indent, sValuePtr->borderWidth,
			    leftXIn, 1, 1, sValuePtr->relief);
		}
		leftX = rightX;
		leftXIn = 1;

		/*
		 * If the chunk in the line above is also ending at the same
		 * point then advance to the next chunk in that line.
		 */

		if (rightX == rightX2 && chunkPtr2) {
		    goto nextChunk2;
		}
	    }
	    chunkPtr = chunkPtr->nextPtr;
	    if (!chunkPtr) {
		break;
	    }
	    rightX = chunkPtr->x + chunkPtr->width;
	    if (!chunkPtr->nextPtr && rightX < maxX) {
		rightX = maxX;
	    }
	    continue;
	}

	/*
	 * The chunk in the line above is ending at an x-position where there
	 * is no change in the style of the current line. If the style above
	 * matches the current line on one side of the change but not on the
	 * other, we have to draw an L-shaped piece of bevel.
	 */

	matchRight = nextPtr2 && SameBackground(nextPtr2->stylePtr, chunkPtr->stylePtr);
	if (matchLeft && !matchRight) {
            borderWidth = sValuePtr->borderWidth;
            if (rightX2 - borderWidth < leftX) {
                borderWidth = rightX2 - leftX;
            }
	    if (sValuePtr->relief != TK_RELIEF_FLAT) {
		Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
			rightX2 - borderWidth + xOffset, y, borderWidth,
			sValuePtr->borderWidth, 0, sValuePtr->relief);
	    }
	    leftX = rightX2 - borderWidth;
	    leftXIn = 0;
	} else if (!matchLeft && matchRight && sValuePtr->relief != TK_RELIEF_FLAT) {
	    int indent = (leftX == 0) ? xIndent : 0;
            borderWidth = sValuePtr->borderWidth;
            if (rightX2 + borderWidth > rightX) {
                borderWidth = rightX - rightX2;
            }
	    Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border, rightX2 + xOffset,
		    y, borderWidth, sValuePtr->borderWidth, 1, sValuePtr->relief);
	    Tk_3DHorizontalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
		    leftX + xOffset + indent, y,
		    rightX2 + borderWidth - leftX - indent,
		    sValuePtr->borderWidth, leftXIn, 0, 1,
		    sValuePtr->relief);
	}

    nextChunk2:
	chunkPtr2 = nextPtr2;
	if (!chunkPtr2) {
	    rightX2 = INT_MAX;
	} else {
	    nextPtr2 = chunkPtr2->nextPtr;
	    rightX2 = chunkPtr2->x + chunkPtr2->width;
	    if (!nextPtr2) {
		rightX2 = INT_MAX;
	    }
	}
    }

    /*
     * Pass 3: draw the horizontal bevels along the bottom of the line. This
     * uses the same approach as pass 2.
     */

    chunkPtr = dlPtr->chunkPtr;
    leftX = 0;				/* See Note A above. */
    leftXIn = 0;
    rightX = chunkPtr->x + chunkPtr->width;
    if (!chunkPtr->nextPtr && rightX < maxX) {
	rightX = maxX;
    }
    chunkPtr2 = NULL;
    if (dlPtr->nextPtr && dlPtr->nextPtr->chunkPtr) {
	/*
	 * Find the chunk in the next line that covers leftX.
	 */

	nextPtr2 = dlPtr->nextPtr->chunkPtr;
	rightX2 = 0;			/* See Note A above. */
	while (rightX2 <= leftX) {
	    chunkPtr2 = nextPtr2;
	    if (!chunkPtr2) {
		break;
	    }
	    nextPtr2 = chunkPtr2->nextPtr;
	    rightX2 = chunkPtr2->x + chunkPtr2->width;
	    if (!nextPtr2) {
		rightX2 = INT_MAX;
	    }
	}
    } else {
	nextPtr2 = NULL;
	rightX2 = INT_MAX;
    }

    while (leftX < maxX) {
	matchLeft = chunkPtr2 && SameBackground(chunkPtr2->stylePtr, chunkPtr->stylePtr);
	sValuePtr = chunkPtr->stylePtr->sValuePtr;
	if (rightX <= rightX2) {
	    if (!chunkPtr->nextPtr
		    || !SameBackground(chunkPtr->stylePtr, chunkPtr->nextPtr->stylePtr)) {
		if (!matchLeft && sValuePtr->relief != TK_RELIEF_FLAT) {
		    int indent = (leftX == 0) ? xIndent : 0;
		    Tk_3DHorizontalBevel(textPtr->tkwin, pixmap,
			    sValuePtr->border, leftX + xOffset + indent,
			    y + dlPtr->height - sValuePtr->borderWidth,
			    rightX - leftX - indent, sValuePtr->borderWidth,
			    leftXIn, 0, 0, sValuePtr->relief);
		}
		leftX = rightX;
		leftXIn = 0;
		if (rightX == rightX2 && chunkPtr2) {
		    goto nextChunk2b;
		}
	    }
	    chunkPtr = chunkPtr->nextPtr;
	    if (!chunkPtr) {
		break;
	    }
	    rightX = chunkPtr->x + chunkPtr->width;
	    if (!chunkPtr->nextPtr && rightX < maxX) {
		rightX = maxX;
	    }
	    continue;
	}

	matchRight = nextPtr2 && SameBackground(nextPtr2->stylePtr, chunkPtr->stylePtr);
	if (matchLeft && !matchRight) {
            borderWidth = sValuePtr->borderWidth;
            if (rightX2 - borderWidth < leftX) {
                borderWidth = rightX2 - leftX;
            }
	    if (sValuePtr->relief != TK_RELIEF_FLAT) {
		Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
			rightX2 - borderWidth + xOffset,
			y + dlPtr->height - sValuePtr->borderWidth,
			borderWidth, sValuePtr->borderWidth, 0,
			sValuePtr->relief);
	    }
	    leftX = rightX2 - borderWidth;
	    leftXIn = 1;
	} else if (!matchLeft && matchRight && sValuePtr->relief != TK_RELIEF_FLAT) {
	    int indent = (leftX == 0) ? xIndent : 0;
            borderWidth = sValuePtr->borderWidth;
            if (rightX2 + borderWidth > rightX) {
                borderWidth = rightX - rightX2;
            }
	    Tk_3DVerticalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
		    rightX2 + xOffset, y + dlPtr->height - sValuePtr->borderWidth,
		    borderWidth, sValuePtr->borderWidth, 1, sValuePtr->relief);
	    Tk_3DHorizontalBevel(textPtr->tkwin, pixmap, sValuePtr->border,
		    leftX + xOffset + indent, y + dlPtr->height - sValuePtr->borderWidth,
		    rightX2 + borderWidth - leftX - indent, sValuePtr->borderWidth,
		    leftXIn, 1, 0, sValuePtr->relief);
	}

    nextChunk2b:
	chunkPtr2 = nextPtr2;
	if (!chunkPtr2) {
	    rightX2 = INT_MAX;
	} else {
	    nextPtr2 = chunkPtr2->nextPtr;
	    rightX2 = chunkPtr2->x + chunkPtr2->width;
	    if (!nextPtr2) {
		rightX2 = INT_MAX;
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AsyncUpdateLineMetrics --
 
 *	This function is invoked as a background handler to update the pixel-
 *	height calculations of individual lines in an asychronous manner.
 *
 *	Currently a timer-handler is used for this purpose, which continuously
 *	reschedules itself. It may well be better to use some other approach
 *	(e.g., a background thread). We can't use an idle-callback because of
 *	a known bug in Tcl/Tk in which idle callbacks are not allowed to
 *	re-schedule themselves. This just causes an effective infinite loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Line heights may be recalculated.
 *
 *----------------------------------------------------------------------
 */

static void
AsyncUpdateLineMetrics(
    ClientData clientData)	/* Information about widget. */
{
    TkText *textPtr = clientData;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    dInfoPtr->lineUpdateTimer = NULL;

    if (TkTextReleaseIfDestroyed(textPtr)) {
	return;
    }

    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	/* not yet configured */
    } else if (dInfoPtr->flags & REDRAW_PENDING) {
	dInfoPtr->flags |= ASYNC_PENDING|ASYNC_UPDATE;
    } else {
	/*
	 * Update the lines in blocks of about 24 recalculations, or 250+ lines
	 * examined, so we pass in 256 for 'doThisMuch'.
	 */

	UpdateLineMetrics(textPtr, 256);
	TK_TEXT_DEBUG(LogTextInvalidateLine(textPtr, 0));

	if (TkRangeListIsEmpty(dInfoPtr->lineMetricUpdateRanges)) {
	    /*
	     * We have looped over all lines, so we're done. We must release our
	     * refCount on the widget (the timer token was already set to NULL
	     * above). If there is a registered aftersync command, run that first.
	     */

	    if (!dInfoPtr->pendingUpdateLineMetricsFinished) {
		UpdateLineMetricsFinished(textPtr, false);
		GetYView(textPtr->interp, textPtr, true);
	    }
	} else {
	    /*
	     * Re-arm the timer. We already have a refCount on the text widget so no
	     * need to adjust that.
	     */

	    dInfoPtr->lineUpdateTimer = Tcl_CreateTimerHandler(1, AsyncUpdateLineMetrics, textPtr);
	    return;
	}
    }

    TkTextDecrRefCountAndTestIfDestroyed(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateLineMetrics --
 *
 *	This function updates the pixel height calculations of a range of
 *	lines in the widget. The range is from lineNum to endLine, but
 *	this function may return earlier, once a certain number of lines
 *	has been examined. The line counts are from 0.
 *
 * Results:
 *	The index of the last line examined (or zero if we are about to wrap
 *	around from end to beginning of the widget).
 *
 * Side effects:
 *	Line heights may be recalculated.
 *
 *----------------------------------------------------------------------
 */

static unsigned
NextLineNum(
    TkTextLine *linePtr,
    unsigned lineNum,
    const TkTextIndex *indexPtr)
{
    TkText *textPtr;

    assert(indexPtr->textPtr);

    if (linePtr->nextPtr == TkTextIndexGetLine(indexPtr)) {
	return lineNum + 1;
    }

    textPtr = indexPtr->textPtr;
    return TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, TkTextIndexGetLine(indexPtr), NULL);
}

static void
UpdateLineMetrics(
    TkText *textPtr,		/* Information about widget. */
    unsigned doThisMuch)	/* How many lines to check, or how many 10s of lines to recalculate. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    const TkRange *range = TkRangeListFirst(dInfoPtr->lineMetricUpdateRanges);
    unsigned maxDispLines = UINT_MAX;
    unsigned count = 0;

    assert(textPtr->sharedTextPtr->allowUpdateLineMetrics);

    while (range) {
	TkTextLine *linePtr;
	TkTextLine *logicalLinePtr;
	int lineNum = range->low;
	int high = range->high;

	linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum);
	logicalLinePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);

	if (linePtr != logicalLinePtr) {
	    lineNum = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, logicalLinePtr, NULL);
	    linePtr = logicalLinePtr;
	}

	while (lineNum <= high) {
	    TkTextPixelInfo *pixelInfo;

	    TK_TEXT_DEBUG(LogTextInvalidateLine(textPtr, count));

	    /*
	     * Now update the line's metrics if necessary.
	     */

	    pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);

	    if (pixelInfo->epoch == dInfoPtr->lineMetricUpdateEpoch) {
		int firstLineNum = lineNum;

		/*
		 * This line is already up to date. That means there's nothing to do here.
		 */

		if (linePtr->nextPtr->logicalLine) {
		    linePtr = linePtr->nextPtr;
		    lineNum += 1;
		} else {
		    linePtr = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
		    lineNum = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, NULL);
		}

		TkRangeListRemove(dInfoPtr->lineMetricUpdateRanges, firstLineNum, lineNum - 1);
	    } else {
		TkTextIndex index;

		TkTextIndexClear(&index, textPtr);
		TkTextIndexSetToStartOfLine2(&index, linePtr);

		/*
		 * Update the line and update the counter, counting 8 for each display line
		 * we actually re-layout. But in case of synchronous update we do a full
		 * computation.
		 */

		if (textPtr->syncTime > 0) {
		    maxDispLines = (doThisMuch - count + 7)/8;
		}
		count += 8*UpdateOneLine(textPtr, linePtr, &index, maxDispLines);

		if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
		    /*
		     * We didn't complete the logical line, because it produced very many
		     * display lines - it must be a long line wrapped many times.
		     */
		    return;
		}

		/*
		 * We're done with this line.
		 */

		lineNum = NextLineNum(linePtr, lineNum, &index);
		linePtr = TkTextIndexGetLine(&index);
	    }

	    if ((++count >= doThisMuch)) {
		return;
	    }
	}

	/* The update process has removed the finished lines. */
	range = TkRangeListFirst(dInfoPtr->lineMetricUpdateRanges);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextUpdateLineMetrics --
 *
 *	This function updates the pixel height calculations of a range of
 *	lines in the widget. The range is from lineNum to endLine. The line
 *	counts are from 0.
 *
 *	All lines in the range will be updated. This will potentially take
 *	quite some time for a large range of lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Line heights may be recalculated.
 *
 *----------------------------------------------------------------------
 */

void
TkTextUpdateLineMetrics(
    TkText *textPtr,		/* Information about widget. */
    unsigned lineNum,		/* Start at this line. */
    unsigned endLine)		/* Go no further than this line. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    const TkRange *range;

    assert(lineNum <= endLine);
    assert((int) endLine <= TkBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr));
    assert(textPtr->sharedTextPtr->allowUpdateLineMetrics);

    dInfoPtr->insideLineMetricUpdate = true;

    if ((range = TkRangeListFindNearest(dInfoPtr->lineMetricUpdateRanges, lineNum))) {
	TkTextLine *linePtr = NULL;
	unsigned count = 0;
	unsigned high = range->high;

	lineNum = range->low;
	endLine = MIN((int) endLine, TkBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr) - 1);

	while (true) {
	    const TkTextPixelInfo *pixelInfo;
	    int firstLineNum;

	    if (lineNum > high) {
		/*
		 * Note that the update process has removed the finished lines.
		 */

		if (!(range = TkRangeListFindNearest(dInfoPtr->lineMetricUpdateRanges, lineNum))) {
		    break;
		}
		linePtr = NULL;
		lineNum = range->low;
		high = range->high;
	    }

	    if (lineNum > endLine) {
		break;
	    }

	    if (!linePtr) {
		linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum);
		linePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
	    }

	    TK_TEXT_DEBUG(LogTextInvalidateLine(textPtr, count));
	    assert(linePtr->nextPtr);

	    pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);

	    if (pixelInfo->epoch != dInfoPtr->lineMetricUpdateEpoch) {
		TkTextIndex index;

		/*
		 * This line is not (fully) up-to-date.
		 */

		TkTextIndexClear(&index, textPtr);
		TkTextIndexSetToStartOfLine2(&index, linePtr);
		UpdateOneLine(textPtr, linePtr, &index, UINT_MAX);
		assert(IsStartOfNotMergedLine(&index) || TkTextIndexIsEndOfText(&index));
		firstLineNum = -1; /* the update has removed the line numbers from range list */
	    } else {
		firstLineNum = lineNum;
	    }

	    if (linePtr->nextPtr->logicalLine) {
		linePtr = linePtr->nextPtr;
		lineNum += 1;
	    } else {
		linePtr = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
		lineNum = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, NULL);
	    }

	    if (firstLineNum >= 0) {
		TkRangeListRemove(dInfoPtr->lineMetricUpdateRanges, firstLineNum, lineNum - 1);
	    }
	}
    }

    dInfoPtr->insideLineMetricUpdate = false;
    CheckIfLineMetricIsUpToDate(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextInvalidateLineMetrics, TextInvalidateLineMetrics --
 *
 *	Mark a number of text lines as having invalid line metric
 *	calculations. Depending on 'action' which indicates whether
 *	the given lines are simply invalid or have been inserted or
 *	deleted, the pre-existing asynchronous line update range may
 *	need to be adjusted.
 *
 *	If linePtr is NULL then 'lineCount' and 'action' are ignored
 *	and all lines are invalidated.
 *
 *	If linePtr is the last (artificial) line, then do nothing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May schedule an asychronous callback.
 *
 *----------------------------------------------------------------------
 */

static void
ResetPixelInfo(
    TkTextPixelInfo *pixelInfo)
{
    TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;

    if (dispLineInfo) {
	if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
	    dispLineInfo->numDispLines = dispLineInfo->entry[dispLineInfo->numDispLines].pixels;
	}
    }
    pixelInfo->epoch = 0;
}

static void
StartAsyncLineCalculation(
    TkText *textPtr)
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	return;
    }

    /*
     * Reset cached chunk.
     */

    dInfoPtr->currChunkPtr = NULL;
    InvokeAsyncUpdateLineMetrics(textPtr);

    if (!(dInfoPtr->flags & ASYNC_UPDATE)) {
	dInfoPtr->flags |= ASYNC_UPDATE;
	TkTextGenerateWidgetViewSyncEvent(textPtr, false);
    }
}

static void
TextInvalidateLineMetrics(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextLine *linePtr,	/* Invalidation starts from this line; can be NULL, but only in
    				 * case of simple invalidation. */
    unsigned lineCount,		/* And includes this amount of following lines. */
    TkTextInvalidateAction action)
				/* Indicates what type of invalidation occurred (insert, delete,
				 * or simple). */
{
    TkRangeList *ranges = textPtr->dInfoPtr->lineMetricUpdateRanges;
    unsigned totalLines = TkBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr);
    unsigned epoch = textPtr->dInfoPtr->lineMetricUpdateEpoch;
    bool isMonospaced = UseMonospacedLineHeights(textPtr);
    unsigned lineNum = 0; /* suppress compiler warning */

    assert(linePtr || action == TK_TEXT_INVALIDATE_ONLY);
    assert(TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, NULL) + lineCount
	    < totalLines + (action == TK_TEXT_INVALIDATE_INSERT));

    if (linePtr) {
	int deviation;

	lineNum = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, linePtr, &deviation);

	assert(lineNum < totalLines);
	assert(deviation >= 0);

	if (deviation) {
	    lineCount -= MIN((int) lineCount, deviation);
	}

	if (action != TK_TEXT_INVALIDATE_ONLY
	    	&& !isMonospaced
		&& linePtr == TkBTreeGetStartLine(textPtr)
		&& lineCount + 1 >= totalLines) {
	    linePtr = NULL;
	}
    } else if (isMonospaced) {
	linePtr = TkBTreeGetStartLine(textPtr);
	lineCount = totalLines;
    }

    if (linePtr) {
	if (TkRangeListSize(ranges) >= 200) {
	    /*
	     * The range list is a great data structure for fast management of update
	     * information, but this list is not designed for a large amount of entries.
	     * If this arbitrarily chosen number of entries has been reached we will
	     * compact the list, because in this case the line traversal may be faster
	     * than the management of this list. Note that reaching this point is in
	     * general not expected, especially since the range list is amalgamating
	     * adjacent ranges automatically.
	     */

	    int low = TkRangeListLow(ranges);
	    int high = TkRangeListHigh(ranges);

	    TkRangeListClear(ranges);
	    ranges = TkRangeListAdd(ranges, low, high);
	}

	switch (action) {
	case TK_TEXT_INVALIDATE_ONLY: {
	    int counter = MIN(lineCount, totalLines - lineNum);

	    if (isMonospaced) {
		TkBTreeUpdatePixelHeights(textPtr, linePtr, lineCount, epoch);
	    } else {
		ranges = TkRangeListAdd(ranges, lineNum, lineNum + lineCount);
		ResetPixelInfo(TkBTreeLinePixelInfo(textPtr, linePtr));

		if (!TkRangeListContainsRange(ranges, lineNum + 1, lineNum + counter)) {
		    /*
		     * Invalidate the height calculations of each line in the given range.
		     * Note that normally only a few lines will be invalidated (in current
		     * case with simple invalidation). Also note that the other cases
		     * (insert, delete) do not need invalidation of single lines, because
		     * inserted lines should be invalid per default, and deleted lines don't
		     * need invalidation at all.
		     */

		    for ( ; counter > 0; --counter) {
			ResetPixelInfo(TkBTreeLinePixelInfo(textPtr, linePtr = linePtr->nextPtr));
		    }
		}
	    }
	    break;
	}
	case TK_TEXT_INVALIDATE_ELIDE: {
	    int counter = MIN(lineCount, totalLines - lineNum);

	    if (isMonospaced) {
		TkBTreeUpdatePixelHeights(textPtr, linePtr, lineCount, epoch);
	    } else {
		TkTextLine *mergedLinePtr = NULL;
		unsigned count;

		if (!linePtr->logicalLine) {
#if 1		    /* TODO: is this sufficient? */
		    assert(linePtr->prevPtr);
		    linePtr = linePtr->prevPtr;
		    lineNum -= 1;
		    lineCount += 1;
#else		    /* TODO: this is sufficient anyway! */
		    TkTextLine *logicalLinePtr =
			    TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);

		    count = TkBTreeCountLines(textPtr->sharedTextPtr->tree, logicalLinePtr, linePtr);
		    lineNum -= count;
		    lineCount += count;
#endif
		}

		ranges = TkRangeListAdd(ranges, lineNum, lineNum + lineCount);
		count = 1;

		/*
		 * Invalidate the height calculations of each line in the given range.
		 * For merged lines (any line which is not a logical line) we have to
		 * reset the display line count.
		 */

		for ( ; counter > 0; --counter, linePtr = linePtr->nextPtr) {
		    if (linePtr->logicalLine) {
			if (mergedLinePtr) {
			    TkBTreeResetDisplayLineCounts(textPtr, mergedLinePtr, count);
			    mergedLinePtr = NULL;
			}
			ResetPixelInfo(TkBTreeLinePixelInfo(textPtr, linePtr));
		    } else {
			if (!mergedLinePtr) {
			    mergedLinePtr = linePtr;
			    count = 1;
			} else {
			    count += 1;
			}
		    }
		}
		if (mergedLinePtr) {
		    TkBTreeResetDisplayLineCounts(textPtr, mergedLinePtr, count);
		}
	    }
	    break;
	}
	case TK_TEXT_INVALIDATE_DELETE:
	    textPtr->dInfoPtr->lastLineNo -= lineCount;
	    if (isMonospaced) {
		return;
	    }
	    if (lineCount > 0) {
		TkTextIndex index;
		DLine *dlPtr;

		TkRangeListDelete(ranges, lineNum + 1, lineNum + lineCount);

		/*
		 * Free all display lines in specified range. This is required, otherwise
		 * it may happen that we are accessing deleted (invalid) data (bug in
		 * old implementation).
		 */

		TkTextIndexClear(&index, textPtr);
		TkTextIndexSetToStartOfLine2(&index, linePtr->nextPtr);
		if ((dlPtr = FindDLine(textPtr, textPtr->dInfoPtr->dLinePtr, &index))) {
		    TkTextIndexSetToEndOfLine2(&index,
			    TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum + lineCount));
		    FreeDLines(textPtr, dlPtr, FindDLine(textPtr, dlPtr, &index), DLINE_UNLINK);
		}
	    }
	    ranges = TkRangeListAdd(ranges, lineNum, lineNum);
	    ResetPixelInfo(TkBTreeLinePixelInfo(textPtr, linePtr));
	    break;
	case TK_TEXT_INVALIDATE_INSERT:
	    if (lineCount > 0 && lineNum + 1 < totalLines) {
		int lastLine = MIN(lineNum + lineCount, totalLines - 1);
		ranges = TkRangeListInsert(ranges, lineNum + 1, lastLine);
	    }
	    textPtr->dInfoPtr->lastLineNo += lineCount;
	    if (isMonospaced) {
		TkBTreeUpdatePixelHeights(textPtr, linePtr, lineCount, epoch);
	    } else {
		ranges = TkRangeListAdd(ranges, lineNum, lineNum);
		ResetPixelInfo(TkBTreeLinePixelInfo(textPtr,
			TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr)));
	    }
	    break;
	}

	assert(TkRangeListIsEmpty(ranges) || TkRangeListHigh(ranges) < (int) totalLines);
    } else {
	/*
	 * This invalidates the height of all lines in the widget.
	 */

	textPtr->dInfoPtr->lineMetricUpdateEpoch += 1;
	if (action == TK_TEXT_INVALIDATE_DELETE) {
	    TkRangeListClear(ranges);
	    FreeDLines(textPtr, textPtr->dInfoPtr->dLinePtr, NULL, DLINE_UNLINK);
	    totalLines -= lineCount;
	    textPtr->dInfoPtr->lastLineNo -= lineCount;
	} else if (action == TK_TEXT_INVALIDATE_INSERT) {
	    textPtr->dInfoPtr->lastLineNo += lineCount;
	}
	ranges = TkRangeListAdd(ranges, 0, totalLines - 1);
    }

    FreeDLines(textPtr, NULL, NULL, DLINE_CACHE);  /* clear cache */
    FreeDLines(textPtr, NULL, NULL, DLINE_METRIC); /* clear cache */
    FreeDLines(textPtr, textPtr->dInfoPtr->savedDLinePtr, NULL, DLINE_FREE_TEMP);
    textPtr->dInfoPtr->lineMetricUpdateRanges = ranges;
    textPtr->dInfoPtr->currChunkPtr = NULL;

    if (textPtr->syncTime == 0) {
#if 0 /* TODO: is it required to update 'lastLineNo' at this place? */
	textPtr->dInfoPtr->lastLineNo = TkBTreeNumLines(textPtr->sharedTextPtr->tree, NULL);
#endif
    } else {
	StartAsyncLineCalculation(textPtr);
    }
}

void
TkTextInvalidateLineMetrics(
    TkSharedText *sharedTextPtr,/* Shared widget section for all peers, or NULL. */
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextLine *linePtr,	/* Invalidation starts from this line. */
    unsigned lineCount,		/* And includes this many following lines. */
    TkTextInvalidateAction action)
				/* Indicates what type of invalidation occurred (insert,
    				 * delete, or simple). */
{
    if (!sharedTextPtr) {
	TextInvalidateLineMetrics(textPtr, linePtr, lineCount, action);
    } else {
	textPtr = sharedTextPtr->peers;

	while (textPtr) {
	    int numLines = lineCount;
	    TkTextLine *firstLinePtr = linePtr;

	    if (textPtr->startMarker != sharedTextPtr->startMarker) {
		TkTextLine *startLinePtr = TkBTreeGetStartLine(textPtr);
		unsigned lineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, firstLinePtr, NULL);
		unsigned firstLineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, startLinePtr, NULL);

		if (firstLineNo > lineNo) {
		    firstLinePtr = startLinePtr;
		    numLines -= firstLineNo - lineNo;
		}
	    }
	    if (textPtr->endMarker != sharedTextPtr->endMarker) {
		TkTextLine *lastLinePtr = TkBTreeGetLastLine(textPtr);
		unsigned lineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, firstLinePtr, NULL);
		unsigned endLineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, lastLinePtr, NULL);

		if (endLineNo <= lineNo + numLines) {
		    numLines = endLineNo - lineNo - 1;
		}
	    }

	    if (numLines >= 0) {
		TextInvalidateLineMetrics(textPtr, firstLinePtr, numLines, action);
	    }

	    textPtr = textPtr->next;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFindDisplayIndex -
 *
 *	This function is computing the index of display line start; the
 *	computation starts at given index, and is searching some display
 *	lines forward or backward, as specified with 'displayLineOffset'.
 *
 * Results:
 *	Modifies indexPtr to point to the wanted display line start.
 *
 *	If xOffset is non-NULL, it is set to the x-pixel offset of the given
 *	original index within the given display line.
 *
 * Side effects:
 *	See 'LayoutDLine' and 'FreeDLines'.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFindDisplayIndex(
    TkText *textPtr,
    TkTextIndex *indexPtr,
    int displayLineOffset,
    int *xOffset)
{
    DisplayInfo info;
    TkTextLine *linePtr;
    TkTextLine *lastLinePtr;
    unsigned byteOffset;
    bool upToDate;
    int myXOffset;

    assert(textPtr);

    if (!xOffset) {
	xOffset = &myXOffset;
    }

    lastLinePtr = TkBTreeGetLastLine(textPtr);
    linePtr = TkTextIndexGetLine(indexPtr);

    if (displayLineOffset >= 0 && linePtr == lastLinePtr) {
	*xOffset = 0;
	return;
    }
    if (displayLineOffset <= 0 && TkTextIndexIsStartOfText(indexPtr)) {
	*xOffset = 0;
	return;
    }

    if (linePtr == lastLinePtr) {
	displayLineOffset += 1;
	*xOffset = 0;
	xOffset = NULL;
	TkTextIndexSetToLastChar2(indexPtr, linePtr->prevPtr);
    }

    if (displayLineOffset > 0) {
	upToDate = TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges);
    } else {
	upToDate = TestIfLinesUpToDate(indexPtr);
    }
    linePtr = ComputeDisplayLineInfo(textPtr, indexPtr, &info);

    if (xOffset) {
	if (IsStartOfNotMergedLine(indexPtr)) {
	    *xOffset = 0;
	} else {
	    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
	    DLine *dlPtr = info.lastDLinePtr;
	    TkTextIndex index = *indexPtr;

	    TkTextIndexBackBytes(textPtr, &index, info.byteOffset, &index);

	    if (!dlPtr) {
		dlPtr = FindCachedDLine(textPtr, indexPtr);

		if (!dlPtr
			&& !(dInfoPtr->flags & DINFO_OUT_OF_DATE)
			&& TkTextIndexCompare(indexPtr, &textPtr->topIndex) >= 0) {
		    dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, indexPtr);
		}
		if (!dlPtr) {
		    dlPtr = LayoutDLine(&index, info.displayLineNo);
		    FreeDLines(textPtr, dlPtr, NULL, DLINE_CACHE);
		}
	    }

	    *xOffset = DLineXOfIndex(textPtr, dlPtr, TkTextIndexCountBytes(&dlPtr->index, indexPtr));
	}
    }

    if (upToDate) {
	const TkTextDispLineInfo *dispLineInfo;

	assert(!info.dLinePtr);

	/*
	 * The display line information is complete for the required range, so
	 * use it for finding the requested display line.
	 */

	if (displayLineOffset == 0) {
	    byteOffset = info.entry->byteOffset;
	} else {
	    if (displayLineOffset > 0) {
		linePtr = TkBTreeNextDisplayLine(textPtr, linePtr, &info.displayLineNo,
			displayLineOffset);
	    } else {
		linePtr = TkBTreePrevDisplayLine(textPtr, linePtr, &info.displayLineNo,
			-displayLineOffset);
	    }
	    dispLineInfo = TkBTreeLinePixelInfo(textPtr, linePtr)->dispLineInfo;
	    byteOffset = dispLineInfo ? dispLineInfo->entry[info.displayLineNo].byteOffset : 0;
	}
    } else {
	unsigned removedLines;

	/*
	 * We want to cache last produced display line, because it's likely that this
	 * line will be used afterwards.
	 */

	removedLines = 0;
	if (info.lastDLinePtr) {
	    DLine *prevPtr = info.lastDLinePtr->prevPtr;
	    FreeDLines(textPtr, info.lastDLinePtr, NULL, DLINE_CACHE);
	    if (info.dLinePtr == info.lastDLinePtr) { info.dLinePtr = NULL; }
	    info.lastDLinePtr = prevPtr;
	    info.numCachedLines -= 1;
	    removedLines = 1;
	}

	TkTextIndexBackBytes(textPtr, indexPtr, info.byteOffset, indexPtr);

	if (displayLineOffset > 0) {
	    ComputeMissingMetric(textPtr, &info, THRESHOLD_LINE_OFFSET, displayLineOffset);
	    info.numDispLines -= info.displayLineNo;

	    while (true) {
		const TkTextDispLineEntry *last;

		if ((int) info.numDispLines >= displayLineOffset) {
		    last = info.entry + displayLineOffset;
		    byteOffset = last->byteOffset;
		    break;
		}
		last = info.entry + info.numDispLines;
		byteOffset = last->byteOffset;
		displayLineOffset -= info.numDispLines;
		TkTextIndexForwBytes(textPtr, indexPtr, byteOffset, indexPtr);
		linePtr = TkTextIndexGetLine(indexPtr);
		if (linePtr == lastLinePtr) {
		    break;
		}
		FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
		ComputeDisplayLineInfo(textPtr, indexPtr, &info);
		ComputeMissingMetric(textPtr, &info, THRESHOLD_LINE_OFFSET, displayLineOffset);
		assert(info.displayLineNo == 0);
	    }
	} else if (displayLineOffset < 0) {
	    info.numDispLines = info.displayLineNo + 1;

	    while (true) {
		TkTextLine *prevLine;

		if (-displayLineOffset < (int) info.numDispLines) {
		    int skipBack;

		    byteOffset = (info.entry + displayLineOffset)->byteOffset;
		    skipBack = displayLineOffset;

		    /*
		     * We want to cache this display line, because it's likely that this
		     * line will be used afterwards. Take into account that probably the
		     * last cached line has been removed.
		     */

		    if ((skipBack -= removedLines) >= 0 && (int) info.numCachedLines > skipBack) {
			DLine *dlPtr = info.lastDLinePtr;
			while (dlPtr && skipBack--) {
			    dlPtr = dlPtr->prevPtr;
			}
			if (dlPtr == info.dLinePtr) {
			    info.dLinePtr = dlPtr->nextPtr;
			}
			if (dlPtr == info.lastDLinePtr) {
			    info.lastDLinePtr = dlPtr->prevPtr;
			}
			FreeDLines(textPtr, dlPtr, NULL, DLINE_CACHE);
		    }
		    break;
		}
		displayLineOffset += info.numDispLines;
		if (!(prevLine = TkBTreePrevLine(textPtr, linePtr))) {
		    byteOffset = info.entry[0].byteOffset;
		    break;
		}
		TkTextIndexSetToLastChar2(indexPtr, linePtr = prevLine);
		FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
		linePtr = ComputeDisplayLineInfo(textPtr, indexPtr, &info);
		removedLines = 0;
	    }
	} else {
	    byteOffset = info.entry[0].byteOffset;
	}

	/*
	 * We want to cache last produced display line, because it's likely that this
	 * line will be used afterwards.
	 */

	if (info.lastDLinePtr) {
	    FreeDLines(textPtr, info.lastDLinePtr, NULL, DLINE_CACHE);
	    if (info.dLinePtr == info.lastDLinePtr) { info.dLinePtr = NULL; }
	}

	FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
    }

    /* set to first byte, not to start of line */
    DEBUG(indexPtr->discardConsistencyCheck = true);
    TkTextIndexSetByteIndex2(indexPtr, linePtr, 0);
    DEBUG(indexPtr->discardConsistencyCheck = false);
    TkTextIndexForwBytes(textPtr, indexPtr, byteOffset, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCountDisplayLines -
 *
 *	This function is counting the number of visible display lines
 *	between given indices. This function will be used for computing
 *	"count -displaylines".
 *
 * Results:
 *	The number of visible display lines inside given range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkTextCountDisplayLines(
    TkText *textPtr,			/* Widget record for text widget. */
    const TkTextIndex *indexFrom,	/* Start counting at this index. */
    const TkTextIndex *indexTo)		/* Stop counting before this index. */
{
    const TkTextPixelInfo *pixelInfo1;
    const TkTextPixelInfo *pixelInfo2;
    TkTextDispLineInfo *dispLineInfo;
    TkTextDispLineEntry *entry;
    TkTextDispLineEntry *lastEntry;
    TkTextLine *linePtr1;
    TkTextLine *linePtr2;
    TkTextIndex index;
    unsigned byteOffset;
    int numLines;

    assert(TkTextIndexCompare(indexFrom, indexTo) <= 0);
    assert(textPtr->sharedTextPtr->allowUpdateLineMetrics);

    TkTextUpdateLineMetrics(textPtr, TkTextIndexGetLineNumber(indexFrom, textPtr),
	    TkTextIndexGetLineNumber(indexTo, textPtr));

    linePtr1 = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, TkTextIndexGetLine(indexFrom));
    linePtr2 = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, TkTextIndexGetLine(indexTo));
    pixelInfo1 = linePtr1->pixelInfo;
    pixelInfo2 = linePtr2->pixelInfo;

    if (!pixelInfo1->dispLineInfo) {
	numLines = 0;
    } else {
	index = *indexFrom;
	TkTextIndexSetToStartOfLine2(&index, linePtr1);
	byteOffset = TkTextIndexCountBytes(&index, indexFrom);
	dispLineInfo = pixelInfo1->dispLineInfo;
	lastEntry = dispLineInfo->entry + dispLineInfo->numDispLines;
	entry = SearchDispLineEntry(dispLineInfo->entry, lastEntry, byteOffset);
	numLines = -(entry - dispLineInfo->entry);
    }

    while (true) {
	if (pixelInfo1->dispLineInfo) {
	    if (pixelInfo1 == pixelInfo2) {
		index = *indexTo;
		TkTextIndexSetToStartOfLine2(&index, linePtr2);
		byteOffset = TkTextIndexCountBytes(&index, indexTo);
		dispLineInfo = pixelInfo2->dispLineInfo;
		lastEntry = dispLineInfo->entry + dispLineInfo->numDispLines;
		entry = SearchDispLineEntry(dispLineInfo->entry, lastEntry, byteOffset);
		return numLines + (entry - dispLineInfo->entry);
	    }
	    numLines += pixelInfo1->dispLineInfo->numDispLines;
	} else if (pixelInfo1 == pixelInfo2) {
	    return numLines;
	} else {
	    numLines += 1;
	}
	linePtr1 = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr1);
	pixelInfo1 = linePtr1->pixelInfo;
    }

    return 0; /* never reached */
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFindDisplayLineStartEnd --
 *
 *	This function is invoked to find the index of the beginning or end of
 *	the particular display line on which the given index sits, whether
 *	that line is displayed or not.
 *
 *	If 'end' is 'false', we look for the start, and if 'end' is 'true'
 *	we look for the end.
 *
 *	If the beginning of the current display line is elided, and we are
 *	looking for the start of the line, then the returned index will be the
 *	first elided index on the display line.
 *
 *	Similarly if the end of the current display line is elided and we are
 *	looking for the end, then the returned index will be the last elided
 *	index on the display line.
 *
 * Results:
 *	Modifies indexPtr to point to the given end.
 *
 * Side effects:
 *	See 'LayoutDLine' and 'FreeDLines'.
 *
 *----------------------------------------------------------------------
 */

static void
FindDisplayLineStartEnd(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextIndex *indexPtr,	/* Index we will adjust to the display line start or end. */
    bool end,			/* 'false' = start, 'true' = end. */
    int cacheType)		/* Argument for FreeDLines, either DLINE_CACHE or DLINE_METRIC. */
{
    DisplayInfo info;
    int byteCount;

    if (TkTextIndexGetLine(indexPtr) == TkBTreeGetLastLine(textPtr)
	    || (!end && IsStartOfNotMergedLine(indexPtr))) {
	/*
	 * Nothing to do, because we are at start/end of a display line.
	 */

	return;
    }

    ComputeDisplayLineInfo(textPtr, indexPtr, &info);
    byteCount = end ? -(info.nextByteOffset - 1) : info.byteOffset;
    TkTextIndexBackBytes(textPtr, indexPtr, byteCount, indexPtr);

    if (end) {
	int offset;
	int skipBack = 0;
	TkTextSegment *segPtr = TkTextIndexGetContentSegment(indexPtr, &offset);
	char const *p = segPtr->body.chars + offset;

	/*
	 * We don't want an offset inside a multi-byte sequence, so find the start
	 * of the current character.
	 */

	while (p > segPtr->body.chars && (*p & 0xc0) == 0x80) {
	    p -= 1;
	    skipBack += 1;
	}
	TkTextIndexBackBytes(textPtr, indexPtr, skipBack, indexPtr);
    }

    /*
     * We want to cache last produced display line, because it's likely that this
     * line will be used afterwards.
     */

    if (info.lastDLinePtr) {
	FreeDLines(textPtr, info.lastDLinePtr, NULL, cacheType);
	if (info.dLinePtr == info.lastDLinePtr) {
	    info.dLinePtr = NULL; /* don't release it twice */
	}
    }

    FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
}

void
TkTextFindDisplayLineStartEnd(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextIndex *indexPtr,	/* Index we will adjust to the display line start or end. */
    bool end)			/* 'false' = start, 'true' = end. */
{
    FindDisplayLineStartEnd(textPtr, indexPtr, end, DLINE_CACHE);
}

/*
 *----------------------------------------------------------------------
 *
 * CalculateDisplayLineHeight --
 *
 *	This function is invoked to recalculate the height of the particular
 *	display line which starts with the given index, whether that line is
 *	displayed or not.
 *
 *	This function does not, in itself, update any cached information about
 *	line heights. That should be done, where necessary, by its callers.
 *
 *	The behaviour of this function is _undefined_ if indexPtr is not
 *	currently at the beginning of a display line.
 *
 * Results:
 *	The number of vertical pixels used by the display line.
 *
 *	If 'byteCountRef' is non-NULL, then returns in that pointer the number
 *	of byte indices on the given display line (which can be used to update
 *	indexPtr in a loop).
 *
 * Side effects:
 *	The same as LayoutDLine and FreeDLines.
 *
 *----------------------------------------------------------------------
 */

#ifndef NDEBUG
static bool
IsAtStartOfDisplayLine(
    const TkTextIndex *indexPtr)
{
    TkTextIndex index2 = *indexPtr;

    assert(indexPtr->textPtr);

    FindDisplayLineStartEnd(indexPtr->textPtr, &index2, DISP_LINE_START, DLINE_METRIC);
    return TkTextIndexCompare(&index2, indexPtr) == 0;
}
#endif /* NDEBUG */

static int
CalculateDisplayLineHeight(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *indexPtr,/* The index at the beginning of the display line of interest. */
    unsigned *byteCountRef)	/* NULL or used to return the number of byte indices on the given
    				 * display line. */
{
    DisplayInfo info;

    assert(!TkTextIndexIsEndOfText(indexPtr));
    assert(IsAtStartOfDisplayLine(indexPtr));

    /*
     * Special case for artificial last line.
     */

    if (TkTextIndexGetLine(indexPtr) == TkBTreeGetLastLine(textPtr)) {
	if (byteCountRef) { *byteCountRef = 0; }
	return 0;
    }

    ComputeDisplayLineInfo(textPtr, indexPtr, &info);

    /*
     * Last computed line has to be cached temporarily.
     */

    if (info.lastDLinePtr) {
	FreeDLines(textPtr, info.lastDLinePtr, NULL, DLINE_METRIC);
	if (info.dLinePtr == info.lastDLinePtr) {
	    info.dLinePtr = NULL; /* don't release it twice */
	}
    }

    FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
    if (byteCountRef) { *byteCountRef = info.nextByteOffset + info.byteOffset; }
    assert(info.entry->height != 0xffffffff);
    return info.entry->height;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetViewOffset --
 *
 *	This function returns the x and y offset of the current view.
 *
 * Results:
 *	The pixel offset of the current view.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkTextGetViewOffset(
    TkText *textPtr,		/* Widget record for text widget. */
    int *x,			/* X offset */
    int *y)			/* Y offset */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    if (dInfoPtr && dInfoPtr->dLinePtr) {
	*x = dInfoPtr->curXPixelOffset;
	*y = dInfoPtr->curYPixelOffset;
    } else {
	*x = 0;
	*y = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetXPixelFromChunk --
 *
 *	Return the left most x pixel index from given chunk.
 *
 * Results:
 *	Returns the left most x pixel index from given chunk.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetXPixelFromChunk(
    const TkText *textPtr,
    const TkTextDispChunk *chunkPtr)
{
    const TextDInfo *dInfoPtr;

    assert(textPtr);
    assert(chunkPtr);

    dInfoPtr = textPtr->dInfoPtr;
    return chunkPtr->x + dInfoPtr->x + dInfoPtr->curXPixelOffset;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetYPixelFromChunk --
 *
 *	Return the top most y pixel index from given chunk.
 *
 * Results:
 *	Returns the top most y pixel index from given chunk.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetYPixelFromChunk(
    const TkText *textPtr,
    const TkTextDispChunk *chunkPtr)
{
    const DLine *dlPtr;

    assert(textPtr);
    assert(chunkPtr);

    dlPtr = chunkPtr->dlPtr;
    /* Note that dInfoPtr->y is already included in dlPtr->y. */
    return dlPtr->y + textPtr->dInfoPtr->curYPixelOffset;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetTagSetFromChunk --
 *
 *	This function returns the tag information from given chunk.
 *	It must be ensured that this chunk contains any content
 *	(character, hyphen, image, window).
 *
 * Results:
 *	The tag information of this chunk.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextTagSet *
TkTextGetTagSetFromChunk(
    const TkTextDispChunk *chunkPtr)
{
    assert(chunkPtr);

    switch (chunkPtr->layoutProcs->type) {
    case TEXT_DISP_CHAR:   /* fallthru */
    case TEXT_DISP_HYPHEN: return CHAR_CHUNK_GET_SEGMENT(chunkPtr)->tagInfoPtr;
    case TEXT_DISP_IMAGE:  /* fallthru */
    case TEXT_DISP_WINDOW: return ((TkTextSegment *) chunkPtr->clientData)->tagInfoPtr;
    case TEXT_DISP_ELIDED: /* fallthru */
    case TEXT_DISP_CURSOR: return NULL;
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetPixelsTo --
 *
 *	This function computes the pixels between the first display line
 *	of the logical line (belonging to given position), and the display
 *	line at the specified position.
 *
 *	If the line metric computation of the specified logical line is
 *	not yet finished, and 'info' is not NULL, then ComputeMissingMetric
 *	will be used to compute the missing metric compuation.
 *
 * Results:
 *	The pixels from first display line (belonging to given position) to
 *	specified display line.
 *
 * Side effects:
 *	Just the ones of ComputeMissingMetric.
 *
 *----------------------------------------------------------------------
 */

static unsigned
GetPixelsTo(
    TkText *textPtr,
    const TkTextIndex *indexPtr,
    bool inclusiveLastLine,
    DisplayInfo *info)		/* can be NULL */
{
    TkTextLine *logicalLinePtr;
    const TkTextPixelInfo *pixelInfo;
    TkTextDispLineInfo *dispLineInfo;
    const TkTextDispLineEntry *lastEntry;
    const TkTextDispLineEntry *entry;
    TkTextIndex index;
    unsigned byteOffset;

    logicalLinePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr,
	    TkTextIndexGetLine(indexPtr));
    if (logicalLinePtr == TkBTreeGetLastLine(textPtr)) {
	return 0;
    }
    pixelInfo = TkBTreeLinePixelInfo(textPtr, logicalLinePtr);

    if (!info && (pixelInfo->epoch & EPOCH_MASK) != textPtr->dInfoPtr->lineMetricUpdateEpoch) {
	return 0;
    }

    if (!(dispLineInfo = pixelInfo->dispLineInfo)) {
	return inclusiveLastLine ? pixelInfo->height : 0;
    }

    index = *indexPtr;
    TkTextIndexSetToStartOfLine2(&index, logicalLinePtr);
    byteOffset = TkTextIndexCountBytes(&index, indexPtr);
    lastEntry = dispLineInfo->entry + dispLineInfo->numDispLines;
    entry = SearchDispLineEntry(dispLineInfo->entry, lastEntry, byteOffset);

    if (entry == lastEntry) {
	/*
	 * This happens if the line metric calculation for this logical line is not yet complete.
	 */

	if (info) {
	    unsigned numDispLinesSoFar = dispLineInfo->numDispLines;

	    ComputeMissingMetric(textPtr, info, THRESHOLD_BYTE_OFFSET, byteOffset);
	    lastEntry = dispLineInfo->entry + dispLineInfo->numDispLines;
	    entry = SearchDispLineEntry(dispLineInfo->entry + numDispLinesSoFar, lastEntry, byteOffset);
	    if (entry == lastEntry) {
		entry -= 1;
	    }
	} else {
	    assert(dispLineInfo->numDispLines > 0);
	    entry -= 1;
	}
    } else if (!inclusiveLastLine && entry-- == dispLineInfo->entry) {
	return 0;
    }

    return entry->pixels;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexYPixels --
 *
 *	This function is invoked to calculate the number of vertical pixels
 *	between the first index of the text widget and the given index. The
 *	range from first logical line to given logical line is determined
 *	using the cached values, and the range inside the given logical line
 *	is calculated on the fly.
 *
 * Results:
 *	The pixel distance between first pixel in the widget and the
 *	top of the index's current display line (could be zero).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextIndexYPixels(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *indexPtr)/* The index of which we want the pixel distance from top of
    				 * text widget to top of index. */
{
    /* Note that TkBTreePixelsTo is computing up to start of the logical line. */
    return TkBTreePixelsTo(textPtr, TkTextIndexGetLine(indexPtr)) +
	    GetPixelsTo(textPtr, indexPtr, false, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateOneLine --
 *
 *	This function is invoked to recalculate the height of a particular
 *	logical line, whether that line is displayed or not.
 *
 *	It must NEVER be called for the artificial last TkTextLine which is
 *	used internally for administrative purposes only. That line must
 *	retain its initial height of 0 otherwise the pixel height calculation
 *	maintained by the B-tree will be wrong.
 *
 * Results:
 *	The number of display lines in the logical line. This could be zero if
 *	the line is totally elided.
 *
 * Side effects:
 *	Line heights may be recalculated, and a timer to update the scrollbar
 *	may be installed. Also see the called function CalculateDisplayLineHeight
 *	for its side effects.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateOneLine(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextLine *linePtr,	/* The line of which to calculate the height. */
    TkTextIndex *indexPtr,	/* Either NULL or an index at the start of a display line belonging
    				 * to linePtr, at which we wish to start (e.g. up to which we have
				 * already calculated). On return this will be set to the first index
				 * on the next line. */
    unsigned maxDispLines)	/* Don't compute more than this number of display lines. */
{
    TkTextIndex index;
    TkTextLine *logicalLinePtr;
    TkTextPixelInfo *pixelInfo;
    unsigned displayLines;
    unsigned updateCounter;
    unsigned pixelHeight;

    assert(linePtr != TkBTreeGetLastLine(textPtr));

    if (!indexPtr) {
	TkTextIndexClear(&index, textPtr);
	TkTextIndexSetToStartOfLine2(&index, linePtr);
	indexPtr = &index;
    }

    linePtr = TkTextIndexGetLine(indexPtr);
    logicalLinePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
    pixelInfo = TkBTreeLinePixelInfo(textPtr, logicalLinePtr);

    if (pixelInfo->epoch == (textPtr->dInfoPtr->lineMetricUpdateEpoch | PARTIAL_COMPUTED_BIT)) {
	const TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;
	unsigned bytes;

	/*
	 * We are inside a partial computation. Continue with next display line.
	 */

	assert(dispLineInfo);
	assert(dispLineInfo->numDispLines > 0);
	bytes = dispLineInfo->entry[dispLineInfo->numDispLines].byteOffset;
	bytes -= dispLineInfo->entry[0].byteOffset;
	TkTextIndexSetToStartOfLine2(indexPtr, logicalLinePtr);
	TkTextIndexForwBytes(textPtr, indexPtr, bytes, indexPtr);
	linePtr = TkTextIndexGetLine(indexPtr);
	assert(!linePtr->logicalLine || !TkTextIndexIsStartOfLine(indexPtr));
    } else if (!linePtr->logicalLine || !TkTextIndexIsStartOfLine(indexPtr)) {
	/*
	 * CalculateDisplayLineHeight must be called with an index at the beginning
	 * of a display line. Force this to happen. This is needed when
	 * UpdateOneLine is called with a line that is merged with its
	 * previous line: the number of merged logical lines in a display line is
	 * calculated correctly only when CalculateDisplayLineHeight receives
	 * an index at the beginning of a display line. In turn this causes the
	 * merged lines to receive their correct zero pixel height in
	 * TkBTreeAdjustPixelHeight.
	 */

	FindDisplayLineStartEnd(textPtr, indexPtr, DISP_LINE_START, DLINE_METRIC);
	linePtr = TkTextIndexGetLine(indexPtr);
    }

    assert(linePtr->nextPtr);
    updateCounter = textPtr->dInfoPtr->lineMetricUpdateCounter;
    pixelHeight = 0;
    displayLines = 0;

    /*
     * Iterate through all display-lines corresponding to the single logical
     * line 'linePtr' (and lines merged into this line due to eol elision),
     * adding up the pixel height of each such display line as we go along.
     * The final total is, therefore, the total height of all display lines
     * made up by the logical line 'linePtr' and subsequent logical lines
     * merged into this line.
     */

    while (true) {
	unsigned bytes, height;
	bool atEnd;

	/*
	 * Currently this call doesn't have many side-effects. However, if in
	 * the future we change the code so there are side-effects (such as
	 * adjusting linePtr->pixelHeight), then the code might not quite work
	 * as intended.
	 */

        height = CalculateDisplayLineHeight(textPtr, indexPtr, &bytes);
	atEnd = TkTextIndexForwBytes(textPtr, indexPtr, bytes, indexPtr) == 1
		|| TkTextIndexIsEndOfText(indexPtr);

	assert(bytes > 0);

	if (height > 0) {
	    pixelHeight += height;
	    displayLines += 1;
	}

	if (atEnd) {
	    break; /* we are at the end */
	}

	if (linePtr != TkTextIndexGetLine(indexPtr)) {
	    if (TkTextIndexGetLine(indexPtr)->logicalLine) {
		break; /* we've reached the end of the logical line */
	    }
	    linePtr = TkTextIndexGetLine(indexPtr);
	} else {
	    /*
	     * We must still be on the same wrapped line, on a new logical
	     * line merged with the logical line 'linePtr'.
	     */
	}

	if (displayLines == maxDispLines) {
	    /*
	     * We are calculating a limited number of display lines at a time, to avoid huge delays.
	     */

	    /* check that LayoutUpdateLineHeightInformation has set this bit */
	    assert(pixelInfo->epoch & PARTIAL_COMPUTED_BIT);
	    break;
	}
    }

    if (updateCounter != textPtr->dInfoPtr->lineMetricUpdateCounter) {
	/*
	 * Otherwise nothing relevant has changed.
	 */

	if (tkTextDebug) {
	    char buffer[2*TCL_INTEGER_SPACE + 1];

	    if (!TkBTreeNextLine(textPtr, linePtr)) {
		Tcl_Panic("Must never ever update line height of last artificial line");
	    }

	    pixelHeight = TkBTreeNumPixels(textPtr);
	    snprintf(buffer, sizeof(buffer), "%u %u",
		    TkBTreeLinesTo(indexPtr->tree, textPtr, linePtr, NULL), pixelHeight);
	    LOG("tk_textNumPixels", buffer);
	}

	if (!textPtr->dInfoPtr->scrollbarTimer) {
	    InvokeAsyncUpdateYScrollbar(textPtr);
	}
    }

    return displayLines;
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayText --
 *
 *	This function is invoked as a when-idle handler to update the display.
 *	It only redisplays the parts of the text widget that are out of date.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is redrawn on the screen.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayText(
    ClientData clientData)	/* Information about widget. */
{
    TkText *textPtr = clientData;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr;
    Pixmap pixmap;
    int maxHeight, borders;
    int bottomY = 0;		/* Initialization needed only to stop compiler warnings. */
    int extent1, extent2;
    Tcl_Interp *interp;

#ifdef MAC_OSX_TK
    /*
     * If drawing is disabled, all we need to do is clear the REDRAW_PENDING flag.
     */
    TkWindow *winPtr = (TkWindow *)(textPtr->tkwin);
    MacDrawable *macWin = winPtr->privatePtr;
    if (macWin && (macWin->flags & TK_DO_NOT_DRAW)) {
	dInfoPtr->flags &= ~REDRAW_PENDING;
	if (dInfoPtr->flags & ASYNC_PENDING) {
	    assert(dInfoPtr->flags & ASYNC_UPDATE);
	    dInfoPtr->flags &= ~ASYNC_PENDING;
	    /* continue with asynchronous pixel-height calculation */
	    InvokeAsyncUpdateLineMetrics(textPtr);
	}
	return;
    }
#endif /* MAC_OSX_TK */

    if (textPtr->flags & DESTROYED) {
	return; /* the widget has been deleted */
    }

    interp = textPtr->interp;
    Tcl_Preserve(interp);

    TK_TEXT_DEBUG(Tcl_SetVar2(interp, "tk_textRelayout", NULL, "", TCL_GLOBAL_ONLY));

    if (!Tk_IsMapped(textPtr->tkwin) || dInfoPtr->maxX <= dInfoPtr->x || dInfoPtr->maxY <= dInfoPtr->y) {
	UpdateDisplayInfo(textPtr);
	dInfoPtr->flags &= ~REDRAW_PENDING;
	goto doScrollbars;
    }
    DEBUG(stats.numRedisplays += 1);
    TK_TEXT_DEBUG(Tcl_SetVar2(interp, "tk_textRedraw", NULL, "", TCL_GLOBAL_ONLY));

    /*
     * Choose a new current item if that is needed (this could cause event
     * handlers to be invoked, hence the preserve/release calls and the loop,
     * since the handlers could conceivably necessitate yet another current
     * item calculation). The textPtr check is because the whole window could go
     * away in the meanwhile.
     */

    if (dInfoPtr->flags & REPICK_NEEDED) {
	textPtr->refCount += 1;
	dInfoPtr->flags &= ~REPICK_NEEDED;
	dInfoPtr->currChunkPtr = NULL;
	TkTextPickCurrent(textPtr, &textPtr->pickEvent);
	if (TkTextDecrRefCountAndTestIfDestroyed(textPtr)) {
	    goto end;
	}
    }

    /*
     * First recompute what's supposed to be displayed.
     */

    UpdateDisplayInfo(textPtr);
    dInfoPtr->dLinesInvalidated = false;

    /*
     * TkScrollWindow must consider the insertion cursor.
     */

    extent1 = MIN(textPtr->padX, textPtr->insertWidth/2);
    extent2 = MIN(textPtr->padX, (textPtr->insertWidth + 1)/2);

    /*
     * See if it's possible to bring some parts of the screen up-to-date by
     * scrolling (copying from other parts of the screen). We have to be
     * particularly careful with the top and bottom lines of the display,
     * since these may only be partially visible and therefore not helpful for
     * some scrolling purposes.
     */

    for (dlPtr = dInfoPtr->dLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	DLine *dlPtr2;
	int offset, height, y, oldY;
	TkRegion damageRgn;

	/*
	 * These tests are, in order:
	 *
	 * 1. If the line is already marked as invalid
	 * 2. If the line hasn't moved
	 * 3. If the line overlaps the bottom of the window and we are scrolling up.
	 * 4. If the line overlaps the top of the window and we are scrolling down.
	 *
	 * If any of these tests are true, then we can't scroll this line's
	 * part of the display.
	 *
	 * Note that even if tests 3 or 4 aren't true, we may be able to
	 * scroll the line, but we still need to be sure to call embedded
	 * window display procs on top and bottom lines if they have any
	 * portion non-visible (see below).
	 */

	if ((dlPtr->flags & OLD_Y_INVALID)
		|| dlPtr->y == dlPtr->oldY
		|| ((dlPtr->oldY + dlPtr->height) > dInfoPtr->maxY && dlPtr->y < dlPtr->oldY)
		|| (dlPtr->oldY < dInfoPtr->y && dlPtr->y > dlPtr->oldY)) {
	    continue;
	}

	/*
	 * This line is already drawn somewhere in the window so it only needs
	 * to be copied to its new location. See if there's a group of lines
	 * that can all be copied together.
	 */

	offset = dlPtr->y - dlPtr->oldY;
	height = dlPtr->height;
	y = dlPtr->y;
	for (dlPtr2 = dlPtr->nextPtr; dlPtr2; dlPtr2 = dlPtr2->nextPtr) {
	    if ((dlPtr2->flags & OLD_Y_INVALID)
		    || dlPtr2->oldY + offset != dlPtr2->y
		    || dlPtr2->oldY + dlPtr2->height > dInfoPtr->maxY) {
		break;
	    }
	    height += dlPtr2->height;
	}

	/*
	 * Reduce the height of the area being copied if necessary to avoid
	 * overwriting the border area.
	 */

	if (y + height > dInfoPtr->maxY) {
	    height = dInfoPtr->maxY - y;
	}
	oldY = dlPtr->oldY;
	if (y < dInfoPtr->y) {
	    /*
	     * Adjust if the area being copied is going to overwrite the top
	     * border of the window (so the top line is only half onscreen).
	     */

	    int y_off = dInfoPtr->y - dlPtr->y;
	    height -= y_off;
	    oldY += y_off;
	    y = dInfoPtr->y;
	}

#if 0 /* TODO: this can happen in certain situations, but shouldn't happen */
	assert(height > 0); /* otherwise dInfoPtr->topPixelOffset is wrong */
#else
	if (height <= 0) {
	    fprintf(stderr, "DisplayText: height <= 0 is unexpected\n");
	}
#endif

	/*
	 * Update the lines we are going to scroll to show that they have been copied.
	 */

	while (true) {
	    /*
	     * The DLine already has OLD_Y_INVALID cleared.
	     */

	    dlPtr->oldY = dlPtr->y;
	    if (dlPtr->nextPtr == dlPtr2) {
		break;
	    }
	    dlPtr = dlPtr->nextPtr;
	}

	/*
	 * Scan through the lines following the copied ones to see if we are
	 * going to overwrite them with the copy operation. If so, mark them
	 * for redisplay.
	 */

	for ( ; dlPtr2; dlPtr2 = dlPtr2->nextPtr) {
	    if (!(dlPtr2->flags & OLD_Y_INVALID)
		    && dlPtr2->oldY + dlPtr2->height > y
		    && dlPtr2->oldY < y + height) {
		dlPtr2->flags |= OLD_Y_INVALID;
	    }
	}

	/*
	 * Now scroll the lines. This may generate damage which we handle by
	 * calling TextInvalidateRegion to mark the display blocks as stale.
	 */

	damageRgn = TkCreateRegion();
	if (TkScrollWindow(textPtr->tkwin, dInfoPtr->scrollGC, dInfoPtr->x - extent1, oldY,
		dInfoPtr->maxX - dInfoPtr->x + extent1 + extent2, height, 0, y - oldY, damageRgn)) {
#ifdef MAC_OSX_TK
	    /* the processing of the Expose event is damaging the region on Mac */
#else
	    TextInvalidateRegion(textPtr, damageRgn);
#endif
	}
	DEBUG(stats.numCopies += 1);
	TkDestroyRegion(damageRgn);
    }

    /*
     * Clear the REDRAW_PENDING flag here. This is actually pretty tricky. We want to
     * wait until *after* doing the scrolling, since that could generate more areas to
     * redraw and don't want to reschedule a redisplay for them. On the other hand, we
     * can't wait until after all the redisplaying, because the act of redisplaying
     * could actually generate more redisplays (e.g. in the case of a nested window
     * with event bindings triggered by redisplay).
     */

    dInfoPtr->flags &= ~REDRAW_PENDING;

    /*
     * Redraw the borders if that's needed.
     */

    if (dInfoPtr->flags & REDRAW_BORDERS) {
	TK_TEXT_DEBUG(LOG("tk_textRedraw", "borders"));

	if (!textPtr->tkwin) {
	    /*
	     * The widget has been deleted. Don't do anything.
	     */

	    goto end;
	}

	Tk_Draw3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		textPtr->border, textPtr->highlightWidth,
		textPtr->highlightWidth,
		Tk_Width(textPtr->tkwin) - 2*textPtr->highlightWidth,
		Tk_Height(textPtr->tkwin) - 2*textPtr->highlightWidth,
		textPtr->borderWidth, textPtr->relief);
	if (textPtr->highlightWidth != 0) {
	    GC fgGC, bgGC;

	    bgGC = Tk_GCForColor(textPtr->highlightBgColorPtr, Tk_WindowId(textPtr->tkwin));
	    if (textPtr->flags & HAVE_FOCUS) {
		fgGC = Tk_GCForColor(textPtr->highlightColorPtr, Tk_WindowId(textPtr->tkwin));
		TkpDrawHighlightBorder(textPtr->tkwin, fgGC, bgGC,
			textPtr->highlightWidth, Tk_WindowId(textPtr->tkwin));
	    } else {
		TkpDrawHighlightBorder(textPtr->tkwin, bgGC, bgGC,
			textPtr->highlightWidth, Tk_WindowId(textPtr->tkwin));
	    }
	}
	borders = textPtr->borderWidth + textPtr->highlightWidth;
	if (textPtr->padY > 0) {
	    Tk_Fill3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		    textPtr->border, borders, borders,
		    Tk_Width(textPtr->tkwin) - 2*borders, textPtr->padY,
		    0, TK_RELIEF_FLAT);
	    Tk_Fill3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		    textPtr->border, borders,
		    Tk_Height(textPtr->tkwin) - borders - textPtr->padY,
		    Tk_Width(textPtr->tkwin) - 2*borders,
		    textPtr->padY, 0, TK_RELIEF_FLAT);
	}
	if (textPtr->padX > 0) {
	    Tk_Fill3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		    textPtr->border, borders, borders + textPtr->padY,
		    textPtr->padX,
		    Tk_Height(textPtr->tkwin) - 2*borders -2*textPtr->padY,
		    0, TK_RELIEF_FLAT);
	    Tk_Fill3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		    textPtr->border,
		    Tk_Width(textPtr->tkwin) - borders - textPtr->padX,
		    borders + textPtr->padY, textPtr->padX,
		    Tk_Height(textPtr->tkwin) - 2*borders -2*textPtr->padY,
		    0, TK_RELIEF_FLAT);
	}
	dInfoPtr->flags &= ~REDRAW_BORDERS;
    }

    /*
     * Now we have to redraw the lines that couldn't be updated by scrolling.
     * First, compute the height of the largest line and allocate an off-
     * screen pixmap to use for double-buffered displays.
     */

    maxHeight = -1;
    for (dlPtr = dInfoPtr->dLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	if (dlPtr->height > maxHeight && ((dlPtr->flags & OLD_Y_INVALID) || dlPtr->oldY != dlPtr->y)) {
	    maxHeight = dlPtr->height;
	}
	bottomY = dlPtr->y + dlPtr->height;
    }

    /*
     * There used to be a line here which restricted 'maxHeight' to be no
     * larger than 'dInfoPtr->maxY', but this is incorrect for the case where
     * individual lines may be taller than the widget _and_ we have smooth
     * scrolling. What we can do is restrict maxHeight to be no larger than
     * 'dInfoPtr->maxY + dInfoPtr->topPixelOffset'.
     */

    if (maxHeight > dInfoPtr->maxY + dInfoPtr->topPixelOffset) {
	maxHeight = (dInfoPtr->maxY + dInfoPtr->topPixelOffset);
    }

    if (maxHeight > 0) {
	pixmap = Tk_GetPixmap(Tk_Display(textPtr->tkwin),
		Tk_WindowId(textPtr->tkwin), Tk_Width(textPtr->tkwin),
		maxHeight, Tk_Depth(textPtr->tkwin));

	for (dlPtr = dInfoPtr->dLinePtr; dlPtr && dlPtr->y < dInfoPtr->maxY; dlPtr = dlPtr->nextPtr) {
	    if (!dlPtr->chunkPtr) {
		continue;
	    }
	    if ((dlPtr->flags & OLD_Y_INVALID) || dlPtr->oldY != dlPtr->y) {
		if (tkTextDebug) {
		    char string[TK_POS_CHARS];

		    TkTextPrintIndex(textPtr, &dlPtr->index, string);
		    LOG("tk_textRedraw", string);
		}
		DisplayDLine(textPtr, dlPtr, dlPtr->prevPtr, pixmap);
		if (dInfoPtr->dLinesInvalidated) {
		    Tk_FreePixmap(Tk_Display(textPtr->tkwin), pixmap);
		    goto doScrollbars;
		}
		dlPtr->oldY = dlPtr->y;
		dlPtr->flags &= ~(NEW_LAYOUT | OLD_Y_INVALID);
	    } else if (dInfoPtr->countWindows > 0
		    && dlPtr->chunkPtr
		    && (dlPtr->y < 0 || dlPtr->y + dlPtr->height > dInfoPtr->maxY)) {
		TkTextDispChunk *chunkPtr;

		/*
		 * It's the first or last DLine which are also overlapping the
		 * top or bottom of the window, but we decided above it wasn't
		 * necessary to display them (we were able to update them by
		 * scrolling). This is fine, except that if the lines contain
		 * any embedded windows, we must still call the display proc
		 * on them because they might need to be unmapped or they
		 * might need to be moved to reflect their new position.
		 * Otherwise, everything else moves, but the embedded window
		 * doesn't!
		 *
		 * So, we loop through all the chunks, calling the display
		 * proc of embedded windows only.
		 */

		for (chunkPtr = dlPtr->chunkPtr; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
		    int x;

		    if (chunkPtr->layoutProcs->type != TEXT_DISP_WINDOW) {
			continue;
		    }
		    x = chunkPtr->x + dInfoPtr->x - dInfoPtr->curXPixelOffset;
		    if (x + chunkPtr->width <= 0 || x >= dInfoPtr->maxX) {
			/*
			 * Note: we have to call the displayProc even for
			 * chunks that are off-screen. This is needed, for
			 * example, so that embedded windows can be unmapped
			 * in this case. Display the chunk at a coordinate
			 * that can be clearly identified by the displayProc
			 * as being off-screen to the left (the displayProc
			 * may not be able to tell if something is off to the
			 * right).
			 */

			x = -chunkPtr->width;
		    }
		    chunkPtr->layoutProcs->displayProc(textPtr, chunkPtr, x,
			    dlPtr->spaceAbove,
			    dlPtr->height - dlPtr->spaceAbove - dlPtr->spaceBelow,
			    dlPtr->baseline - dlPtr->spaceAbove, NULL,
			    (Drawable) None, dlPtr->y + dlPtr->spaceAbove);
		}

	    }
	}
	Tk_FreePixmap(Tk_Display(textPtr->tkwin), pixmap);
    }

    /*
     * See if we need to refresh the part of the window below the last line of
     * text (if there is any such area). Refresh the padding area on the left
     * too, since the insertion cursor might have been displayed there
     * previously).
     */

    if (dInfoPtr->topOfEof > dInfoPtr->maxY) {
	dInfoPtr->topOfEof = dInfoPtr->maxY;
    }
    if (bottomY < dInfoPtr->topOfEof) {
	TK_TEXT_DEBUG(LOG("tk_textRedraw", "eof"));

	if (textPtr->flags & DESTROYED) {
	    goto end; /* the widget has been deleted */
	}

	Tk_Fill3DRectangle(textPtr->tkwin, Tk_WindowId(textPtr->tkwin),
		textPtr->border, dInfoPtr->x - textPtr->padX, bottomY,
		dInfoPtr->maxX - (dInfoPtr->x - textPtr->padX),
		dInfoPtr->topOfEof - bottomY, 0, TK_RELIEF_FLAT);
    }
    dInfoPtr->topOfEof = bottomY;

    /*
     * Update the vertical scrollbar, if there is one. Note: it's important to
     * clear REDRAW_PENDING here, just in case the scroll function does
     * something that requires redisplay.
     */

  doScrollbars:
    if (textPtr->flags & UPDATE_SCROLLBARS) {

	/*
	 * Update the vertical scrollbar, if any.
	 */

	textPtr->flags &= ~UPDATE_SCROLLBARS;
	if (textPtr->yScrollCmd || textPtr->watchCmd) {
	    GetYView(textPtr->interp, textPtr, true);
	}

	/*
	 * Update the horizontal scrollbar, if any.
	 */

	if (textPtr->xScrollCmd || textPtr->watchCmd) {
	    GetXView(textPtr->interp, textPtr, true);
	}

	if (!(TriggerWatchCursor(textPtr))) {
	    goto end; /* the widget has been deleted */
	}
    }

    if (dInfoPtr->flags & ASYNC_PENDING) {
	assert(dInfoPtr->flags & ASYNC_UPDATE);
	dInfoPtr->flags &= ~ASYNC_PENDING;
	/* continue with asynchronous pixel-height calculation */
	InvokeAsyncUpdateLineMetrics(textPtr);
    }

  end:
    Tcl_Release(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextEventuallyRepick --
 *
 *	This function is invoked whenever something happens that could change
 *	the current character or the tags associated with it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A repick is scheduled as an idle handler.
 *
 *----------------------------------------------------------------------
 */

void
TkTextEventuallyRepick(
    TkText *textPtr)		/* Widget record for text widget. */
{
    textPtr->dInfoPtr->flags |= REPICK_NEEDED;
    DisplayTextWhenIdle(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextRedrawRegion --
 *
 *	This function is invoked to schedule a redisplay for a given region of
 *	a text widget. The redisplay itself may not occur immediately: it's
 *	scheduled as a when-idle handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information will eventually be redrawn on the screen.
 *
 *----------------------------------------------------------------------
 */

void
TkTextRedrawRegion(
    TkText *textPtr,		/* Widget record for text widget. */
    int x, int y,		/* Coordinates of upper-left corner of area to be redrawn, in
    				 * pixels relative to textPtr's window. */
    int width, int height)	/* Width and height of area to be redrawn. */
{
    TkRegion damageRgn = TkCreateRegion();
    XRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    TkUnionRectWithRegion(&rect, damageRgn, damageRgn);
    TextInvalidateRegion(textPtr, damageRgn);
    TkDestroyRegion(damageRgn);

    DisplayTextWhenIdle(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TextInvalidateRegion --
 *
 *	Mark a region of text as invalid.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the display information for the text widget.
 *
 *----------------------------------------------------------------------
 */

static void
TextInvalidateRegion(
    TkText *textPtr,		/* Widget record for text widget. */
    TkRegion region)		/* Region of area to redraw. */
{
    DLine *dlPtr;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    int maxY, inset;
    int extent1, extent2;
    XRectangle rect;

    /*
     * Find all lines that overlap the given region and mark them for redisplay.
     */

    TkClipBox(region, &rect);
    maxY = rect.y + rect.height;
    for (dlPtr = dInfoPtr->dLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	if (!(dlPtr->flags & OLD_Y_INVALID)
		&& TkRectInRegion(region, rect.x, dlPtr->y, rect.width, dlPtr->height) != RectangleOut) {
	    dlPtr->flags |= OLD_Y_INVALID;
	}
    }
    if (dInfoPtr->topOfEof < maxY) {
	dInfoPtr->topOfEof = maxY;
    }
    dInfoPtr->currChunkPtr = NULL;

    /*
     * Schedule the redisplay operation if there isn't one already scheduled.
     */

    inset = textPtr->borderWidth + textPtr->highlightWidth;
    extent1 = MIN(textPtr->padX, textPtr->insertWidth/2);
    extent2 = MIN(textPtr->padX, (textPtr->insertWidth + 1)/2);
    if (rect.x < inset + textPtr->padX - extent1
	    || rect.y < inset + textPtr->padY
	    || (int) (rect.x + rect.width) > Tk_Width(textPtr->tkwin) - inset - textPtr->padX + extent1 + extent2
	    || maxY > Tk_Height(textPtr->tkwin) - inset - textPtr->padY) {
	dInfoPtr->flags |= REDRAW_BORDERS;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextChanged --
 *
 *	This function is invoked when info in a text widget is about to be
 *	modified in a way that changes how it is displayed (e.g. characters
 *	were inserted or deleted, or tag information was changed). This
 *	function must be called *before* a change is made, so that indexes in
 *	the display information are still valid.
 *
 *	Note: if the range of indices may change geometry as well as simply
 *	requiring redisplay, then the caller should also call
 *	TkTextInvalidateLineMetrics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The range of character between index1Ptr (inclusive) and index2Ptr
 *	(exclusive) will be redisplayed at some point in the future (the
 *	actual redisplay is scheduled as a when-idle handler).
 *
 *----------------------------------------------------------------------
 */

static void
TextChanged(
    TkText *textPtr,			/* Widget record for text widget, or NULL. */
    const TkTextIndex *index1Ptr,	/* Index of first character to redisplay. */
    const TkTextIndex *index2Ptr)	/* Index of character just after last one to redisplay. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextLine *lastLinePtr = TkBTreeGetLastLine(textPtr);
    DLine *firstPtr = NULL;
    DLine *lastPtr= NULL;
    TkTextIndex rounded;
    TkTextLine *linePtr;

    /*
     * Find the DLines corresponding to index1Ptr and index2Ptr. There is one
     * tricky thing here, which is that we have to relayout in units of whole
     * text lines: This is necessary because the indices stored in the display
     * lines will no longer be valid. It's also needed because any edit could
     * change the way lines wrap.
     * To relayout in units of whole text (logical) lines, round index1Ptr
     * back to the beginning of its text line (or, if this line start is
     * elided, to the beginning of the text line that starts the display line
     * it is included in), and include all the display lines after index2Ptr,
     * up to the end of its text line (or, if this line end is elided, up to
     * the end of the first non elided text line after this line end).
     */

    if ((linePtr = TkTextIndexGetLine(index1Ptr)) != lastLinePtr) {
	rounded = *index1Ptr;
	TkTextIndexSetLine(&rounded, TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr));

	if (!(firstPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &rounded))) {
	    /*
	     * index1Ptr pertains to no display line, i.e this index is after
	     * the last display line. Since index2Ptr is after index1Ptr, there
	     * is no display line to free/redisplay and we can return early.
	     */
	} else {
	    rounded = *index2Ptr;
	    linePtr = TkTextIndexGetLine(index2Ptr);
	    if (linePtr == lastLinePtr) {
		linePtr = NULL;
	    } else {
		linePtr = TkBTreeNextLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
		TkTextIndexSetLine(&rounded, linePtr);
	    }

	    if (!linePtr) {
		lastPtr = NULL;
	    } else {
		/*
		 * 'rounded' now points to the start of a display line as well as the
		 * start of a logical line not merged with its previous line, and
		 * this index is the closest after index2Ptr.
		 */

		lastPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &rounded);

#if 0
		/*
		 * NOTE: In revised implementation this seems not to be useful,
		 * it is only causing superfluous redrawings.
		 */

		/*
		 * At least one display line is supposed to change. This makes the
		 * redisplay OK in case the display line we expect to get here was
		 * unlinked by a previous call to TkTextChanged and the text widget
		 * did not update before reaching this point. This happens for
		 * instance when moving the cursor up one line.
		 * Note that lastPtr != NULL here, otherwise we would have returned
		 * earlier when we tested for firstPtr being NULL.
		 */

		if (lastPtr && lastPtr == firstPtr) {
		    lastPtr = lastPtr->nextPtr;
		}
#endif
	    }
	}
    }

    /*
     * Schedule both a redisplay and a recomputation of display information.
     * It's done here rather than the end of the function for two reasons:
     *
     * 1. If there are no display lines to update we'll want to return
     *	  immediately, well before the end of the function.
     *
     * 2. It's important to arrange for the redisplay BEFORE calling
     *	  FreeDLines. The reason for this is subtle and has to do with
     *	  embedded windows. The chunk delete function for an embedded window
     *	  will schedule an idle handler to unmap the window. However, we want
     *	  the idle handler for redisplay to be called first, so that it can
     *	  put the embedded window back on the screen again (if appropriate).
     *	  This will prevent the window from ever being unmapped, and thereby
     *	  avoid flashing.
     */

    DisplayTextWhenIdle(textPtr);
    dInfoPtr->flags |= DINFO_OUT_OF_DATE|REPICK_NEEDED;
    dInfoPtr->currChunkPtr = NULL;

    /*
     * Delete all the DLines from firstPtr up to but not including lastPtr.
     */

    FreeDLines(textPtr, firstPtr, lastPtr, DLINE_UNLINK_KEEP_BRKS);
}

void
TkTextChanged(
    TkSharedText *sharedTextPtr,	/* Shared widget section, or NULL. */
    TkText *textPtr,			/* Widget record for text widget, or NULL. */
    const TkTextIndex *index1Ptr,	/* Index of first character to redisplay. */
    const TkTextIndex *index2Ptr)	/* Index of character just after last one to redisplay. */
{
    assert(!sharedTextPtr != !textPtr);

    if (!sharedTextPtr) {
	TextChanged(textPtr, index1Ptr, index2Ptr);
    } else {
	TkTextIndex index1 = *index1Ptr;
	TkTextIndex index2 = *index2Ptr;

	for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	    DEBUG(index1.discardConsistencyCheck = true);
	    DEBUG(index2.discardConsistencyCheck = true);
	    TkTextIndexSetPeer(&index1, textPtr);
	    TkTextIndexSetPeer(&index2, textPtr);
	    TextChanged(textPtr, &index1, &index2);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextRedrawTag --
 *
 *	This function is invoked to request a redraw of all characters in a
 *	given range that have a particular tag on or off. It's called, for
 *	example, when tag options change.
 *
 * Results:
 *	Return whether any redraw will happen.
 *
 * Side effects:
 *	Information on the screen may be redrawn, and the layout of the screen
 *	may change.
 *
 *----------------------------------------------------------------------
 */

static void
TextRedrawTag(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *index1Ptr,
    				/* First character in range to consider for redisplay. NULL
				 * means start at beginning of text. */
    const TkTextIndex *index2Ptr,
    				/* Character just after last one to consider for redisplay.
				 * NULL means process all the characters in the text. */
    bool affectsDisplayGeometry)/* Whether the display geometry is affected. */
{
    TextDInfo *dInfoPtr;
    DLine *dlPtr;
    DLine *endPtr;

    if (textPtr->flags & DESTROYED) {
	return;
    }

    assert(index1Ptr);
    assert(index2Ptr);
    assert(textPtr);

    dInfoPtr = textPtr->dInfoPtr;
    dlPtr = dInfoPtr->dLinePtr;

    if (!dlPtr) {
	return;
    }

    /*
     * Invalidate the pixel calculation of all lines in the given range.
     */

    if (affectsDisplayGeometry) {
	TkTextLine *startLine, *endLine;
	unsigned lineCount;

	dInfoPtr->currChunkPtr = NULL; /* reset cached chunk */
	endLine = TkTextIndexGetLine(index2Ptr);
	if (endLine == textPtr->endMarker->sectionPtr->linePtr) {
	    assert(endLine->prevPtr);
	    endLine = endLine->prevPtr;
	}
	lineCount = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, endLine, NULL);
	startLine = TkTextIndexGetLine(index1Ptr);
	lineCount -= TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, startLine, NULL);
	TkTextInvalidateLineMetrics(NULL, textPtr, startLine, lineCount, TK_TEXT_INVALIDATE_ONLY);
    }

    /*
     * Round up the starting position if it's before the first line visible on
     * the screen (we only care about what's on the screen).
     */

    if (TkTextIndexCompare(&dlPtr->index, index1Ptr) > 0) {
	index1Ptr = &dlPtr->index;
    }

    /*
     * Schedule a redisplay and layout recalculation if they aren't already
     * pending. This has to be done before calling FreeDLines, for the reason
     * given in TkTextChanged.
     */

    DisplayTextWhenIdle(textPtr);
    dInfoPtr->flags |= DINFO_OUT_OF_DATE|REPICK_NEEDED;

    /*
     * Each loop through the loop below is for one range of characters where
     * the tag's current state is different than its eventual state. At the
     * top of the loop, search contains information about the first character
     * in the range.
     */

    dlPtr = FindDLine(textPtr, dlPtr, index1Ptr);

    if (dlPtr) {
	/*
	 * Find the first DLine structure that's past the end of the range.
	 */

	endPtr = FindDLine(textPtr, dlPtr, index2Ptr);
	if (endPtr && TkTextIndexCompare(&endPtr->index, index2Ptr) < 0) {
	    endPtr = endPtr->nextPtr;
	}

	/*
	 * Delete all of the display lines in the range, so that they'll be
	 * re-layed out and redrawn.
	 */

	FreeDLines(textPtr, dlPtr, endPtr, DLINE_UNLINK);
    }
}

static void
RedrawTagsInPeer(
    const TkSharedText *sharedTextPtr,
    TkText *textPtr,
    TkTextIndex *indexPtr1,
    TkTextIndex *indexPtr2,
    bool affectsDisplayGeometry)
{
    TkTextIndex start, end;

    if (!textPtr->dInfoPtr || !textPtr->dInfoPtr->dLinePtr) {
	return;
    }

    if (textPtr->startMarker != sharedTextPtr->startMarker) {
	TkTextIndexSetupToStartOfText(&start, textPtr, sharedTextPtr->tree);
	if (TkTextIndexCompare(indexPtr1, &start) <= 0) {
	    indexPtr1 = &start;
	}
    }

    if (textPtr->endMarker != sharedTextPtr->endMarker) {
	TkTextIndexSetupToEndOfText(&end, textPtr, sharedTextPtr->tree);
	if (TkTextIndexCompare(indexPtr2, &end) <= 0) {
	    indexPtr2 = &end;
	}
    }

    TkTextIndexSetPeer(indexPtr1, textPtr);
    TkTextIndexSetPeer(indexPtr2, textPtr);
    TextRedrawTag(textPtr, indexPtr1, indexPtr2, affectsDisplayGeometry);
}

bool
TkTextRedrawTag(
    const TkSharedText *sharedTextPtr,
    				/* Shared widget section, or NULL if textPtr is not NULL. */
    TkText *textPtr,		/* Widget record for text widget, or NULL if sharedTextPtr is not
    				 * NULL. */
    const TkTextIndex *index1Ptr,
    				/* First character in range to consider for redisplay. NULL means
				 * start at beginning of text. */
    const TkTextIndex *index2Ptr,
    				/* Character just after last one to consider for redisplay. NULL
				 * means process all the characters in the text. Note that either
				 * both indices are NULL, or both are non-Null. */
    const TkTextTag *tagPtr,	/* Information about tag, can be NULL, but only if the indices are
    				 * non-NULL*/
    bool affectsDisplayGeometry)/* Whether the display geometry is affected. If argument tagPtr is
    				 * given, then also this tag will be tested if the display geometry
				 * is affected. */
{
    assert(!index1Ptr == !index2Ptr);
    assert(index1Ptr || tagPtr);
    assert(sharedTextPtr || textPtr);

    if (!sharedTextPtr && !textPtr->dInfoPtr->dLinePtr) {
	return false;
    }

    if (tagPtr && tagPtr->affectsDisplayGeometry) {
	affectsDisplayGeometry = true;
    }

    if (!index1Ptr) {
	TkTextSegment *endMarker;
	TkTextSearch search;
	TkTextIndex startIndex, endIndex;

	if (!sharedTextPtr) {
	    TkTextIndexClear2(&startIndex, NULL, textPtr->sharedTextPtr->tree);
	    TkTextIndexClear2(&endIndex, NULL, textPtr->sharedTextPtr->tree);
	    TkTextIndexSetSegment(&startIndex, textPtr->startMarker);
	    TkTextIndexSetSegment(&endIndex, textPtr->endMarker);
	    endMarker = textPtr->endMarker;
	} else {
	    TkTextIndexClear2(&startIndex, NULL, sharedTextPtr->tree);
	    TkTextIndexClear2(&endIndex, NULL, sharedTextPtr->tree);
	    TkTextIndexSetSegment(&startIndex, sharedTextPtr->startMarker);
	    TkTextIndexSetSegment(&endIndex, sharedTextPtr->endMarker);
	    endMarker = sharedTextPtr->endMarker;
	}

	/*
	 * Now we try to restrict the range, because redrawing is in general an expensive
	 * operation.
	 */

	if (tagPtr) {
	    bool found = false;

	    TkBTreeStartSearch(&startIndex, &endIndex, tagPtr, &search, SEARCH_EITHER_TAGON_TAGOFF);

	    while (true) {
		if (!TkBTreeNextTag(&search)) {
		    return found;
		}
		if (search.tagon) {
		    /* we need end of range */
		    startIndex = search.curIndex;
		    TkBTreeNextTag(&search);
		    assert(search.segPtr); /* search must not fail */
		} else {
		    assert(!found);
		}
		found = true;
		assert(!search.tagon);
		if (!sharedTextPtr) {
		    TextRedrawTag(textPtr, &startIndex, &search.curIndex, affectsDisplayGeometry);
		} else {
		    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
			RedrawTagsInPeer(sharedTextPtr, textPtr, &startIndex, &search.curIndex,
				affectsDisplayGeometry);
		    }
		}
	    }
	} else {
	    const TkBitField *discardTags = NULL;
	    TkTextSegment *segPtr;
	    TkTextIndex index2;

	    if (affectsDisplayGeometry) {
		if (sharedTextPtr) {
		    discardTags = sharedTextPtr->notAffectDisplayTags;
		} else {
		    discardTags = textPtr->sharedTextPtr->notAffectDisplayTags;
		}
	    }
	    if (!(segPtr = TkBTreeFindNextTagged(&startIndex, &endIndex, discardTags))) {
		return false;
	    }
	    index2 = endIndex;

	    while (segPtr) {
		TkTextSegment *endPtr;

		TkTextIndexSetSegment(&startIndex, segPtr);
		endPtr = TkBTreeFindNextUntagged(&startIndex, &endIndex, discardTags);

		if (!endPtr) {
		    endPtr = endMarker;
		}

		TkTextIndexSetSegment(&index2, endPtr);

		if (!sharedTextPtr) {
		    TextRedrawTag(textPtr, &startIndex, &index2, affectsDisplayGeometry);
		} else {
		    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
			RedrawTagsInPeer(sharedTextPtr, textPtr, &startIndex, &index2,
				affectsDisplayGeometry);
		    }
		}
	    }
	}
    } else if (!sharedTextPtr) {
	TextRedrawTag(textPtr, index1Ptr, index2Ptr, affectsDisplayGeometry);
    } else {
	TkTextIndex index1 = *index1Ptr;
	TkTextIndex index2 = *index2Ptr;

	for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	    RedrawTagsInPeer(sharedTextPtr, textPtr, &index1, &index2, affectsDisplayGeometry);
	}
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextRelayoutWindow --
 *
 *	This function is called when something has happened that invalidates
 *	the whole layout of characters on the screen, such as a change in a
 *	configuration option for the overall text widget or a change in the
 *	window size. It causes all display information to be recomputed and
 *	the window to be redrawn.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the display information will be recomputed for the window and the
 *	window will be redrawn.
 *
 *----------------------------------------------------------------------
 */

void
TkTextRelayoutWindow(
    TkText *textPtr,		/* Widget record for text widget. */
    int mask)			/* OR'd collection of bits showing what has changed. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    XGCValues gcValues;
    GC newGC;
    bool recomputeGeometry;
    bool asyncLineCalculation;
    unsigned firstLineNo;
    unsigned lastLineNo;
    int maxX;

    if ((mask & TK_TEXT_LINE_REDRAW_BOTTOM_LINE) && dInfoPtr->lastDLinePtr) {
	dInfoPtr->lastDLinePtr->flags |= OLD_Y_INVALID;
    }

    /*
     * Schedule the window redisplay. See TkTextChanged for the reason why
     * this has to be done before any calls to FreeDLines.
     */

    DisplayTextWhenIdle(textPtr);
    dInfoPtr->flags |= REDRAW_BORDERS|DINFO_OUT_OF_DATE|REPICK_NEEDED;

    /*
     * (Re-)create the graphics context for drawing the traversal highlight.
     */

    gcValues.graphics_exposures = False;
    newGC = Tk_GetGC(textPtr->tkwin, GCGraphicsExposures, &gcValues);
    if (dInfoPtr->copyGC != None) {
	Tk_FreeGC(textPtr->display, dInfoPtr->copyGC);
    }
    dInfoPtr->copyGC = newGC;

    /*
     * (Re-)create the graphics context for drawing the characters "behind" the block cursor.
     */

    if (dInfoPtr->insertFgGC != None) {
	Tk_FreeGC(textPtr->display, dInfoPtr->insertFgGC);
	dInfoPtr->insertFgGC = None;
    }
    if (textPtr->state == TK_TEXT_STATE_NORMAL
	    && textPtr->blockCursorType
	    && textPtr->showInsertFgColor) {
	gcValues.foreground = textPtr->insertFgColorPtr->pixel;
	dInfoPtr->insertFgGC = Tk_GetGC(textPtr->tkwin, GCForeground, &gcValues);
    }

    maxX = MAX(Tk_Width(textPtr->tkwin) - dInfoPtr->x, dInfoPtr->x + 1);
    firstLineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, TkBTreeGetStartLine(textPtr), NULL);
    lastLineNo = TkBTreeLinesTo(sharedTextPtr->tree, NULL, TkBTreeGetLastLine(textPtr), NULL);
    recomputeGeometry = (maxX != dInfoPtr->maxX) || (mask & TK_TEXT_LINE_GEOMETRY);

    /*
     * Throw away all the current display lines, except the visible ones if
     * they will not change.
     */

    if (recomputeGeometry || (mask & TK_TEXT_LINE_REDRAW)) {
	FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK_KEEP_BRKS);
    }

    FreeDLines(textPtr, NULL, NULL, DLINE_CACHE);  /* release cached display lines */
    FreeDLines(textPtr, NULL, NULL, DLINE_METRIC); /* release cached lines */
    FreeDLines(textPtr, dInfoPtr->savedDLinePtr, NULL, DLINE_FREE_TEMP);

    /*
     * Recompute some overall things for the layout. Even if the window gets very small,
     * pretend that there's at least one pixel of drawing space in it.
     */

    assert(textPtr->highlightWidth >= 0);
    assert(textPtr->borderWidth >= 0);

    dInfoPtr->x = textPtr->highlightWidth + textPtr->borderWidth + textPtr->padX;
    dInfoPtr->y = textPtr->highlightWidth + textPtr->borderWidth + textPtr->padY;

    dInfoPtr->maxX = MAX(Tk_Width(textPtr->tkwin) - dInfoPtr->x, dInfoPtr->x + 1);

    /*
     * This is the only place where dInfoPtr->maxY is set.
     */

    dInfoPtr->maxY = MAX(Tk_Height(textPtr->tkwin) - dInfoPtr->y, dInfoPtr->y + 1);
    dInfoPtr->topOfEof = dInfoPtr->maxY;

    /*
     * If the upper-left character isn't the first in a line, recompute it.
     * This is necessary because a change in the window's size or options
     * could change the way lines wrap.
     */

    if (!IsStartOfNotMergedLine(&textPtr->topIndex)) {
	TkTextFindDisplayLineStartEnd(textPtr, &textPtr->topIndex, DISP_LINE_START);
    }

    /*
     * Invalidate cached scrollbar positions, so that scrollbars sliders will be udpated.
     */

    dInfoPtr->xScrollFirst = dInfoPtr->xScrollLast = -1;
    dInfoPtr->yScrollFirst = dInfoPtr->yScrollLast = -1;

    /*
     * Invalidate cached cursor chunk.
     */

    dInfoPtr->currChunkPtr = NULL;

    if (mask & TK_TEXT_LINE_GEOMETRY) {
	/* Setup end of line segment. */
	SetupEolSegment(textPtr, dInfoPtr);
	SetupEotSegment(textPtr, dInfoPtr);
    }

    asyncLineCalculation = false;

#if SPEEDUP_MONOSPACED_LINE_HEIGHTS
    if (TestMonospacedLineHeights(textPtr)) {
	TkRangeList *ranges = textPtr->dInfoPtr->lineMetricUpdateRanges;

	if (!TkRangeListIsEmpty(ranges)) {
	    TkBTreeUpdatePixelHeights(textPtr,
		    TkBTreeFindLine(sharedTextPtr->tree, textPtr, TkRangeListLow(ranges)),
		    TkRangeListSpan(ranges), dInfoPtr->lineMetricUpdateEpoch);
	    TkRangeListClear(ranges);
	}
	if (dInfoPtr->lineHeight != textPtr->lineHeight) {
	    TkBTreeUpdatePixelHeights(textPtr, TkBTreeGetStartLine(textPtr), lastLineNo - firstLineNo,
		    dInfoPtr->lineMetricUpdateEpoch);
	    dInfoPtr->lineHeight = textPtr->lineHeight;
	}
    } else
#endif
    if (recomputeGeometry) {
	/*
	 * Set up line metric recalculation.
	 */

	dInfoPtr->lineHeight = 0;
	TkRangeListClear(dInfoPtr->lineMetricUpdateRanges);
	if (lastLineNo > firstLineNo) {
	    dInfoPtr->lineMetricUpdateRanges =
		    TkRangeListAdd(dInfoPtr->lineMetricUpdateRanges, 0, lastLineNo - firstLineNo - 1);
	    dInfoPtr->lineMetricUpdateEpoch += 1;
	    asyncLineCalculation = true;
	}
    } else {
	TkTextIndex index;
	DLine *dlPtr;
	int numLines;

	dInfoPtr->lineHeight = 0;

	/*
	 * We have to handle -startindex, -endIndex.
	 */

	if (lastLineNo == firstLineNo) {
	    FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK);
	    TkRangeListClear(dInfoPtr->lineMetricUpdateRanges);
	} else if (dInfoPtr->lastLineNo <= firstLineNo || lastLineNo <= dInfoPtr->firstLineNo) {
	    FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK);
	    TkRangeListClear(dInfoPtr->lineMetricUpdateRanges);
	    dInfoPtr->lineMetricUpdateRanges = TkRangeListAdd(
		    dInfoPtr->lineMetricUpdateRanges, 0, lastLineNo - firstLineNo - 1);
	    asyncLineCalculation = true;
	} else {
	    if (firstLineNo < dInfoPtr->firstLineNo) {
		dInfoPtr->lineMetricUpdateRanges = TkRangeListInsert(
			dInfoPtr->lineMetricUpdateRanges, 0, dInfoPtr->firstLineNo - firstLineNo - 1);
		asyncLineCalculation = true;
	    } else if (dInfoPtr->firstLineNo < firstLineNo) {
		TkTextIndexSetupToStartOfText(&index, textPtr, sharedTextPtr->tree);
		dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &index);
		FreeDLines(textPtr, dInfoPtr->dLinePtr, dlPtr, DLINE_UNLINK);
		numLines = firstLineNo - dInfoPtr->firstLineNo;
		TkRangeListDelete(dInfoPtr->lineMetricUpdateRanges, 0, numLines - 1);
	    }
	    if (dInfoPtr->lastLineNo < lastLineNo) {
		dInfoPtr->lineMetricUpdateRanges = TkRangeListAdd(
			dInfoPtr->lineMetricUpdateRanges,
			dInfoPtr->lastLineNo - dInfoPtr->firstLineNo,
			lastLineNo - firstLineNo - 1);
		asyncLineCalculation = true;
	    } else if (lastLineNo < dInfoPtr->lastLineNo) {
		TkTextIndexSetupToEndOfText(&index, textPtr, sharedTextPtr->tree);
		dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &index);
		FreeDLines(textPtr, dlPtr, NULL, DLINE_UNLINK);
		TkRangeListTruncateAtEnd(dInfoPtr->lineMetricUpdateRanges, lastLineNo - firstLineNo - 1);
	    }
	}
    }

    dInfoPtr->firstLineNo = firstLineNo;
    dInfoPtr->lastLineNo = lastLineNo;

    if (asyncLineCalculation) {
	StartAsyncLineCalculation(textPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextSetYView --
 *
 *	This function is called to specify what lines are to be displayed in a
 *	text widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The display will (eventually) be updated so that the position given by
 *	"indexPtr" is visible on the screen at the position determined by
 *	"pickPlace".
 *
 *----------------------------------------------------------------------
 */

void
TkTextSetYView(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextIndex *indexPtr,	/* Position that is to appear somewhere in the view. */
    int pickPlace)		/* 0 means the given index must appear exactly at the top of the
    				 * screen. TK_TEXT_PICKPLACE (-1) means we get to pick where it
				 * appears: minimize screen motion or else display line at center
				 * of screen. TK_TEXT_NOPIXELADJUST (-2) indicates to make the
				 * given index the top line, but if it is already the top line,
				 * don't nudge it up or down by a few pixels just to make sure
				 * it is entirely displayed. Positive numbers indicate the number
				 * of pixels of the index's line which are to be off the top of
				 * the screen. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr;
    int bottomY, close;
    TkTextIndex tmpIndex;
    TkTextLine *linePtr;
    int lineHeight;
    int topLineNo;
    int topByteIndex;
    int32_t overlap;

    if (TkTextIsDeadPeer(textPtr)) {
	textPtr->topIndex = *indexPtr;
	TkTextIndexSetPeer(&textPtr->topIndex, textPtr);
	return;
    }

    /*
     * If the specified position is the extra line at the end of the text,
     * round it back to the last real line.
     */

    linePtr = TkTextIndexGetLine(indexPtr);

    if (linePtr == TkBTreeGetLastLine(textPtr) && TkTextIndexGetByteIndex(indexPtr) == 0) {
	assert(linePtr->prevPtr);
	assert(TkBTreeGetStartLine(textPtr) != linePtr);
	TkTextIndexSetToEndOfLine2(indexPtr, linePtr->prevPtr);
    }

    if (pickPlace == TK_TEXT_NOPIXELADJUST) {
	pickPlace = TkTextIndexIsEqual(&textPtr->topIndex, indexPtr) ? dInfoPtr->topPixelOffset : 0;
    }

    if (pickPlace != TK_TEXT_PICKPLACE) {
	/*
	 * The specified position must go at the top of the screen. Just leave
	 * all the DLine's alone: we may be able to reuse some of the information
	 * that's currently on the screen without redisplaying it all.
	 */

	textPtr->topIndex = *indexPtr;
	TkTextIndexSetPeer(&textPtr->topIndex, textPtr);
	TkTextIndexToByteIndex(&textPtr->topIndex);
        if (!IsStartOfNotMergedLine(indexPtr)) {
            TkTextFindDisplayLineStartEnd(textPtr, &textPtr->topIndex, DISP_LINE_START);
        }
	dInfoPtr->newTopPixelOffset = pickPlace;
	goto scheduleUpdate;
    }

    /*
     * We have to pick where to display the index. First, bring the display
     * information up to date and see if the index will be completely visible
     * in the current screen configuration. If so then there's nothing to do.
     */

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    if ((dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, indexPtr))) {
	int x, y, width, height;

	if (TkTextIndexCompare(&dlPtr->index, indexPtr) <= 0
		&& GetBbox(textPtr, dlPtr, indexPtr, &x, &y, &width, &height, NULL, NULL)) {
	    assert(TkTextIndexCountBytes(&dlPtr->index, indexPtr) <= dlPtr->byteCount);
	    if (dInfoPtr->y <= y && y + height <= dInfoPtr->maxY - dInfoPtr->y) {
		return; /* this character is fully visible, so we don't need to scroll */
	    }
	    if (dlPtr->height > dInfoPtr->maxY - dInfoPtr->y) {
		/*
		 * This line has more height than the view, so either center the char,
		 * or position at top of view.
		 */
		textPtr->topIndex = *indexPtr;
		dInfoPtr->newTopPixelOffset = MAX(0, y - dlPtr->y - (dInfoPtr->maxY - height)/2);
		goto scheduleUpdate;
	    }
	}
	if (dlPtr->y + dlPtr->height > dInfoPtr->maxY) {
	    /*
	     * Part of the line hangs off the bottom of the screen; pretend
	     * the whole line is off-screen.
	     */
	    dlPtr = NULL;
	} else {
	    if (TkTextIndexCompare(&dlPtr->index, indexPtr) <= 0) {
		if (dInfoPtr->dLinePtr == dlPtr && dInfoPtr->topPixelOffset != 0) {
		    /*
		     * It is on the top line, but that line is hanging off the top
		     * of the screen. Change the top overlap to zero and update.
		     */
		    dInfoPtr->newTopPixelOffset = 0;
		    goto scheduleUpdate;
		}
		return; /* the line is already on screen, with no need to scroll */
	    }
	}
    }

    /*
     * The desired line isn't already on-screen. Figure out what it means to
     * be "close" to the top or bottom of the screen. Close means within 1/3
     * of the screen height or within three lines, whichever is greater.
     *
     * If the line is not close, place it in the center of the window.
     */

    tmpIndex = *indexPtr;
    FindDisplayLineStartEnd(textPtr, &tmpIndex, DISP_LINE_START, DLINE_METRIC);
    lineHeight = CalculateDisplayLineHeight(textPtr, &tmpIndex, NULL);

    if (lineHeight > dInfoPtr->maxY - dInfoPtr->y) {
	int x, y, width, height;
	DisplayInfo info;

	/*
	 * In this case we have either to center the char, or to bring it to
	 * the top position, otherwise it may happen that it will not be visible.
	 */

	FreeDLines(textPtr, dInfoPtr->dLinePtr, NULL, DLINE_UNLINK); /* not needed anymore */
	ComputeDisplayLineInfo(textPtr, indexPtr, &info);
	if (!info.dLinePtr) {
	    tmpIndex = *indexPtr;
	    TkTextIndexBackBytes(textPtr, &tmpIndex, info.byteOffset, &tmpIndex);
	    dlPtr = info.dLinePtr = info.lastDLinePtr = LayoutDLine(&tmpIndex, info.displayLineNo);
	    SaveDisplayLines(textPtr, &info, true);
	}
	GetBbox(textPtr, dlPtr, indexPtr, &x, &y, &width, &height, NULL, NULL);
	dInfoPtr->newTopPixelOffset = MAX(0, y - dlPtr->y - (dInfoPtr->maxY - height)/2);
	textPtr->topIndex = *indexPtr;
    } else {
	/*
	 * It would be better if 'bottomY' were calculated using the actual height
	 * of the given line, not 'textPtr->lineHeight'.
	 */

	bottomY = (dInfoPtr->y + dInfoPtr->maxY + lineHeight)/2;
	close = (dInfoPtr->maxY - dInfoPtr->y)/3;
	if (close < 3*textPtr->lineHeight) {
	    close = 3*textPtr->lineHeight;
	}
	if (dlPtr) {

	    /*
	     * The desired line is above the top of screen. If it is "close" to
	     * the top of the window then make it the top line on the screen.
	     * MeasureUp counts from the bottom of the given index upwards, so we
	     * add an extra half line to be sure we count far enough.
	     */

	    MeasureUp(textPtr, &textPtr->topIndex, close + textPtr->lineHeight/2, &tmpIndex, &overlap);
	    if (TkTextIndexCompare(&tmpIndex, indexPtr) <= 0) {
		textPtr->topIndex = *indexPtr;
		TkTextIndexSetPeer(&textPtr->topIndex, textPtr);
		TkTextIndexToByteIndex(&textPtr->topIndex);
		TkTextFindDisplayLineStartEnd(textPtr, &textPtr->topIndex, DISP_LINE_START);
		dInfoPtr->newTopPixelOffset = 0;
		goto scheduleUpdate;
	    }
	} else {
	    /*
	     * The desired line is below the bottom of the screen. If it is
	     * "close" to the bottom of the screen then position it at the bottom
	     * of the screen.
	     */

	    MeasureUp(textPtr, indexPtr, close + lineHeight - textPtr->lineHeight/2, &tmpIndex,
		    &overlap);
	    if (FindDLine(textPtr, dInfoPtr->dLinePtr, &tmpIndex)) {
		bottomY = dInfoPtr->maxY - dInfoPtr->y;
	    }
	}

	/*
	 * If the window height is smaller than the line height, prefer to make
	 * the top of the line visible.
	 */

	if (dInfoPtr->maxY - dInfoPtr->y < lineHeight) {
	    bottomY = lineHeight;
	}

	/*
	 * Our job now is to arrange the display so that indexPtr appears as low
	 * on the screen as possible but with its bottom no lower than bottomY.
	 * BottomY is the bottom of the window if the desired line is just below
	 * the current screen, otherwise it is a half-line lower than the center
	 * of the window.
	 */

	MeasureUp(textPtr, indexPtr, bottomY, &textPtr->topIndex, &dInfoPtr->newTopPixelOffset);
    }

  scheduleUpdate:
    topLineNo = TkTextIndexGetLineNumber(&textPtr->topIndex, NULL);
    topByteIndex = TkTextIndexGetByteIndex(&textPtr->topIndex);

    if (dInfoPtr->newTopPixelOffset != dInfoPtr->topPixelOffset
	    || dInfoPtr->topLineNo != topLineNo
	    || dInfoPtr->topByteIndex != topByteIndex) {
	DisplayTextWhenIdle(textPtr);
	dInfoPtr->flags |= DINFO_OUT_OF_DATE|REPICK_NEEDED;
	dInfoPtr->topLineNo = topLineNo;
	dInfoPtr->topByteIndex = topByteIndex;
    }
}

/*
 *--------------------------------------------------------------
 *
 * FindDisplayLineOffset --
 *
 *	Given a line pointer to a logical line, and a distance in
 *	pixels, find the byte offset of the corresponding display
 *	line. If only one display line belongs to the given logical
 *	line, then the offset is always zero.
 *
 * Results:
 *	Returns the offset to the display line at specified pixel
 *	position, relative to the given logical line. 'distance'
 *	will be set to the vertical distance in pixels measured
 *	from the top pixel in specified display line (this means,
 *	that all the display lines pixels between the top of the
 *	logical line and the corresponding display line will be
 *	subtracted).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static const TkTextDispLineEntry *
SearchPixelEntry(
    const TkTextDispLineEntry *first,
    const TkTextDispLineEntry *last,
    unsigned pixels)
{
    assert(first != last);

    if ((last - 1)->pixels < pixels) {
	return last - 1; /* catch a frequent case */
    }

    do {
	const TkTextDispLineEntry *mid = first + (last - first)/2;

	if (mid->pixels <= pixels) {
	    first = mid + 1;
	} else {
	    last = mid;
	}
    } while (first != last);

    return first;
}

static unsigned
FindDisplayLineOffset(
    TkText *textPtr,
    TkTextLine *linePtr,
    int32_t *distance)	/* IN:  distance in pixels to logical line
    			 * OUT: distance in pixels of specified display line. */
{
    const TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);
    const TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;
    const TkTextDispLineEntry *lastEntry;
    const TkTextDispLineEntry *entry;

    assert(distance);
    assert(*distance >= 0);
    assert(linePtr->logicalLine);

    if (!dispLineInfo) {
	return 0;
    }

    lastEntry = dispLineInfo->entry + dispLineInfo->numDispLines;
    entry = SearchPixelEntry(dispLineInfo->entry, lastEntry, *distance);
    assert(entry != lastEntry);
    if (entry == dispLineInfo->entry) {
	return 0;
    }

    *distance -= (entry - 1)->pixels;
    return entry->byteOffset;
}

/*
 *--------------------------------------------------------------
 *
 * MeasureDown --
 *
 *	Given one index, find the index of the first character on the highest
 *	display line that would be displayed no more than "distance" pixels
 *	below the top of the given index.
 *
 * Results:
 *	The srcPtr is manipulated in place to reflect the new position. We
 *	return the number of pixels by which 'distance' overlaps the srcPtr
 *	in 'overlap'. The return valus is 'false' if we are already at top
 *	of view, otherwise the return valus is 'true'.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
AlreadyAtBottom(
    const TkText *textPtr)
{
    const TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr = dInfoPtr->lastDLinePtr;
    TkTextIndex index;

    if (!dlPtr) {
	return true;
    }
    if (dlPtr->y + dlPtr->height != dInfoPtr->maxY) {
	return false;
    }

    index = dlPtr->index;
    TkTextIndexForwBytes(textPtr, &index, dlPtr->byteCount, &index);
    return TkTextIndexIsEndOfText(&index);
}

static bool
MeasureDown(
    TkText *textPtr,		/* Text widget in which to measure. */
    TkTextIndex *srcPtr,	/* Index of character from which to start measuring. */
    int distance,		/* Vertical distance in pixels measured from the top pixel in
    				 * srcPtr's logical line. */
    int32_t *overlap,		/* The number of pixels by which 'distance' overlaps the srcPtr. */
    bool saveDisplayLines)	/* Save produced display line for re-use in UpdateDisplayInfo? */
{
    const TkTextLine *lastLinePtr;
    TkTextLine *linePtr;
    TkTextIndex index;
    int byteOffset;
    int32_t myOverlap;

    if (AlreadyAtBottom(textPtr)) {
	return false;
    }

    if (!overlap) {
	overlap = &myOverlap;
    }

    linePtr = TkTextIndexGetLine(srcPtr);
    lastLinePtr = TkBTreeGetLastLine(textPtr);

    if (TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges)) {
	int pixelHeight;

	/*
	 * No display line metric calculation is pending, this is fine,
	 * now we can use the B-Tree for the measurement.
	 *
	 * Note that TkBTreePixelsTo is measuring up to the logical line.
	 */

	pixelHeight = TkBTreePixelsTo(textPtr, linePtr);
	pixelHeight += GetPixelsTo(textPtr, srcPtr, false, NULL);
	pixelHeight += distance;
	linePtr = TkBTreeFindPixelLine(srcPtr->tree, textPtr, pixelHeight, overlap);

	if (linePtr == lastLinePtr) {
	    TkTextLine *prevLinePtr = TkBTreePrevLine(textPtr, linePtr);
	    if (prevLinePtr) {
		linePtr = prevLinePtr;
	    }
	}

	/*
	 * We have the logical line, and the overlap, now search for the display line.
	 */

	byteOffset = FindDisplayLineOffset(textPtr, linePtr, overlap);
    } else {
	DisplayInfo info;

	/*
	 * Search down line by line until we'e found the bottom line for given distance.
	 */

	linePtr = ComputeDisplayLineInfo(textPtr, srcPtr, &info);
	distance += GetPixelsTo(textPtr, srcPtr, false, &info);
	index = *srcPtr;

	while (true) {
	    ComputeMissingMetric(textPtr, &info, THRESHOLD_PIXEL_DISTANCE, distance);
	    if (saveDisplayLines) {
		SaveDisplayLines(textPtr, &info, true);
	    } else {
		FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
	    }

	    if (distance < info.pixels) {
		const TkTextDispLineInfo *dispLineInfo = info.pixelInfo->dispLineInfo;

		if (dispLineInfo) {
		    const TkTextDispLineEntry *entry, *last;

		    last = dispLineInfo->entry + dispLineInfo->numDispLines;
		    entry = SearchPixelEntry(dispLineInfo->entry, last, distance);
		    assert(entry < last);
		    byteOffset = entry->byteOffset;
		    if (entry != dispLineInfo->entry) {
			distance -= (entry - 1)->pixels;
		    }
		} else {
		    byteOffset = 0;
		}
		break;
	    }

	    if (TkTextIndexGetLine(&info.index) == lastLinePtr) {
		byteOffset = 0;
		distance = *overlap;
		break;
	    }
	    linePtr = TkTextIndexGetLine(&info.index);
	    if ((distance -= info.pixels) == 0) {
		byteOffset = 0;
		break;
	    }
	    TkTextIndexSetToStartOfLine2(&index, linePtr);
	    linePtr = ComputeDisplayLineInfo(textPtr, &index, &info);
	}

	*overlap = distance;
    }

    assert(linePtr != lastLinePtr);

    TkTextIndexSetToStartOfLine2(srcPtr, linePtr);
    TkTextIndexForwBytes(textPtr, srcPtr, byteOffset, srcPtr);

    return true;
}

/*
 *--------------------------------------------------------------
 *
 * MeasureUp --
 *
 *	Given one index, find the index of the first character on the highest
 *	display line that would be displayed no more than "distance" pixels
 *	above the given index.
 *
 *	If this function is called with distance=0, it simply finds the first
 *	index on the same display line as srcPtr. However, there is a another
 *	function TkTextFindDisplayLineStartEnd designed just for that task
 *	which is probably better to use.
 *
 * Results:
 *	*dstPtr is filled in with the index of the first character on a
 *	display line. The display line is found by measuring up "distance"
 *	pixels above the pixel just below an imaginary display line that
 *	contains srcPtr. If the display line that covers this coordinate
 *	actually extends above the coordinate, then return any excess pixels
 *	in *overlap. The return valus is 'false' if we are already at top
 *	of view, otherwise the return valus is 'false'.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
AlreadyAtTop(
    const TkText *textPtr)
{
    const TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    if (!dInfoPtr->dLinePtr) {
	return true;
    }
    return dInfoPtr->topPixelOffset == 0 && TkTextIndexIsStartOfText(&dInfoPtr->dLinePtr->index);
}

static bool
MeasureUp(
    TkText *textPtr,		/* Text widget in which to measure. */
    const TkTextIndex *srcPtr,	/* Index of character from which to start measuring. */
    int distance,		/* Vertical distance in pixels measured from the pixel just below
    				 * the lowest one in srcPtr's line. */
    TkTextIndex *dstPtr,	/* Index to fill in with result. */
    int32_t *overlap)		/* Used to store how much of the final index returned was not covered
    				 * by 'distance'. */
{
    TkTextLine *linePtr;
    TkTextLine *startLinePtr;
    unsigned byteOffset;

    assert(overlap);
    assert(dstPtr);

    if (TkTextIndexIsStartOfText(srcPtr) && AlreadyAtTop(textPtr)) {
	return false;
    }

    *dstPtr = *srcPtr;
    startLinePtr = TkBTreeGetStartLine(textPtr);
    linePtr = TkTextIndexGetLine(srcPtr);

    if (TestIfLinesUpToDate(srcPtr)) {
	int pixelHeight;

	/*
	 * No display line height calculation is pending (not in required range),
	 * this is fine, now we can use the B-Tree for the measurement.
	 *
	 * Note that TkBTreePixelsTo is measuring up to the logical line.
	 */

	pixelHeight  = TkBTreePixelsTo(textPtr, linePtr);
	pixelHeight += GetPixelsTo(textPtr, srcPtr, true, NULL);
	pixelHeight -= distance;

	if (pixelHeight <= 0) {
	    linePtr = startLinePtr;
	    byteOffset = *overlap = 0;
	} else {
	    linePtr = TkBTreeFindPixelLine(srcPtr->tree, textPtr, pixelHeight, overlap);
	    byteOffset = FindDisplayLineOffset(textPtr, linePtr, overlap);
	}
    } else {
	DisplayInfo info;

	/*
	 * Search up line by line until we have found the start line for given distance.
	 */

	linePtr = ComputeDisplayLineInfo(textPtr, srcPtr, &info);
	SaveDisplayLines(textPtr, &info, false);
	distance -= GetPixelsTo(textPtr, srcPtr, true, &info);

	while (linePtr != startLinePtr && distance > 0) {
	    TkTextIndexSetToLastChar2(dstPtr, linePtr->prevPtr);
	    linePtr = ComputeDisplayLineInfo(textPtr, dstPtr, &info);
	    SaveDisplayLines(textPtr, &info, false);
	    distance -= info.pixels;
	}

	if (distance < 0) {
	    *overlap = -distance;
	    byteOffset = FindDisplayLineOffset(textPtr, linePtr, overlap);
	} else {
	    byteOffset = *overlap = 0;
	}
    }

    TkTextIndexSetToStartOfLine2(dstPtr, linePtr);
    TkTextIndexForwBytes(textPtr, dstPtr, byteOffset, dstPtr);
    return true;
}
/*
 *--------------------------------------------------------------
 *
 * GetBbox --
 *
 *	Given an index, find the bounding box of the screen area occupied by
 *	the entity (character, window, image) at that index.
 *
 * Results:
 *	The byte count inside the chunk is returned if the index is on the screen.
 *	'false' means the index is not on the screen. If the return value is true,
 *	then the bounding box of the part of the index that's visible on the screen
 *	is returned to *xPtr, *yPtr, *widthPtr, and *heightPtr.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
GetBbox(
    TkText *textPtr,		/* Information about text widget. */
    const DLine *dlPtr,		/* Display line for given index. */
    const TkTextIndex *indexPtr,/* Index whose bounding box is desired. */
    int *xPtr, int *yPtr,	/* Filled with index's upper-left coordinate. */
    int *widthPtr, int *heightPtr,
				/* Filled in with index's dimensions. */
    bool *isLastCharInLine,	/* Last char in display line? Can be NULL. */
    Tcl_UniChar *thisChar)	/* The character at specified position, can be NULL. Will be zero if
    				 * this is not a char chunk. */
{
    TkTextDispChunkSection *sectionPtr;
    TkTextDispChunk *chunkPtr;
    unsigned byteCount;

    assert(xPtr);
    assert(yPtr);
    assert(widthPtr);
    assert(heightPtr);

    /*
     * Find the chunk within the display line that contains the desired
     * index. The chunks making the display line are skipped up to but not
     * including the one crossing indexPtr. Skipping is done based on
     * a byteCount offset possibly spanning several logical lines in case
     * they are elided.
     */

    byteCount = TkTextIndexCountBytes(&dlPtr->index, indexPtr);
    sectionPtr = dlPtr->chunkPtr->sectionPtr;

    while (byteCount >= sectionPtr->numBytes) {
	byteCount -= sectionPtr->numBytes;
	if (!(sectionPtr = sectionPtr->nextPtr)) {
	    if (thisChar) { *thisChar = 0; }
	    return false;
	}
    }

    chunkPtr = sectionPtr->chunkPtr;

    while (byteCount >= chunkPtr->numBytes) {
	byteCount -= chunkPtr->numBytes;
	if (!(chunkPtr = chunkPtr->nextPtr)) {
	    if (thisChar) { *thisChar = 0; }
	    return false;
	}
    }

    /*
     * Call a chunk-specific function to find the horizontal range of the
     * character within the chunk, then fill in the vertical range. The
     * x-coordinate returned by bboxProc is a coordinate within a line, not a
     * coordinate on the screen. Translate it to reflect horizontal scrolling.
     */

    chunkPtr->layoutProcs->bboxProc(
	    textPtr, chunkPtr, byteCount,
	    dlPtr->y + dlPtr->spaceAbove,
	    dlPtr->height - dlPtr->spaceAbove - dlPtr->spaceBelow,
	    dlPtr->baseline - dlPtr->spaceAbove,
	    xPtr, yPtr, widthPtr, heightPtr);

    if (isLastCharInLine) {
	*isLastCharInLine = (byteCount == chunkPtr->numBytes - 1 && !chunkPtr->nextPtr);
    }

    if (thisChar) {
	if (IsCharChunk(chunkPtr)) {
	    const TkTextSegment *segPtr = CHAR_CHUNK_GET_SEGMENT(chunkPtr);
	    assert((int) byteCount < segPtr->size);
	    Tcl_UtfToUniChar(segPtr->body.chars + byteCount, thisChar);
	} else {
	    *thisChar = 0;
	}
    }

    return true;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextSeeCmd --
 *
 *	This function is invoked to process the "see" option for the widget
 *	command for text widgets. See the user documentation for details on
 *	what it does.
 *
 *	TODO: the current implementation does not consider that the position
 *	has to be fully visible.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
TkTextSeeCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "see". */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextIndex index;
    int x, y, width, height, oneThird, delta;
    unsigned lineWidth;
    DLine *dlPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "index");
	return TCL_ERROR;
    }
    if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index)) {
	return TCL_ERROR;
    }
    if (TkTextIsDeadPeer(textPtr)) {
	return TCL_OK;
    }

    /*
     * If the specified position is the extra line at the end of the text,
     * round it back to the last real line.
     */

    if (TkTextIndexGetLine(&index) == TkBTreeGetLastLine(textPtr)) {
	TkTextIndexSetToLastChar2(&index, TkTextIndexGetLine(&index)->prevPtr);
    }

    /*
     * First get the desired position into the vertical range of the window.
     */

    TkTextSetYView(textPtr, &index, TK_TEXT_PICKPLACE);

    /*
     * Now make sure that the character is in view horizontally.
     */

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    assert(dInfoPtr->maxX >= dInfoPtr->x);
    lineWidth = dInfoPtr->maxX - dInfoPtr->x;

    if (dInfoPtr->maxLength < lineWidth) {
	return TCL_OK;
    }

    /*
     * Take into account that the desired index is past the visible text.
     * It's also possible that the widget is not yet mapped.
     */

    if (!(dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, &index))) {
	return TCL_OK;
    }

    if (GetBbox(textPtr, dlPtr, &index, &x, &y, &width, &height, NULL, NULL)) {
        delta = x - dInfoPtr->curXPixelOffset;
        oneThird = lineWidth/3;
        if (delta < 0) {
            if (delta < -oneThird) {
                dInfoPtr->newXPixelOffset = x - lineWidth/2;
            } else {
                dInfoPtr->newXPixelOffset += delta;
            }
        } else {
            delta -= lineWidth - width;
            if (delta <= 0) {
                return TCL_OK;
            }
            if (delta > oneThird) {
                dInfoPtr->newXPixelOffset = x - lineWidth/2;
            } else {
                dInfoPtr->newXPixelOffset += delta;
            }
        }
    }

    dInfoPtr->flags |= DINFO_OUT_OF_DATE;
    DisplayTextWhenIdle(textPtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextXviewCmd --
 *
 *	This function is invoked to process the "xview" option for the widget
 *	command for text widgets. See the user documentation for details on
 *	what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
TkTextXviewCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "xview". */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    int count;
    double fraction;

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    if (objc == 2) {
	GetXView(interp, textPtr, false);
	return TCL_OK;
    }

    switch (TextGetScrollInfoObj(interp, textPtr, objc, objv, &fraction, &count)) {
    case SCROLL_ERROR:
	return TCL_ERROR;
    case SCROLL_MOVETO:
	dInfoPtr->newXPixelOffset = (int) (MIN(1.0, MAX(0.0, fraction))*dInfoPtr->maxLength + 0.5);
	break;
    case SCROLL_PAGES: {
	int pixelsPerPage;

	pixelsPerPage = dInfoPtr->maxX - dInfoPtr->x - 2*textPtr->charWidth;
	dInfoPtr->newXPixelOffset += count*MAX(1, pixelsPerPage);
	break;
    }
    case SCROLL_UNITS:
	dInfoPtr->newXPixelOffset += count*textPtr->charWidth;
	break;
    case SCROLL_PIXELS:
	dInfoPtr->newXPixelOffset += count;
	break;
    }

    dInfoPtr->flags |= DINFO_OUT_OF_DATE;
    DisplayTextWhenIdle(textPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * YScrollByPixels --
 *
 *	This function is called to scroll a text widget up or down by a given
 *	number of pixels.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The view in textPtr's window changes to reflect the value of "offset".
 *
 *----------------------------------------------------------------------
 */

static void
YScrollByPixels(
    TkText *textPtr,	/* Widget to scroll. */
    int offset)		/* Amount by which to scroll, in pixels. Positive means that
    			 * information later in text becomes visible, negative means
			 * that information earlier in the text becomes visible. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    if (offset < 0) {
	/*
	 * Now we want to measure up this number of pixels from the top of the
	 * screen. But the top line may not be totally visible.
	 */

	offset -= CalculateDisplayLineHeight(textPtr, &textPtr->topIndex, NULL);
	offset += dInfoPtr->topPixelOffset;

	if (!MeasureUp(textPtr, &textPtr->topIndex, -offset,
		&textPtr->topIndex, &dInfoPtr->newTopPixelOffset)) {
	    return; /* already at top, we cannot scroll */
	}
    } else if (offset > 0) {
	/*
	 * Scrolling down by pixels. Layout lines starting at the top index
	 * and count through the desired vertical distance.
	 */

	offset += dInfoPtr->topPixelOffset;
	if (!MeasureDown(textPtr, &textPtr->topIndex, offset, &dInfoPtr->newTopPixelOffset, true)) {
	    return; /* already at end, we cannot scroll */
	}
	TkTextIndexToByteIndex(&textPtr->topIndex);
    } else {
	/*
	 * offset = 0, so no scrolling required.
	 */
	return;
    }

    assert(dInfoPtr->newTopPixelOffset < CalculateDisplayLineHeight(textPtr, &textPtr->topIndex, NULL));

    DisplayTextWhenIdle(textPtr);
    dInfoPtr->flags |= DINFO_OUT_OF_DATE|REPICK_NEEDED;
}

/*
 *----------------------------------------------------------------------
 *
 * YScrollByLines --
 *
 *	This function is called to scroll a text widget up or down by a given
 *	number of lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The view in textPtr's window changes to reflect the value of "offset".
 *
 *----------------------------------------------------------------------
 */

static bool
ScrollUp(
    TkText *textPtr,	/* Widget to scroll. */
    unsigned offset)	/* Amount by which to scroll, in display lines. */
{
    TkTextLine *linePtr;
    unsigned byteOffset;
    DisplayInfo info;
    bool upToDate;

    assert(offset > 0);

    /*
     * Scrolling up, to show earlier information in the text.
     */

    if (AlreadyAtTop(textPtr)) {
	return false;
    }

    if (TkTextIndexIsStartOfText(&textPtr->dInfoPtr->dLinePtr->index)) {
	textPtr->dInfoPtr->newTopPixelOffset = 0;
	return true;
    }

    upToDate = TestIfLinesUpToDate(&textPtr->topIndex);
    linePtr = ComputeDisplayLineInfo(textPtr, &textPtr->topIndex, &info);

    if (upToDate) {
	const TkTextDispLineInfo *dispLineInfo;

	assert(!info.dLinePtr);

	/*
	 * The display line information is complete for the required range, so
	 * use it for finding the requested display line.
	 */

	linePtr = TkBTreePrevDisplayLine(textPtr, linePtr, &info.displayLineNo, offset);
	dispLineInfo = TkBTreeLinePixelInfo(textPtr, linePtr)->dispLineInfo;
	byteOffset = dispLineInfo ? dispLineInfo->entry[info.displayLineNo].byteOffset : 0;
    } else {
	TkTextLine *firstLinePtr;
	TkTextIndex index;

	/*
	 * The display line information is incomplete, so we do a search line by line.
	 * The computed display lines will be saved for displaying.
	 */

	firstLinePtr = TkBTreeGetStartLine(textPtr);
	index = textPtr->topIndex;
	SaveDisplayLines(textPtr, &info, false);
	info.numDispLines = info.displayLineNo + 1;

	while (true) {
	    if (info.numDispLines > offset) {
		byteOffset = (info.entry - offset)->byteOffset;
		break;
	    }
	    offset -= info.numDispLines;
	    if (linePtr == firstLinePtr) {
		byteOffset = 0;
		break;
	    }
	    TkTextIndexSetToLastChar2(&index, linePtr->prevPtr);
	    linePtr = ComputeDisplayLineInfo(textPtr, &index, &info);
	    SaveDisplayLines(textPtr, &info, false);
	    assert(!TkBTreeLinePixelInfo(textPtr, linePtr)->dispLineInfo
		    || info.entry == TkBTreeLinePixelInfo(textPtr, linePtr)->dispLineInfo->entry +
			    info.numDispLines - 1);
	}
    }

    TkTextIndexSetToStartOfLine2(&textPtr->topIndex, linePtr);
    TkTextIndexForwBytes(textPtr, &textPtr->topIndex, byteOffset, &textPtr->topIndex);

    return true;
}

static bool
ScrollDown(
    TkText *textPtr,	/* Widget to scroll. */
    unsigned offset)	/* Amount by which to scroll, in display lines. */
{
    TkTextLine *linePtr;
    unsigned byteOffset;
    DisplayInfo info;
    bool upToDate;

    assert(offset > 0);

    /*
     * Scrolling down, to show later information in the text.
     */

    if (AlreadyAtBottom(textPtr)) {
	return false;
    }

    upToDate = TkRangeListIsEmpty(textPtr->dInfoPtr->lineMetricUpdateRanges);
    linePtr = ComputeDisplayLineInfo(textPtr, &textPtr->topIndex, &info);

    if (upToDate) {
	const TkTextDispLineInfo *dispLineInfo;

	assert(!info.dLinePtr);

	/*
	 * The display line information is complete for the required range, so
	 * use it for finding the requested display line.
	 */

	linePtr = TkBTreeNextDisplayLine(textPtr, linePtr, &info.displayLineNo, offset);
	dispLineInfo = TkBTreeLinePixelInfo(textPtr, linePtr)->dispLineInfo;
	byteOffset = dispLineInfo ? dispLineInfo->entry[info.displayLineNo].byteOffset : 0;
    } else {
	TkTextLine *lastLinePtr;

	/*
	 * The display line information is incomplete, so we do a search line by line.
	 * The computed display lines will be saved for displaying.
	 */

	lastLinePtr = TkBTreeGetLastLine(textPtr);
	ComputeMissingMetric(textPtr, &info, THRESHOLD_LINE_OFFSET, offset);
	SaveDisplayLines(textPtr, &info, true);
	info.numDispLines -= info.displayLineNo;

	while (true) {
	    if (info.numDispLines == offset) {
		byteOffset = 0;
		linePtr = linePtr->nextPtr;
		break;
	    }
	    if (info.numDispLines > offset) {
		byteOffset = (info.entry + offset)->byteOffset;
		break;
	    }
	    offset -= info.numDispLines;
	    if (TkTextIndexGetLine(&info.index) == lastLinePtr) {
		byteOffset = (info.entry + info.numDispLines - 1)->byteOffset;
		break;
	    }
	    linePtr = ComputeDisplayLineInfo(textPtr, &info.index, &info);
	    ComputeMissingMetric(textPtr, &info, THRESHOLD_LINE_OFFSET, offset);
	    SaveDisplayLines(textPtr, &info, true);
	}
    }

    TkTextIndexSetToStartOfLine2(&textPtr->topIndex, linePtr);
    TkTextIndexForwBytes(textPtr, &textPtr->topIndex, byteOffset, &textPtr->topIndex);
    return true;
}

static void
YScrollByLines(
    TkText *textPtr,	/* Widget to scroll. */
    int offset)		/* Amount by which to scroll, in display lines. Positive means
    			 * that information later in text becomes visible, negative
			 * means that information earlier in the text becomes visible. */
{
    assert(textPtr);

    if (offset < 0) {
	if (!ScrollUp(textPtr, -offset)) {
	    return;
	}
    } else if (offset > 0) {
	if (!ScrollDown(textPtr, offset)) {
	    return;
	}
    } else {
	return;
    }

    DisplayTextWhenIdle(textPtr);
    textPtr->dInfoPtr->flags |= DINFO_OUT_OF_DATE|REPICK_NEEDED;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextYviewCmd --
 *
 *	This function is invoked to process the "yview" option for the widget
 *	command for text widgets. See the user documentation for details on
 *	what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
MakePixelIndex(
    TkText *textPtr,		/* The text widget. */
    unsigned pixelIndex,	/* Pixel-index of desired line (0 means first pixel of first
    				 * line of text). */
    TkTextIndex *indexPtr)	/* Structure to fill in. */
{
    TkTextLine *linePtr;
    TkTextLine *lastLinePtr;
    int32_t pixelOffset;

    assert(!TkTextIsDeadPeer(textPtr));

    TkTextIndexClear(indexPtr, textPtr);
    linePtr = TkBTreeFindPixelLine(textPtr->sharedTextPtr->tree, textPtr, pixelIndex, &pixelOffset);
    lastLinePtr = TkBTreeGetLastLine(textPtr);

    if (linePtr != lastLinePtr) {
	int byteOffset = FindDisplayLineOffset(textPtr, linePtr, &pixelOffset);
	TkTextIndexSetByteIndex2(indexPtr, linePtr, byteOffset);
    } else {
	assert(lastLinePtr->prevPtr); /* MakePixelIndex will not be called if peer is empty */
	linePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr->prevPtr);
	TkTextIndexSetToLastChar2(indexPtr, linePtr);
	FindDisplayLineStartEnd(textPtr, indexPtr, DISP_LINE_START, DLINE_CACHE);
	pixelOffset = CalculateDisplayLineHeight(textPtr, indexPtr, NULL) - 1;
    }

    return MAX(0, pixelOffset);
}

static void
Repick(
    ClientData clientData)	/* Information about widget. */
{
    TkText *textPtr = (TkText *) clientData;

    if (!TkTextDecrRefCountAndTestIfDestroyed(textPtr)) {
	textPtr->dInfoPtr->flags &= ~REPICK_NEEDED;
	textPtr->dInfoPtr->currChunkPtr = NULL;
	textPtr->dInfoPtr->repickTimer = NULL;
	textPtr->dontRepick = false;
	TkTextPickCurrent(textPtr, &textPtr->pickEvent);
    }
}

static void
DelayRepick(
    TkText *textPtr)
{
    assert(textPtr->dInfoPtr->flags & REPICK_NEEDED);

    if (textPtr->responsiveness > 0) {
	TextDInfo *dInfoPtr = textPtr->dInfoPtr;

	if (dInfoPtr->repickTimer) {
	    Tcl_DeleteTimerHandler(dInfoPtr->repickTimer);
	} else {
	    textPtr->refCount += 1;
	}
	textPtr->dontRepick = true;
	dInfoPtr->flags &= ~REPICK_NEEDED;
	dInfoPtr->repickTimer = Tcl_CreateTimerHandler(textPtr->responsiveness, Repick, textPtr);
    }
}

int
TkTextYviewCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "yview". */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    int pickPlace;
    int pixels, count;
    int switchLength;
    double fraction;
    TkTextIndex index;

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    if (objc == 2) {
	GetYView(interp, textPtr, false);
	return TCL_OK;
    }

    /*
     * Next, handle the old syntax: "pathName yview ?-pickplace? where"
     */

    pickPlace = 0;
    if (Tcl_GetString(objv[2])[0] == '-') {
	const char *switchStr = Tcl_GetStringFromObj(objv[2], &switchLength);

	if (switchLength >= 2 && strncmp(switchStr, "-pickplace", switchLength) == 0) {
	    pickPlace = 1;
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "lineNum|index");
		return TCL_ERROR;
	    }
	}
    }

    if (objc == 3 || pickPlace) {
	int lineNum;

	if (Tcl_GetIntFromObj(interp, objv[2 + pickPlace], &lineNum) == TCL_OK) {
	    TkTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineNum, 0, &index);
	    TkTextSetYView(textPtr, &index, 0);
	} else {
	    /*
	     * The argument must be a regular text index.
	     */

	    Tcl_ResetResult(interp);
	    if (!TkTextGetIndexFromObj(interp, textPtr, objv[2 + pickPlace], &index)) {
		return TCL_ERROR;
	    }
	    TkTextSetYView(textPtr, &index, pickPlace ? TK_TEXT_PICKPLACE : 0);
	}
    } else {
	/*
	 * New syntax: dispatch based on objv[2].
	 */

	switch (TextGetScrollInfoObj(interp, textPtr, objc, objv, &fraction, &count)) {
	case SCROLL_ERROR:
	    return TCL_ERROR;
	case SCROLL_MOVETO: {
	    int numPixels = TkBTreeNumPixels(textPtr);
	    int topMostPixel;

	    if (numPixels == 0 || TkTextIsDeadPeer(textPtr)) {
		/*
		 * If the window is totally empty no scrolling is needed, and the
		 * MakePixelIndex call below will fail.
		 */
		break;
	    }
	    if (fraction > 1.0) {
		fraction = 1.0;
	    } else if (fraction < 0.0) {
		fraction = 0.0;
	    }

	    /*
	     * Calculate the pixel count for the new topmost pixel in the topmost
	     * line of the window. Note that the interpretation of 'fraction' is
	     * that it counts from 0 (top pixel in buffer) to 1.0 (one pixel past
	     * the last pixel in buffer).
	     */

	    topMostPixel = MAX(0, MIN((int) (fraction*numPixels + 0.5), numPixels - 1));

	    /*
	     * This function returns the number of pixels by which the given line
	     * should overlap the top of the visible screen.
	     *
	     * This is then used to provide smooth scrolling.
	     */

	    pixels = MakePixelIndex(textPtr, topMostPixel, &index);
	    TkTextSetYView(textPtr, &index, pixels);
	    break;
	}
	case SCROLL_PAGES: {
	    /*
	     * Scroll up or down by screenfuls. Actually, use the window height
	     * minus two lines, so that there's some overlap between adjacent
	     * pages.
	     */

	    int height = dInfoPtr->maxY - dInfoPtr->y;

	    if (textPtr->lineHeight*4 >= height) {
		/*
		 * A single line is more than a quarter of the display. We choose
		 * to scroll by 3/4 of the height instead.
		 */

		pixels = 3*height/4;
		if (pixels < textPtr->lineHeight) {
		    /*
		     * But, if 3/4 of the height is actually less than a single
		     * typical character height, then scroll by the minimum of the
		     * linespace or the total height.
		     */

		    if (textPtr->lineHeight < height) {
			pixels = textPtr->lineHeight;
		    } else {
			pixels = height;
		    }
		}
		pixels *= count;
	    } else {
		pixels = (height - 2*textPtr->lineHeight)*count;
	    }
	    YScrollByPixels(textPtr, pixels);
	    break;
	}
	case SCROLL_PIXELS:
	    YScrollByPixels(textPtr, count);
	    break;
	case SCROLL_UNITS:
	    YScrollByLines(textPtr, count);
	    break;
	}
    }

    if (dInfoPtr->flags & REPICK_NEEDED) {
	DelayRepick(textPtr);
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextScanCmd --
 *
 *	This function is invoked to process the "scan" option for the widget
 *	command for text widgets. See the user documentation for details on
 *	what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
TkTextScanCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "scan". */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextIndex index;
    int c, x, y, totalScroll, gain=10;
    size_t length;

    if (objc != 5 && objc != 6) {
	Tcl_WrongNumArgs(interp, 2, objv, "mark x y");
	Tcl_AppendResult(interp, " or \"", Tcl_GetString(objv[0]), " scan dragto x y ?gain?\"", NULL);
	/*
	 * Ought to be: Tcl_WrongNumArgs(interp, 2, objc, "dragto x y ?gain?");
	 */
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 6 && Tcl_GetIntFromObj(interp, objv[5], &gain) != TCL_OK) {
	return TCL_ERROR;
    }
    c = Tcl_GetString(objv[2])[0];
    length = strlen(Tcl_GetString(objv[2]));
    if (c == 'd' && strncmp(Tcl_GetString(objv[2]), "dragto", length) == 0) {
	int newX, maxX;

	/*
	 * Amplify the difference between the current position and the mark
	 * position to compute how much the view should shift, then update the
	 * mark position to correspond to the new view. If we run off the edge
	 * of the text, reset the mark point so that the current position
	 * continues to correspond to the edge of the window. This means that
	 * the picture will start dragging as soon as the mouse reverses
	 * direction (without this reset, might have to slide mouse a long
	 * ways back before the picture starts moving again).
	 */

	newX = dInfoPtr->scanMarkXPixel + gain*(dInfoPtr->scanMarkX - x);
	maxX = 1 + dInfoPtr->maxLength - (dInfoPtr->maxX - dInfoPtr->x);
	if (newX < 0) {
	    newX = 0;
	    dInfoPtr->scanMarkXPixel = 0;
	    dInfoPtr->scanMarkX = x;
	} else if (newX > maxX) {
	    newX = maxX;
	    dInfoPtr->scanMarkXPixel = maxX;
	    dInfoPtr->scanMarkX = x;
	}
	dInfoPtr->newXPixelOffset = newX;

	totalScroll = gain*(dInfoPtr->scanMarkY - y);
	if (totalScroll != dInfoPtr->scanTotalYScroll) {
	    index = textPtr->topIndex;
	    YScrollByPixels(textPtr, totalScroll - dInfoPtr->scanTotalYScroll);
	    dInfoPtr->scanTotalYScroll = totalScroll;
	    if (TkTextIndexIsEqual(&index, &textPtr->topIndex)) {
		dInfoPtr->scanTotalYScroll = 0;
		dInfoPtr->scanMarkY = y;
	    }
	}
	dInfoPtr->flags |= DINFO_OUT_OF_DATE;
	DisplayTextWhenIdle(textPtr);
    } else if (c == 'm' && strncmp(Tcl_GetString(objv[2]), "mark", length) == 0) {
	dInfoPtr->scanMarkXPixel = dInfoPtr->newXPixelOffset;
	dInfoPtr->scanMarkX = x;
	dInfoPtr->scanTotalYScroll = 0;
	dInfoPtr->scanMarkY = y;
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad scan option \"%s\": must be mark or dragto", Tcl_GetString(objv[2])));
	Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "scan option", Tcl_GetString(objv[2]), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetXView --
 *
 *	This function computes the fractions that indicate what's visible in a
 *	text window and, optionally, evaluates a Tcl script to report them to
 *	the text's associated scrollbar.
 *
 * Results:
 *	If report is zero, then the interp's result is filled in with two real
 *	numbers separated by a space, giving the position of the left and
 *	right edges of the window as fractions from 0 to 1, where 0 means the
 *	left edge of the text and 1 means the right edge. If report is
 *	non-zero, then the interp's result isn't modified directly, but
 *	instead a script is evaluated in interp to report the new horizontal
 *	scroll position to the scrollbar (if the scroll position hasn't
 *	changed then no script is invoked).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetXView(
    Tcl_Interp *interp,		/* If "report" is FALSE, string describing visible range gets stored
    				 * in the interp's result. */
    TkText *textPtr,		/* Information about text widget. */
    bool report)		/* 'true' means report info to scrollbar if it has changed. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    double first, last;
    int xMin, xMax;
    int code;
    Tcl_Obj *listObj;

    if (dInfoPtr->maxLength > 0) {
	first = ((double) dInfoPtr->curXPixelOffset)/dInfoPtr->maxLength;
	last = ((double) (dInfoPtr->curXPixelOffset + dInfoPtr->maxX - dInfoPtr->x))/dInfoPtr->maxLength;
	if (last > 1.0) {
	    last = 1.0;
	}
	xMin = dInfoPtr->curXPixelOffset;
	xMax = xMin + dInfoPtr->maxX - dInfoPtr->x;
    } else {
	first = 0.0;
	last = 1.0;
	xMin = xMax = dInfoPtr->curXPixelOffset;
    }
    if (!report) {
	listObj = Tcl_NewObj();
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(first));
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(last));
	Tcl_SetObjResult(interp, listObj);
	return;
    }
    if (FP_EQUAL_SCALE(first, dInfoPtr->xScrollFirst, dInfoPtr->maxLength) &&
	    FP_EQUAL_SCALE(last, dInfoPtr->xScrollLast, dInfoPtr->maxLength)) {
	return;
    }

    dInfoPtr->xScrollFirst = first;
    dInfoPtr->xScrollLast = last;
    dInfoPtr->curPixelPos.xFirst = xMin;
    dInfoPtr->curPixelPos.xLast = xMax;

    if (textPtr->xScrollCmd) {
	char buf1[TCL_DOUBLE_SPACE + 1];
	char buf2[TCL_DOUBLE_SPACE + 1];
	Tcl_DString buf;

	buf1[0] = ' ';
	buf2[0] = ' ';
	Tcl_PrintDouble(NULL, first, buf1 + 1);
	Tcl_PrintDouble(NULL, last, buf2 + 1);
	Tcl_DStringInit(&buf);
	Tcl_DStringAppend(&buf, textPtr->xScrollCmd, -1);
	Tcl_DStringAppend(&buf, buf1, -1);
	Tcl_DStringAppend(&buf, buf2, -1);
	code = Tcl_EvalEx(interp, Tcl_DStringValue(&buf), -1, 0);
	Tcl_DStringFree(&buf);
	if (code != TCL_OK) {
	    Tcl_AddErrorInfo(interp,
		    "\n    (horizontal scrolling command executed by text)");
	    Tcl_BackgroundException(interp, code);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetYPixelCount --
 *
 *	How many pixels are there between the absolute top of the widget and
 *	the top of the given DLine.
 *
 *	While this function will work for any valid DLine, it is only ever
 *	called when dlPtr is the first display line in the widget (by
 *	'GetYView'). This means that usually this function is a very quick
 *	calculation, since it can use the pre-calculated linked-list of DLines
 *	for height information.
 *
 *	The only situation where this breaks down is if dlPtr's logical line
 *	wraps enough times to fill the text widget's current view - in this
 *	case we won't have enough dlPtrs in the linked list to be able to
 *	subtract off what we want.
 *
 * Results:
 *	The number of pixels.
 *
 *	This value has a valid range between '0' (the very top of the widget)
 *	and the number of pixels in the total widget minus the pixel-height of
 *	the last line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned
GetYPixelCount(
    TkText *textPtr,	/* Information about text widget. */
    DLine *dlPtr)	/* Information about the layout of a given index. */
{
    TkTextLine *linePtr;
    DisplayInfo info;

    linePtr = ComputeDisplayLineInfo(textPtr, &dlPtr->index, &info);
    FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
    return TkBTreePixelsTo(textPtr, linePtr) + info.entry->pixels - info.entry->height;
}

/*
 *----------------------------------------------------------------------
 *
 * GetYView --
 *
 *	This function computes the fractions that indicate what's visible in a
 *	text window and, optionally, evaluates a Tcl script to report them to
 *	the text's associated scrollbar.
 *
 * Results:
 *	If report is zero, then the interp's result is filled in with two real
 *	numbers separated by a space, giving the position of the top and
 *	bottom of the window as fractions from 0 to 1, where 0 means the
 *	beginning of the text and 1 means the end. If report is non-zero, then
 *	the interp's result isn't modified directly, but a script is evaluated
 *	in interp to report the new scroll position to the scrollbar (if the
 *	scroll position hasn't changed then no script is invoked).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetYView(
    Tcl_Interp *interp,		/* If "report" is 'false', string describing visible range gets
    				 * stored in the interp's result. */
    TkText *textPtr,		/* Information about text widget. */
    bool report)		/* 'true' means report info to scrollbar if it has changed. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    double first, last;
    DLine *dlPtr;
    int totalPixels, code, count;
    int yMin, yMax;
    Tcl_Obj *listObj;

    dlPtr = dInfoPtr->dLinePtr;

    if (!dlPtr) {
	return;
    }

    totalPixels = TkBTreeNumPixels(textPtr);

    if (totalPixels == 0) {
	first = 0.0;
	last = 1.0;
	yMin = yMax = dInfoPtr->topPixelOffset;
    } else {
	/*
	 * Get the pixel count for the first visible pixel of the first
	 * visible line. If the first visible line is only partially visible,
	 * then we use 'topPixelOffset' to get the difference.
	 */

	count = yMin = GetYPixelCount(textPtr, dlPtr);
	first = (count + dInfoPtr->topPixelOffset) / (double) totalPixels;

	/*
	 * Add on the total number of visible pixels to get the count to one
	 * pixel _past_ the last visible pixel. This is how the 'yview'
	 * command is documented, and also explains why we are dividing by
	 * 'totalPixels' and not 'totalPixels-1'.
	 */

	while (dlPtr) {
	    int extra;

	    count += dlPtr->height;
	    extra = dlPtr->y + dlPtr->height - dInfoPtr->maxY;
	    if (extra > 0) {
		/*
		 * This much of the last line is not visible, so don't count
		 * these pixels. Since we've reached the bottom of the window,
		 * we break out of the loop.
		 */

		count -= extra;
		break;
	    }
	    dlPtr = dlPtr->nextPtr;
	}

	if (count > totalPixels) {
	    /*
	     * It can be possible, if we do not update each line's pixelHeight
	     * cache when we lay out individual DLines that the count
	     * generated here is more up-to-date than that maintained by the
	     * BTree. In such a case, the best we can do here is to fix up
	     * 'count' and continue, which might result in small, temporary
	     * perturbations to the size of the scrollbar. This is basically
	     * harmless, but in a perfect world we would not have this
	     * problem.
	     *
	     * For debugging purposes, if anyone wishes to improve the text
	     * widget further, the following 'panic' can be activated. In
	     * principle it should be possible to ensure the BTree is always
	     * at least as up to date as the display, so in the future we
	     * might be able to leave the 'panic' in permanently when we
	     * believe we have resolved the cache synchronisation issue.
	     *
	     * However, to achieve that goal would, I think, require a fairly
	     * substantial refactorisation of the code in this file so that
	     * there is much more obvious and explicit coordination between
	     * calls to LayoutDLine and updating of each TkTextLine's
	     * pixelHeight. The complicated bit is that LayoutDLine deals with
	     * individual display lines, but pixelHeight is for a logical
	     * line.
	     */

#if 0
	    Tcl_Panic("Counted more pixels (%d) than expected (%d) total "
		    "pixels in text widget scroll bar calculation.", count,
		    totalPixels);
#elif 0 /* TODO: still happens sometimes, why? */
	    fprintf(stderr, "warning: Counted more pixels (%d) than expected (%d)\n",
		    count, totalPixels);
#endif

	    count = totalPixels;
	}

	yMax = count;
	last = ((double) count)/((double) totalPixels);
    }

    if (!report) {
	listObj = Tcl_NewObj();
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(first));
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(last));
	Tcl_SetObjResult(interp, listObj);
    } else {
	dInfoPtr->curPixelPos.yFirst = yMin + dInfoPtr->topPixelOffset;
	dInfoPtr->curPixelPos.yLast = yMax + dInfoPtr->topPixelOffset;

	if (!FP_EQUAL_SCALE(first, dInfoPtr->yScrollFirst, totalPixels) ||
		!FP_EQUAL_SCALE(last, dInfoPtr->yScrollLast, totalPixels)) {
	    dInfoPtr->yScrollFirst = first;
	    dInfoPtr->yScrollLast = last;

	    if (textPtr->yScrollCmd) {
		char buf1[TCL_DOUBLE_SPACE + 1];
		char buf2[TCL_DOUBLE_SPACE + 1];
		Tcl_DString buf;

		buf1[0] = ' ';
		buf2[0] = ' ';
		Tcl_PrintDouble(NULL, first, buf1 + 1);
		Tcl_PrintDouble(NULL, last, buf2 + 1);
		Tcl_DStringInit(&buf);
		Tcl_DStringAppend(&buf, textPtr->yScrollCmd, -1);
		Tcl_DStringAppend(&buf, buf1, -1);
		Tcl_DStringAppend(&buf, buf2, -1);
		code = Tcl_EvalEx(interp, Tcl_DStringValue(&buf), -1, 0);
		Tcl_DStringFree(&buf);
		if (code != TCL_OK) {
		    Tcl_AddErrorInfo(interp,
			    "\n    (vertical scrolling command executed by text)");
		    Tcl_BackgroundException(interp, code);
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AsyncUpdateYScrollbar --
 *
 *	This function is called to update the vertical scrollbar asychronously
 *	as the pixel height calculations progress for lines in the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See 'GetYView'. In particular the scrollbar position and size may be
 *	changed.
 *
 *----------------------------------------------------------------------
 */

static void
AsyncUpdateYScrollbar(
    ClientData clientData)	/* Information about widget. */
{
    TkText *textPtr = clientData;
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;

    dInfoPtr->scrollbarTimer = NULL;

    if (!TkTextDecrRefCountAndTestIfDestroyed(textPtr) && !dInfoPtr->insideLineMetricUpdate) {
	GetYView(textPtr->interp, textPtr, true);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FindCachedDLine --
 *
 *	This function is called to find the cached line for given text
 *	index.
 *
 * Results:
 *	The return value is a pointer to the cached DLine found, or NULL
 *	if not available.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static DLine *
FindCachedDLine(
    TkText *textPtr,
    const TkTextIndex *indexPtr)
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr;

    for (dlPtr = dInfoPtr->cachedDLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	if (TkBTreeLinePixelInfo(textPtr, TkTextIndexGetLine(&dlPtr->index))->epoch
		    == dInfoPtr->lineMetricUpdateEpoch
		&& TkTextIndexCompare(indexPtr, &dlPtr->index) >= 0) {
	    TkTextIndex index = dlPtr->index;

	    TkTextIndexForwBytes(textPtr, &index, dlPtr->byteCount, &index);
	    if (TkTextIndexCompare(indexPtr, &index) < 0) {
		DEBUG(stats.numHits++);
		return dlPtr;
	    }
	}
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindDLine --
 *
 *	This function is called to find the DLine corresponding to a given
 *	text index.
 *
 * Results:
 *	The return value is a pointer to the first DLine found in the list
 *	headed by dlPtr that displays information at or after the specified
 *	position. If there is no such line in the list then NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static DLine *
FindDLine(
    TkText *textPtr,		/* Widget record for text widget. */
    DLine *dlPtr,		/* Pointer to first in list of DLines to search. */
    const TkTextIndex *indexPtr)/* Index of desired character. */
{
    DLine *lastDlPtr;

    if (!dlPtr) {
	return NULL;
    }

    if (TkTextIndexGetLineNumber(indexPtr, NULL) < TkTextIndexGetLineNumber(&dlPtr->index, NULL)) {
	/*
	 * The first display line is already past the desired line.
	 */
	return dlPtr;
    }

    /*
     * The display line containing the desired index is such that the index
     * of the first character of this display line is at or before the
     * desired index, and the index of the first character of the next
     * display line is after the desired index.
     */

    while (TkTextIndexCompare(&dlPtr->index, indexPtr) < 0) {
        lastDlPtr = dlPtr;
        dlPtr = dlPtr->nextPtr;
        if (!dlPtr) {
            TkTextIndex index2;
            /*
             * We're past the last display line, either because the desired
             * index lies past the visible text, or because the desired index
             * is on the last display line showing the last logical line.
             */
            index2 = lastDlPtr->index;
            TkTextIndexForwBytes(textPtr, &index2, lastDlPtr->byteCount, &index2);
            if (TkTextIndexCompare(&index2, indexPtr) > 0) {
                /*
                 * The desired index is on the last display line, hence return this display line.
                 */
                dlPtr = lastDlPtr;
                break;
            } else {
                /*
                 * The desired index is past the visible text. There is no display line
		 * displaying something at the desired index, hence return NULL.
                 */
                return NULL;
            }
        }
        if (TkTextIndexCompare(&dlPtr->index, indexPtr) > 0) {
            /*
             * If we're here then we would normally expect that:
             *   lastDlPtr->index <= indexPtr < dlPtr->index
             * i.e. we have found the searched display line being dlPtr.
             * However it is possible that some DLines were unlinked
             * previously, leading to a situation where going through
             * the list of display lines skips display lines that did
             * exist just a moment ago.
             */

	    TkTextIndex index;
            TkTextIndexForwBytes(textPtr, &lastDlPtr->index, lastDlPtr->byteCount, &index);
            if (TkTextIndexCompare(&index, indexPtr) > 0) {
                /*
                 * Confirmed: lastDlPtr->index <= indexPtr < dlPtr->index
                 */
                dlPtr = lastDlPtr;
            } else {
                /*
                 * The last (rightmost) index shown by dlPtrPrev is still before the desired
		 * index. This may be because there was previously a display line between
		 * dlPtrPrev and dlPtr and this display line has been unlinked.
                 */
            }
            break;
        }
    }

    return dlPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetFirstXPixel --
 *
 *	Get first x-pixel position in current widget.
 *
 * Results:
 *	Returns first x-pixel.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetFirstXPixel(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->x;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetFirstYPixel --
 *
 *	Get first y-pixel position in current widget.
 *
 * Results:
 *	Returns first y-pixel.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetFirstYPixel(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->y;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetLastXPixel --
 *
 *	Get last x-pixel position in current widget.
 *
 * Results:
 *	Returns last x-pixel.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetLastXPixel(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->maxX - 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetLastYPixel --
 *
 *	Get last y-pixel position in current widget.
 *
 * Results:
 *	Returns last y-pixel.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextGetLastYPixel(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->maxY - 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCountVisibleImages --
 *
 *	Return the number of visible embedded images.
 *
 * Results:
 *	Returns number of visible embedded images.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkTextCountVisibleImages(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->countImages;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCountVisibleWindows --
 *
 *	Return the number of visible embedded windows.
 *
 * Results:
 *	Returns number of visible embedded windows.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkTextCountVisibleWindows(
    const TkText *textPtr)	/* Widget record for text widget. */
{
    assert(textPtr);
    return textPtr->dInfoPtr->countWindows;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPixelIndex --
 *
 *	Given an (x,y) coordinate on the screen, find the location of the
 *	character closest to that location.
 *
 * Results:
 *	The index at *indexPtr is modified to refer to the character on the
 *	display that is closest to (x,y). It returns the affected display
 *	chunk.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const TkTextDispChunk *
TkTextPixelIndex(
    TkText *textPtr,		/* Widget record for text widget. */
    int x, int y,		/* Pixel coordinates of point in widget's window. */
    TkTextIndex *indexPtr,	/* This index gets filled in with the index of the character
    				 * nearest to (x,y). */
    bool *nearest)		/* If non-NULL then gets set to false if (x,y) is actually over the
    				 * returned index, and true if it is just nearby (e.g. if x,y is on
				 * the border of the widget). */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr = NULL;
    DLine *currDLinePtr;
    TkTextDispChunk *currChunkPtr;
    bool nearby = false;
    unsigned epoch;

    /*
     * Make sure that all of the layout information about what's displayed
     * where on the screen is up-to-date.
     */

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    /*
     * If the coordinates are above the top of the window, then adjust them to
     * refer to the upper-right corner of the window. If they're off to one
     * side or the other, then adjust to the closest side.
     */

    if (y < dInfoPtr->y) {
	y = dInfoPtr->y;
	nearby = true;
    }
    if (x >= dInfoPtr->maxX) {
	x = dInfoPtr->maxX - 1;
	nearby = true;
    }
    if (x < dInfoPtr->x) {
	x = dInfoPtr->x;
	nearby = true;
    }

    /*
     * Find the display line containing the desired y-coordinate.
     */

    if (!dInfoPtr->dLinePtr) {
	if (nearest) {
	    *nearest = true;
	}
	*indexPtr = textPtr->topIndex;
	return NULL;
    }

    epoch = TkBTreeEpoch(textPtr->sharedTextPtr->tree);
    currChunkPtr = dInfoPtr->currChunkPtr;

    if (currChunkPtr && dInfoPtr->currChunkIndex.stateEpoch == epoch) {
	currDLinePtr = dInfoPtr->currDLinePtr;

	assert(currChunkPtr->stylePtr); /* otherwise the chunk has been expired */

	if (currDLinePtr->y <= y && y < currDLinePtr->y + currDLinePtr->height) {
	    int rx = x - dInfoPtr->x + dInfoPtr->curXPixelOffset;

	    if (currChunkPtr->x <= rx && rx < currChunkPtr->x + currChunkPtr->width) {
		/*
		 * We have luck, it's inside the cache.
		 */

		*indexPtr = dInfoPtr->currChunkIndex;
		DLineIndexOfX(textPtr, currChunkPtr, x, indexPtr);
		if (nearest) {
		    *nearest = nearby;
		}
		return currChunkPtr;
	    }

	    dlPtr = currDLinePtr;
	}
    }

    if (!dlPtr) {
	DLine *validDlPtr = dInfoPtr->dLinePtr;

	for (dlPtr = validDlPtr; y >= dlPtr->y + dlPtr->height; dlPtr = dlPtr->nextPtr) {
	    if (dlPtr->chunkPtr) {
		validDlPtr = dlPtr;
	    }
	    if (!dlPtr->nextPtr) {
		/*
		 * Y-coordinate is off the bottom of the displayed text. Use the
		 * last character on the last line.
		 */

		if (nearest) {
		    *nearest = true;
		}
		dInfoPtr->currChunkPtr = NULL;
		*indexPtr = dlPtr->index;
		assert(dlPtr->byteCount > 0);
		TkTextIndexForwBytes(textPtr, indexPtr, dlPtr->byteCount - 1, indexPtr);
		return NULL;
	    }
	}
	if (!dlPtr->chunkPtr) {
	    dlPtr = validDlPtr;
	}
    }

    currChunkPtr = DLineChunkOfX(textPtr, dlPtr, x, indexPtr, &nearby);

    if (nearest) {
	*nearest = nearby;
    }

    if (!nearby) {
	/*
	 * Cache the result.
	 */

	dInfoPtr->currChunkIndex = *indexPtr;
	TkTextIndexSetEpoch(&dInfoPtr->currChunkIndex, epoch); /* price it as actual */
	dInfoPtr->currChunkPtr = currChunkPtr;
	dInfoPtr->currDLinePtr = dlPtr;
    } else {
	dInfoPtr->currChunkPtr = NULL;
    }

    DLineIndexOfX(textPtr, currChunkPtr, x, indexPtr);
    return currChunkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * DLineIndexOfX --
 *
 *	Given an x coordinate in a display line, increase the byte position
 *	of the index according to the character closest to that location.
 *
 *	Together with DLineChunkOfX this is effectively the opposite of
 *	DLineXOfIndex.
 *
 *	Note: use DLineChunkOfX for the computation of the chunk.
 *
 * Results:
 *	The index at *indexPtr is modified to refer to the character on the
 *	display line that is closest to x.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DLineIndexOfX(
    TkText *textPtr,		/* Widget record for text widget. */
    TkTextDispChunk *chunkPtr,	/* Chunk which contains the character. */
    int x,			/* Pixel x coordinate of point in widget's window. */
    TkTextIndex *indexPtr)	/* This byte offset of this index will be increased according
    				 * to the character position. */
{
    /*
     * If the chunk has more than one byte in it, ask it which character is at
     * the desired location. In this case we can manipulate
     * 'indexPtr->byteIndex' directly, because we know we're staying inside a
     * single logical line.
     */

    if (chunkPtr && chunkPtr->numBytes > 1) {
	x -= textPtr->dInfoPtr->x - textPtr->dInfoPtr->curXPixelOffset;
	TkTextIndexAddToByteIndex(indexPtr, chunkPtr->layoutProcs->measureProc(chunkPtr, x));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DLineChunkOfX --
 *
 *	Given an x coordinate in a display line, find the index of the
 *	character closest to that location.
 *
 *	This is effectively the opposite of DLineXOfIndex.
 *
 * Results:
 *	The index at *indexPtr is modified to refer to the character on the
 *	display line that is closest to x.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextDispChunk *
DLineChunkOfX(
    TkText *textPtr,		/* Widget record for text widget. */
    DLine *dlPtr,		/* Display information for this display line. */
    int x,			/* Pixel x coordinate of point in widget's window. */
    TkTextIndex *indexPtr,	/* This index gets filled in with the index of the character
    				 * nearest to x. */
    bool *nearby)		/* If non-NULL then gets set to true if (x,y) is not actually over the
    				 * returned index, but never set to false. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    TkTextDispChunk *chunkPtr;
    TkTextDispChunkSection *sectionPtr;
    unsigned countBytes;

    /*
     * Scan through the line's chunks to find the one that contains the desired x-coordinate.
     * Before doing this, translate the x-coordinate from the coordinate system of the window
     * to the coordinate system of the line (to take account of x-scrolling).
     */

    chunkPtr = dlPtr->chunkPtr;
    *indexPtr = dlPtr->index;

    if (!chunkPtr) {
	/* this may happen if everything is elided */
	if (nearby) {
	    *nearby = true;
	}
	return chunkPtr;
    }

    x -= dInfoPtr->x - dInfoPtr->curXPixelOffset;

    if (x < chunkPtr->x) {
	if (chunkPtr->stylePtr->sValuePtr->indentBg) {
	    /* if -indentbackground is enabled, then do not trigger when hovering the margin */
	    *nearby = true;
	}
	return chunkPtr;
    }

    sectionPtr = chunkPtr->sectionPtr;
    countBytes = chunkPtr->byteOffset;

    while (sectionPtr->nextPtr && x >= sectionPtr->nextPtr->chunkPtr->x) {
	countBytes += sectionPtr->numBytes;
	sectionPtr = sectionPtr->nextPtr;
    }

    chunkPtr = sectionPtr->chunkPtr;

    while (chunkPtr->nextPtr && x >= chunkPtr->x + chunkPtr->width) {
	countBytes += chunkPtr->numBytes;
	chunkPtr = chunkPtr->nextPtr;
    }

    TkTextIndexForwBytes(textPtr, indexPtr, countBytes, indexPtr);
    return chunkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexOfX --
 *
 *	Given a logical x coordinate (i.e. distance in pixels from the
 *	beginning of the display line, not taking into account any information
 *	about the window, scrolling etc.) on the display line starting with
 *	the given index, adjust that index to refer to the object under the x
 *	coordinate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkTextIndexOfX(
    TkText *textPtr,		/* Widget record for text widget. */
    int x,			/* The x coordinate for which we want the index. */
    TkTextIndex *indexPtr)	/* Index of display line start, which will be adjusted to the
    				 * index under the given x coordinate. */
{
    TextDInfo *dInfoPtr;
    DLine *dlPtr;

    assert(textPtr);

    if (TkTextIndexGetLine(indexPtr) == TkBTreeGetLastLine(textPtr)) {
	return;
    }

    dInfoPtr = textPtr->dInfoPtr;
    dlPtr = FindCachedDLine(textPtr, indexPtr);

    if (!dlPtr
	    && !(dInfoPtr->flags & DINFO_OUT_OF_DATE)
	    && TkTextIndexCompare(indexPtr, &textPtr->topIndex) >= 0) {
	dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, indexPtr);
    }
    if (!dlPtr) {
	DisplayInfo info;

	ComputeDisplayLineInfo(textPtr, indexPtr, &info);
	if (!(dlPtr = info.lastDLinePtr)) {
	    TkTextIndex index = *indexPtr;

	    /* we need display line start */
	    TkTextIndexBackBytes(textPtr, &index, info.byteOffset, &index);
	    dlPtr = LayoutDLine(&index, info.displayLineNo);
	} else if ((info.lastDLinePtr = info.lastDLinePtr->prevPtr)) {
	    dlPtr->prevPtr = info.lastDLinePtr->nextPtr = NULL;
	} else {
	    info.dLinePtr = NULL;
	}
	FreeDLines(textPtr, dlPtr, NULL, DLINE_CACHE);
	FreeDLines(textPtr, info.dLinePtr, NULL, DLINE_FREE_TEMP);
    }
    x += dInfoPtr->x - dInfoPtr->curXPixelOffset;
    DLineIndexOfX(textPtr, DLineChunkOfX(textPtr, dlPtr, x, indexPtr, NULL), x, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DLineXOfIndex --
 *
 *	Given a relative byte index on a given display line (i.e. the number
 *	of byte indices from the beginning of the given display line), find
 *	the x coordinate of that index within the abstract display line,
 *	without adjusting for the x-scroll state of the line.
 *
 *	This is effectively the opposite of DLineIndexOfX.
 *
 *	NB. The 'byteIndex' is relative to the display line, NOT the logical
 *	line.
 *
 * Results:
 *	The x coordinate.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
DLineXOfIndex(
    TkText *textPtr,		/* Widget record for text widget. */
    DLine *dlPtr,		/* Display information for this display line. */
    unsigned byteIndex)		/* The byte index for which we want the coordinate. */
{
    TkTextDispChunkSection *sectionPtr, *nextPtr;
    TkTextDispChunk *chunkPtr;
    int x;

    if (byteIndex == 0 || !(sectionPtr = dlPtr->chunkPtr->sectionPtr)) {
	return 0;
    }

    while (byteIndex >= sectionPtr->numBytes && (nextPtr = sectionPtr->nextPtr)) {
	byteIndex -= sectionPtr->numBytes;
	sectionPtr = nextPtr;
    }

    chunkPtr = sectionPtr->chunkPtr;
    assert(chunkPtr);

    /*
     * Scan through the line's chunks to find the one that contains the desired byte index.
     */

    x = 0;

    while (true) {
	if (byteIndex < chunkPtr->numBytes) {
	    int unused;

	    x = chunkPtr->x;
	    chunkPtr->layoutProcs->bboxProc(textPtr, chunkPtr, byteIndex,
		    dlPtr->y + dlPtr->spaceAbove,
		    dlPtr->height - dlPtr->spaceAbove - dlPtr->spaceBelow,
		    dlPtr->baseline - dlPtr->spaceAbove, &x, &unused, &unused,
		    &unused);
	    break;
	}
	if (!chunkPtr->nextPtr || byteIndex == chunkPtr->numBytes) {
	    x = chunkPtr->x + chunkPtr->width;
	    break;
	}
	byteIndex -= chunkPtr->numBytes;
	chunkPtr = chunkPtr->nextPtr;
    }

    return x;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexBbox --
 *
 *	Given an index, find the bounding box of the screen area occupied by
 *	the entity (character, window, image) at that index.
 *
 * Results:
 *	'True' is returned if the index is on the screen. 'False' means the index
 *	is not on the screen. If the return value is 'true', then the bounding box
 *	of the part of the index that's visible on the screen is returned to
 *	*xPtr, *yPtr, *widthPtr, and *heightPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexBbox(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *indexPtr,/* Index whose bounding box is desired. */
    bool extents,		/* Return the extents of the bbox (the overflow, not visible on
    				 * screen). */
    int *xPtr, int *yPtr,	/* Filled with index's upper-left coordinate. */
    int *widthPtr, int *heightPtr,
				/* Filled in with index's dimensions. */
    int *charWidthPtr,		/* If the 'index' is at the end of a display line and therefore
    				 * takes up a very large width, this is used to return the smaller
				 * width actually desired by the index. */
    Tcl_UniChar *thisChar)	/* Character at given position, can be NULL. Zero will be returned
    				 * if this is not a char chunk, or if outside of screen. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    bool isLastCharInLine;
    DLine *dlPtr;

    /*
     * Make sure that all of the screen layout information is up to date.
     */

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    /*
     * Find the display line containing the desired index.
     */

    dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, indexPtr);

    /*
     * Two cases shall be trapped here because the logic later really
     * needs dlPtr to be the display line containing indexPtr:
     *   1. if no display line contains the desired index (NULL dlPtr or no chunk)
     *   2. if indexPtr is before the first display line, in which case
     *      dlPtr currently points to the first display line
     */

    if (!dlPtr || !dlPtr->chunkPtr || TkTextIndexCompare(&dlPtr->index, indexPtr) > 0) {
	if (thisChar) { *thisChar = 0; }
	return false;
    }

    if (!GetBbox(textPtr, dlPtr, indexPtr, xPtr, yPtr, widthPtr, heightPtr,
	    &isLastCharInLine, thisChar)) {
	return false;
    }

    *xPtr -= dInfoPtr->curXPixelOffset;

    if (extents) {
	*widthPtr = MAX(0, *xPtr + *widthPtr - dInfoPtr->maxX);
	*heightPtr = MAX(0, *yPtr + *heightPtr - dInfoPtr->maxY);
	*xPtr = MAX(0, -*xPtr);
	*yPtr = MAX(0, -*yPtr);
    } else {
	*xPtr = *xPtr + dInfoPtr->x;

	if (isLastCharInLine) {
	    /*
	     * Last character in display line. Give it all the space up to the line.
	     */

	    if (charWidthPtr) {
		*charWidthPtr = dInfoPtr->maxX - *xPtr;
		if (*charWidthPtr > textPtr->charWidth) {
		    *charWidthPtr = textPtr->charWidth;
		}
	    }
	    if (*xPtr > dInfoPtr->maxX) {
		*xPtr = dInfoPtr->maxX;
	    }
	    *widthPtr = dInfoPtr->maxX - *xPtr;
	} else {
	    if (charWidthPtr) {
		*charWidthPtr = *widthPtr;
	    }
	}

	if (*widthPtr == 0) {
	    /*
	     * With zero width (e.g. elided text) we just need to make sure it is
	     * onscreen, where the '=' case here is ok.
	     */

	    if (*xPtr < dInfoPtr->x) {
		return false;
	    }
	} else if (*xPtr + *widthPtr <= dInfoPtr->x) {
	    return false;
	}

	if (*xPtr + *widthPtr > dInfoPtr->maxX) {
	    if ((*widthPtr = dInfoPtr->maxX - *xPtr) <= 0) {
		return false;
	    }
	}

	if (*yPtr + *heightPtr > dInfoPtr->maxY) {
	    if ((*heightPtr = dInfoPtr->maxY - *yPtr) <= 0) {
		return false;
	    }
	}
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextDLineInfo --
 *
 *	Given an index, return information about the display line containing
 *	that character.
 *
 * Results:
 *	'true' is returned if the character is on the screen. 'false' means
 *	the character isn't on the screen. If the return value is 'true', then
 *	information is returned in the variables pointed to by xPtr, yPtr,
 *	widthPtr, heightPtr, and basePtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextGetDLineInfo(
    TkText *textPtr,		/* Widget record for text widget. */
    const TkTextIndex *indexPtr,/* Index of character whose bounding box is desired. */
    bool extents,		/* Return the extents of the bbox (the overflow, not visible on
    				 * screen). */
    int *xPtr, int *yPtr,	/* Filled with line's upper-left coordinate. */
    int *widthPtr, int *heightPtr,
				/* Filled in with line's dimensions. */
    int *basePtr)		/* Filled in with the baseline position, measured as an offset down
    				 * from *yPtr. */
{
    TextDInfo *dInfoPtr = textPtr->dInfoPtr;
    DLine *dlPtr;
    int dlx;

    /*
     * Make sure that all of the screen layout information is up to date.
     */

    if (dInfoPtr->flags & DINFO_OUT_OF_DATE) {
	UpdateDisplayInfo(textPtr);
    }

    /*
     * Find the display line containing the desired index.
     */

    dlPtr = FindDLine(textPtr, dInfoPtr->dLinePtr, indexPtr);

    /*
     * Two cases shall be trapped here because the logic later really
     * needs dlPtr to be the display line containing indexPtr:
     *   1. if no display line contains the desired index (NULL dlPtr)
     *   2. if indexPtr is before the first display line, in which case
     *      dlPtr currently points to the first display line
     */

    if (!dlPtr || TkTextIndexCompare(&dlPtr->index, indexPtr) > 0) {
	return false;
    }

    dlx = dlPtr->chunkPtr ? dlPtr->chunkPtr->x : 0;
    *xPtr = dInfoPtr->x - dInfoPtr->curXPixelOffset + dlx;
    *widthPtr = dlPtr->length - dlx;
    *yPtr = dlPtr->y;
    *heightPtr = dlPtr->height;

    if (extents) {
	*widthPtr = MAX(0, *xPtr + *widthPtr - dInfoPtr->maxX);
	*heightPtr = MAX(0, *yPtr + *heightPtr - dInfoPtr->maxY);
	*xPtr = MAX(0, -*xPtr);
	*yPtr = MAX(0, -*yPtr);
    } else {
	if (dlPtr->y + dlPtr->height > dInfoPtr->maxY) {
	    *heightPtr = dInfoPtr->maxY - dlPtr->y;
	}
    }

    *basePtr = dlPtr->baseline;
    return true;
}

/*
 * Get bounding-box information about an elided chunk.
 */

static void
ElideBboxProc(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Chunk containing desired char. */
    int index,			/* Index of desired character within the chunk. */
    int y,			/* Topmost pixel in area allocated for this line. */
    int lineHeight,		/* Height of line, in pixels. */
    int baseline,		/* Location of line's baseline, in pixels measured down from y. */
    int *xPtr, int *yPtr,	/* Gets filled in with coords of character's upper-left pixel.
    				 * X-coord is in same coordinate system as chunkPtr->x. */
    int *widthPtr,		/* Gets filled in with width of character, in pixels. */
    int *heightPtr)		/* Gets filled in with height of character, in pixels. */
{
    *xPtr = chunkPtr->x;
    *yPtr = y;
    *widthPtr = *heightPtr = 0;
}

/*
 * Measure an elided chunk.
 */

static int
ElideMeasureProc(
    TkTextDispChunk *chunkPtr,	/* Chunk containing desired coord. */
    int x)			/* X-coordinate, in same coordinate system as chunkPtr->x. */
{
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * CharMeasureProc --
 *
 *	This function is called to determine which character in a character
 *	chunk lies over a given x-coordinate.
 *
 * Results:
 *	The return value is the index *within the chunk* of the character that
 *	covers the position given by "x".
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
CharMeasureProc(
    TkTextDispChunk *chunkPtr,	/* Chunk containing desired coord. */
    int x)			/* X-coordinate, in same coordinate system as chunkPtr->x. */
{
    if (chunkPtr->endOfLineSymbol) {
	return 0;
    }
    return CharChunkMeasureChars(chunkPtr, NULL, 0, 0, chunkPtr->numBytes - 1, chunkPtr->x, x, 0, NULL);
}

/*
 *--------------------------------------------------------------
 *
 * CharBboxProc --
 *
 *	This function is called to compute the bounding box of the area
 *	occupied by a single character.
 *
 * Results:
 *	There is no return value. *xPtr and *yPtr are filled in with the
 *	coordinates of the upper left corner of the character, and *widthPtr
 *	and *heightPtr are filled in with the dimensions of the character in
 *	pixels. Note: not all of the returned bbox is necessarily visible on
 *	the screen (the rightmost part might be off-screen to the right, and
 *	the bottommost part might be off-screen to the bottom).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
CharBboxProc(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Chunk containing desired char. */
    int byteIndex,		/* Byte offset of desired character within the chunk */
    int y,			/* Topmost pixel in area allocated for this line. */
    int lineHeight,		/* Height of line, in pixels. */
    int baseline,		/* Location of line's baseline, in pixels measured down from y. */
    int *xPtr, int *yPtr,	/* Gets filled in with coords of character's upper-left pixel.
    				 * X-coord is in same coordinate system as chunkPtr->x. */
    int *widthPtr,		/* Gets filled in with width of character, in pixels. */
    int *heightPtr)		/* Gets filled in with height of character, in pixels. */
{
    CharInfo *ciPtr = chunkPtr->clientData;
    int offset = ciPtr->baseOffset + byteIndex;
    int maxX = chunkPtr->width + chunkPtr->x;
    int nextX;

    CharChunkMeasureChars(chunkPtr, NULL, 0, 0, byteIndex, chunkPtr->x, -1, 0, xPtr);

    if (byteIndex >= ciPtr->numBytes) {
	/*
	 * This situation only happens if the last character in a line is a
	 * space character, in which case it absorbs all of the extra space in
	 * the line (see TkTextCharLayoutProc).
	 */

	*widthPtr = maxX - *xPtr;
    } else if (ciPtr->u.chars[offset] == '\t' && byteIndex == ciPtr->numBytes - 1) {
	/*
	 * The desired character is a tab character that terminates a chunk;
	 * give it all the space left in the chunk.
	 */

	*widthPtr = maxX - *xPtr;
    } else {
	CharChunkMeasureChars(chunkPtr, NULL, 0, byteIndex, byteIndex + 1, *xPtr, -1, 0, &nextX);

	if (nextX >= maxX) {
	    *widthPtr = maxX - *xPtr;
	} else {
	    *widthPtr = nextX - *xPtr;

	    if (chunkPtr->additionalWidth && IsExpandableSpace(ciPtr->u.chars + offset)) {
		/*
		 * We've expanded some spaces for full line justification. Compute the
		 * width of this specific space character.
		 */

		const char *base = ciPtr->u.chars + ciPtr->baseOffset;
		const char *q = ciPtr->u.chars + offset;
		unsigned numSpaces = chunkPtr->numSpaces;
		unsigned remaining = chunkPtr->additionalWidth;

		do {
		    unsigned space;

		    assert(numSpaces > 0);
		    space = (remaining + numSpaces - 1)/numSpaces;
		    *widthPtr += space;
		    remaining -= space;
		    numSpaces -= 1;
		    if (base == q) {
			break;
		    }
		    q = Tcl_UtfPrev(q, ciPtr->u.chars);
		} while (IsExpandableSpace(q));
	    }
	}
    }

    *yPtr = y + baseline - chunkPtr->minAscent;
    *heightPtr = chunkPtr->minAscent + chunkPtr->minDescent;
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustForTab --
 *
 *	This function is called to move a series of chunks right in order to
 *	align them with a tab stop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The width of chunkPtr gets adjusted so that it absorbs the extra space
 *	due to the tab. The x locations in all the chunks after chunkPtr are
 *	adjusted rightward to align with the tab stop given by tabArrayPtr and
 *	index.
 *
 *----------------------------------------------------------------------
 */

static TkTextDispChunk *
FindEndOfTab(
    TkTextDispChunk *chunkPtr,
    int *decimalPtr)
{
    TkTextDispChunk *decimalChunkPtr = NULL;
    bool gotDigit = false;

    *decimalPtr = 0;

    for ( ; chunkPtr; chunkPtr = chunkPtr->nextPtr) {
	if (IsCharChunk(chunkPtr)) {
	    CharInfo *ciPtr = chunkPtr->clientData;
	    const char *s = ciPtr->u.chars + ciPtr->baseOffset;
	    const char *p;
	    int i;

	    for (p = s, i = 0; i < ciPtr->numBytes; ++p, ++i) {
		if (isdigit(*p)) {
		    gotDigit = true;
		} else if (*p == '.' || *p == ',') {
		    *decimalPtr = p - s;
		    decimalChunkPtr = chunkPtr;
		} else if (gotDigit) {
		    if (!decimalChunkPtr) {
			*decimalPtr = p - s;
			decimalChunkPtr = chunkPtr;
		    }
		    return decimalChunkPtr;
		}
	    }
	}
    }
    return decimalChunkPtr;
}

static void
AdjustForTab(
    LayoutData *data)
{
    int x, desired = 0, delta, width;
    TkTextDispChunk *chunkPtr, *nextChunkPtr, *chPtr;
    TkTextTabArray *tabArrayPtr;
    TkText *textPtr;
    int tabX, tabIndex;
    TkTextTabAlign alignment;

    assert(data->tabIndex >= 0);
    assert(data->tabChunkPtr);

    chunkPtr = data->tabChunkPtr;
    nextChunkPtr = chunkPtr->nextPtr;

    if (!nextChunkPtr) {
	/* Nothing after the actual tab; just return. */
	return;
    }

    tabIndex = data->tabIndex;
    textPtr = data->textPtr;
    tabArrayPtr = data->tabArrayPtr;
    x = nextChunkPtr->x;

    /*
     * If no tab information has been given, assuming tab stops are at 8
     * average-sized characters. Still ensure we respect the tabular versus
     * wordprocessor tab style.
     */

    if (!tabArrayPtr || tabArrayPtr->numTabs == 0) {
	/*
	 * No tab information has been given, so use the default
	 * interpretation of tabs.
	 */

	unsigned tabWidth = textPtr->charWidth*8;

	tabWidth = MAX(1u, tabWidth);

	if (textPtr->tabStyle == TK_TEXT_TABSTYLE_TABULAR) {
	    desired = tabWidth*(tabIndex + 1);
	} else {
	    desired = NextTabStop(tabWidth, x, 0);
	}
    } else {
	if (tabIndex < tabArrayPtr->numTabs) {
	    alignment = tabArrayPtr->tabs[tabIndex].alignment;
	    tabX = tabArrayPtr->tabs[tabIndex].location;
	} else {
	    /*
	     * Ran out of tab stops; compute a tab position by extrapolating from
	     * the last two tab positions.
	     */

	    tabX = (int) (tabArrayPtr->lastTab +
		    (tabIndex + 1 - tabArrayPtr->numTabs)*tabArrayPtr->tabIncrement + 0.5);
	    alignment = tabArrayPtr->tabs[tabArrayPtr->numTabs - 1].alignment;
	}

	switch (alignment) {
	case LEFT:
	    desired = tabX;
	    break;

	case CENTER:
	case RIGHT:
	    /*
	     * Compute the width of all the information in the tab group, then use
	     * it to pick a desired location.
	     */

	    width = 0;
	    for (chPtr = nextChunkPtr; chPtr; chPtr = chPtr->nextPtr) {
		width += chPtr->width;
	    }
	    desired = tabX - (alignment == CENTER ? width/2 : width);
	    break;

	case NUMERIC: {
	    /*
	     * Search through the text to be tabbed, looking for the last ',' or
	     * '.' before the first character that isn't a number, comma, period,
	     * or sign.
	     */

	    int decimal;
	    TkTextDispChunk *decimalChunkPtr = FindEndOfTab(nextChunkPtr, &decimal);

	    if (decimalChunkPtr) {
		int curX;

		CharChunkMeasureChars(decimalChunkPtr, NULL, 0, 0, decimal,
			decimalChunkPtr->x, -1, 0, &curX);
		desired = tabX - (curX - x);
	    } else {
		/*
		 * There wasn't a decimal point. Right justify the text.
		 */

		width = 0;
		for (chPtr = nextChunkPtr; chPtr; chPtr = chPtr->nextPtr) {
		    width += chPtr->width;
		}
		desired = tabX - width;
	    }
	}
	}
    }

    /*
     * Shift all of the chunks to the right so that the left edge is at the
     * desired location, then expand the chunk containing the tab. Be sure
     * that the tab occupies at least the width of a space character.
     */

    delta = MAX(textPtr->spaceWidth, desired - x);
    for (chPtr = nextChunkPtr; chPtr; chPtr = chPtr->nextPtr) {
	chPtr->x += delta;
    }
    chunkPtr->width += delta;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeSizeOfTab --
 *
 *	This estimates the amount of white space that will be consumed by
 *	a tab.
 *
 * Results:
 *	The 'current tab' is the minimum number of pixels that will be occupied
 *	by the next tab of tabArrayPtr, assuming that the current position on the
 *	line is x and the end of the line is maxX. The 'next tab' is determined
 *	by a combination of the current position (x) which it must be equal to or
 *	beyond, and the tab count in indexPtr.
 *
 *	For numeric tabs, this is a conservative estimate. The 'current tab' is
 *	always >= 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ComputeSizeOfTab(
    LayoutData *data)
{
    TkText *textPtr;
    TkTextTabArray *tabArrayPtr;
    unsigned tabWidth;
    int tabX;
    TkTextTabAlign alignment;

    textPtr = data->textPtr;
    tabArrayPtr = data->tabArrayPtr;

    if (!tabArrayPtr || tabArrayPtr->numTabs == 0) {
	/*
	 * We're using a default tab spacing of 8 characters.
	 */

	tabWidth = MAX(1, textPtr->charWidth*8);
    } else {
	tabWidth = 0;
    }

    do {
	/*
	 * We were given the count before this tab, so increment it first.
	 */

	data->tabIndex += 1;

	if (!tabArrayPtr || tabArrayPtr->numTabs == 0) {
	    /*
	     * We're using a default tab spacing calculated above.
	     */

	    tabX = tabWidth*(data->tabIndex + 1);
	    alignment = LEFT;
	} else if (data->tabIndex < tabArrayPtr->numTabs) {
	    tabX = tabArrayPtr->tabs[data->tabIndex].location;
	    alignment = tabArrayPtr->tabs[data->tabIndex].alignment;
	} else {
	    /*
	     * Ran out of tab stops; compute a tab position by extrapolating.
	     */

	    tabX = (int) (tabArrayPtr->lastTab
		    + (data->tabIndex + 1 - tabArrayPtr->numTabs)*tabArrayPtr->tabIncrement
		    + 0.5);
	    alignment = tabArrayPtr->tabs[tabArrayPtr->numTabs - 1].alignment;
	}

	/*
	 * If this tab stop is before the current x position, then we have two
	 * cases:
	 *
	 * With 'wordprocessor' style tabs, we must obviously continue until
	 * we reach the text tab stop.
	 *
	 * With 'tabular' style tabs, we always use the data->tabIndex'th tab stop.
	 */
    } while (tabX <= data->x && data->tabStyle == TK_TEXT_TABSTYLE_WORDPROCESSOR);

    /*
     * Inform our caller of how many tab stops we've used up.
     */

    switch (alignment) {
    case CENTER:
	/*
	 * Be very careful in the arithmetic below, because maxX may be the
	 * largest positive number: watch out for integer overflow.
	 */

	if (data->maxX - tabX < tabX - data->x) {
	    data->tabSize = data->maxX - data->x - 2*(data->maxX - tabX);
	} else {
	    data->tabSize = 0;
	}
	break;

    case RIGHT:
	data->tabSize = 0;
	break;

    case LEFT:
    case NUMERIC:
	/*
	 * Note: this treats NUMERIC alignment the same as LEFT alignment, which
	 * is somewhat conservative. However, it's pretty tricky at this point to
	 * figure out exactly where the damn decimal point will be.
	 */

	data->tabSize = tabX - data->x;
	assert(textPtr->spaceWidth > 0); /* ensure positive size */
    	break;
    }

    data->tabSize = MAX(data->tabSize, textPtr->spaceWidth);
}

/*
 *---------------------------------------------------------------------------
 *
 * NextTabStop --
 *
 *	Given the current position, determine where the next default tab stop
 *	would be located. This function is called when the current chunk in
 *	the text has no tabs defined and so the default tab spacing for the
 *	font should be used, provided we are using wordprocessor style tabs.
 *
 * Results:
 *	The location in pixels of the next tab stop.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
NextTabStop(
    unsigned tabWidth,		/* Default tab width of the widget. */
    int x,			/* X-position in pixels where last character was drawn. The next
    				 * tab stop occurs somewhere after this location. */
    int tabOrigin)		/* The origin for tab stops. May be non-zero if text has been
    				 * scrolled. */
{
    int rem;

    assert(tabWidth > 0);

    x += tabWidth;
    if ((rem = (x - tabOrigin) % tabWidth) < 0) {
	rem += tabWidth;
    }
    x -= rem;
    return x;
}

/*
 *---------------------------------------------------------------------------
 *
 * MeasureChars --
 *
 *	Determine the number of characters from the string that will fit in
 *	the given horizontal span. The measurement is done under the
 *	assumption that Tk_DrawChars will be used to actually display the
 *	characters.
 *
 *	If tabs are encountered in the string, they will be ignored (they
 *	should only occur as last character of the string anyway).
 *
 *	If a newline is encountered in the string, the line will be broken at
 *	that point.
 *
 * Results:
 *	The return value is the number of bytes from the range of start to end
 *	in source that fit in the span given by startX and maxX. *nextXPtr is
 *	filled in with the x-coordinate at which the first character that
 *	didn't fit would be drawn, if it were to be drawn.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

#if TK_DRAW_IN_CONTEXT

static int
TkpMeasureChars(
    Tk_Font tkfont,
    const char *source,
    int numBytes,
    int rangeStart,
    int rangeLength,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    return TkpMeasureCharsInContext(tkfont, source, numBytes, rangeStart,
	    rangeLength, maxLength, flags, lengthPtr);
}

#else /* if !TK_DRAW_IN_CONTEXT */

static int
TkpMeasureChars(
    Tk_Font tkfont,
    const char *source,
    int numBytes,
    int rangeStart,
    int rangeLength,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength, maxLength, flags, lengthPtr);
}

#endif /* TK_DRAW_IN_CONTEXT */

static int
MeasureChars(
    Tk_Font tkfont,		/* Font in which to draw characters. */
    const char *source,		/* Characters to be displayed. Need not be NUL-terminated. */
    int maxBytes,		/* Maximum # of bytes to consider from source. */
    int rangeStart, int rangeLength,
				/* Range of bytes to consider in source.*/
    int startX,			/* X-position at which first character will be drawn. */
    int maxX,			/* Don't consider any character that would cross this x-position. */
    int flags,			/* Flags to pass to Tk_MeasureChars. */
    int *nextXPtr)		/* Return x-position of terminating character here, can be NULL. */
{
    int curX, width, ch;
    const char *special, *end, *start;

    ch = 0;
    curX = startX;
    start = source + rangeStart;
    end = start + rangeLength;
    special = start;

    while (start < end) {
	if (start >= special) {
	    /*
	     * Find the next special character in the string.
	     */

	    for (special = start; special < end; ++special) {
		ch = *special;
		if (ch == '\t' || ch == '\n') {
		    break;
		}
	    }
	}

	/*
	 * Special points at the next special character (or the end of the
	 * string). Process characters between start and special.
	 */

	if (maxX >= 0 && curX >= maxX) {
	    break;
	}
	start += TkpMeasureChars(tkfont, source, maxBytes, start - source, special - start,
		maxX >= 0 ? maxX - curX : -1, flags, &width);
	curX += width;
	if (start < special) {
	    /*
	     * No more chars fit in line.
	     */

	    break;
	}
	if (special < end) {
	    if (ch != '\t') {
		break;
	    }
	    start += 1;
	}
    }

    if (nextXPtr) {
	*nextXPtr = curX;
    }
    return start - (source + rangeStart);
}

/*
 *----------------------------------------------------------------------
 *
 * TextGetScrollInfoObj --
 *
 *	This function is invoked to parse "xview" and "yview" scrolling
 *	commands for text widgets using the new scrolling command syntax
 *	("moveto" or "scroll" options). It extends the public
 *	Tk_GetScrollInfoObj function with the addition of "pixels" as a valid
 *	unit alongside "pages" and "units". It is a shame the core API isn't
 *	more flexible in this regard.
 *
 * Results:
 *	The return value is either SCROLL_MOVETO, SCROLL_PAGES,
 *	SCROLL_UNITS, SCROLL_PIXELS or SCROLL_ERROR. This
 *	indicates whether the command was successfully parsed and what form
 *	the command took. If SCROLL_MOVETO, *dblPtr is filled in with
 *	the desired position; if SCROLL_PAGES, SCROLL_PIXELS or
 *	SCROLL_UNITS, *intPtr is filled in with the number of
 *	pages/pixels/lines to move (may be negative); if SCROLL_ERROR,
 *	the interp's result contains an error message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ScrollMethod
TextGetScrollInfoObj(
    Tcl_Interp *interp,		/* Used for error reporting. */
    TkText *textPtr,		/* Information about the text widget. */
    int objc,			/* # arguments for command. */
    Tcl_Obj *const objv[],	/* Arguments for command. */
    double *dblPtr,		/* Filled in with argument "moveto" option, if any. */
    int *intPtr)		/* Filled in with number of pages or lines or pixels to scroll,
    				 * if any. */
{
    static const char *const subcommands[] = {
	"moveto", "scroll", NULL
    };
    enum viewSubcmds {
	VIEW_MOVETO, VIEW_SCROLL
    };
    static const char *const units[] = {
	"units", "pages", "pixels", NULL
    };
    enum viewUnits {
	VIEW_SCROLL_UNITS, VIEW_SCROLL_PAGES, VIEW_SCROLL_PIXELS
    };
    int index;

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], subcommands, sizeof(char *), "option", 0, &index)
	    != TCL_OK) {
	return SCROLL_ERROR;
    }

    switch ((enum viewSubcmds) index) {
    case VIEW_MOVETO:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "fraction");
	    return SCROLL_ERROR;
	}
	if (Tcl_GetDoubleFromObj(interp, objv[3], dblPtr) != TCL_OK) {
	    return SCROLL_ERROR;
	}
	return SCROLL_MOVETO;
    case VIEW_SCROLL:
	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "number units|pages|pixels");
	    return SCROLL_ERROR;
	}
	if (Tcl_GetIndexFromObjStruct(interp, objv[4], units, sizeof(char *), "argument", 0, &index)
		!= TCL_OK) {
	    return SCROLL_ERROR;
	}
	switch ((enum viewUnits) index) {
	case VIEW_SCROLL_PAGES:
	    if (Tcl_GetIntFromObj(interp, objv[3], intPtr) != TCL_OK) {
		return SCROLL_ERROR;
	    }
	    return SCROLL_PAGES;
	case VIEW_SCROLL_PIXELS:
	    if (Tk_GetPixelsFromObj(interp, textPtr->tkwin, objv[3], intPtr) != TCL_OK) {
		return SCROLL_ERROR;
	    }
	    return SCROLL_PIXELS;
	case VIEW_SCROLL_UNITS:
	    if (Tcl_GetIntFromObj(interp, objv[3], intPtr) != TCL_OK) {
		return SCROLL_ERROR;
	    }
	    return SCROLL_UNITS;
	}
    }
    assert(!"unexpected switch fallthrough");
    return SCROLL_ERROR; /* should be never reached */
}

/*
 *----------------------------------------------------------------------
 *
 * AllocCharInfo --
 *
 *	Allocate new char info struct. We are using a pool of char info
 *	structs.
 *
 * Results:
 *	The newly allocated struct, or a free char info struct from
 *	pool.
 *
 * Side effects:
 *	May allocate some memory.
 *
 *----------------------------------------------------------------------
 */

static CharInfo *
AllocCharInfo(
    TkText *textPtr)
{
    TextDInfo *dInfoPtr;
    CharInfo *ciPtr;

    assert(textPtr);

    dInfoPtr = textPtr->dInfoPtr;
    if ((ciPtr = dInfoPtr->charInfoPoolPtr)) {
	dInfoPtr->charInfoPoolPtr = dInfoPtr->charInfoPoolPtr->u.next;
    } else {
	ciPtr = malloc(sizeof(CharInfo));
	DEBUG_ALLOC(tkTextCountNewCharInfo++);
    }

    return ciPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeCharInfo --
 *
 *	Put back given char info to pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeCharInfo(
    TkText *textPtr,
    CharInfo *ciPtr)
{
    TextDInfo *dInfoPtr;

    assert(textPtr);
    assert(ciPtr);

    TkBTreeFreeSegment(ciPtr->segPtr);
    dInfoPtr = textPtr->dInfoPtr;
    ciPtr->u.next = dInfoPtr->charInfoPoolPtr;
    dInfoPtr->charInfoPoolPtr = ciPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeBreakIndex --
 *
 *	Compute a break location. If we're in word wrap mode, a break
 *	can occurr after any space character, or at the end of the chunk
 *	if the the next segment (ignoring those with zero size) is not a
 *	character segment.
 *
 * Results:
 *	The computed break location.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ComputeBreakIndex(
    TkText *textPtr,
    const TkTextDispChunk *chunkPtr,
    TkTextSegment *segPtr,
    int byteOffset,
    TkWrapMode wrapMode,
    TkTextSpaceMode spaceMode)
{
    switch (wrapMode) {
    case TEXT_WRAPMODE_NONE:
	break;
    case TEXT_WRAPMODE_CHAR:
    case TEXT_WRAPMODE_NULL:
	return chunkPtr->numBytes;
    case TEXT_WRAPMODE_WORD:
    case TEXT_WRAPMODE_CODEPOINT: {
	TkTextSegment *nextPtr;
	const char *p;
	int count;

	if (segPtr->typePtr == &tkTextHyphenType) {
	    return 1;
	}

	if ((int) chunkPtr->numBytes + byteOffset == segPtr->size) {
	    for (nextPtr = segPtr->nextPtr; nextPtr; nextPtr = nextPtr->nextPtr) {
		if (nextPtr->size > 0) {
		    if (!(nextPtr->typePtr->group & (SEG_GROUP_CHAR|SEG_GROUP_HYPHEN))) {
			return chunkPtr->numBytes;
		    }
		    break;
		} else if (nextPtr->typePtr == &tkTextBranchType) {
		    nextPtr = nextPtr->body.branch.nextPtr->nextPtr;
		}
	    }
	}

	count = chunkPtr->numBytes;
	if (chunkPtr->endsWithSyllable) {
	    assert(chunkPtr->numBytes > 0);
	    count -= 1;
	}
	p = segPtr->body.chars + byteOffset + count - 1;

	if (wrapMode == TEXT_WRAPMODE_WORD) {
	    /*
	     * Don't use isspace(); effects are unpredictable (because the result
	     * is locale dependent) and can lead to odd word-wrapping problems on
	     * some platforms. Also don't use Tcl_UniCharIsSpace here either, this
	     * can be used when displaying Markup in read-only mode (except the
	     * non-breaking space), but in text data there is a difference between
	     * ASCII spaces and all other spaces, and this difference must be
	     * visible for the user (line break makes the spaces indistinguishable).
	     * Keep in mind that the text widget will also be used for editing
	     * text. What we actually want is only the ASCII space characters, so
	     * use them explicitly...
	     *
	     * NOTE: don't break at HYPHEN-MINUS character (U+002D), because the
	     * meaning of this character is contextual. The user has to use the
	     * "codepoint" wrap mode if he want's line breaking at hard hyphen
	     * characters.
	     */

	    for ( ; count > 0; --count, --p) {
		switch (*p) {
		case ' ':
		    if (spaceMode == TEXT_SPACEMODE_EXACT) {
			return -1;
		    }
		    /* fallthru */
		case '\t': case '\n': case '\v': case '\f': case '\r':
		    return count;
		}
	    }
	} else {
	    const char *brks;
	    int i;

	    if (*p == '\n') {
		return count; /* catch special case end of line */
	    }

	    brks = chunkPtr->brks;
	    i = count - 1;

	    /* In this case the break locations must be already computed. */
	    assert(brks);

	    for ( ; i >= 0; --i, --p) {
		if (brks[i] == LINEBREAK_ALLOWBREAK) {
		    if (*p == ' ' && spaceMode == TEXT_SPACEMODE_EXACT) {
			return -1;
		    }
		    return i + 1;
		}
	    }
	}
	break;
    }
    }

    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCheckDisplayLineConsistency --
 *
 *	This function is called for consistency checking of display line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If anything suspicious is found in the display lines, the function
 *	panics.
 *
 *----------------------------------------------------------------------
 */

void
TkTextCheckDisplayLineConsistency(
    const TkText *textPtr)
{
    DLine *dlPtr;

    for (dlPtr = textPtr->dInfoPtr->dLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	if (dlPtr->chunkPtr) {
	    const TkTextLine *linePtr = TkTextIndexGetLine(&dlPtr->index);

	    if (!linePtr->parentPtr || linePtr->parentPtr == (void *) 0x61616161) {
		Tcl_Panic("CheckDisplayLineConsisteny: expired index in display line");
	    }
	}
    }

    for (dlPtr = textPtr->dInfoPtr->savedDLinePtr; dlPtr; dlPtr = dlPtr->nextPtr) {
	if (dlPtr->chunkPtr) {
	    const TkTextLine *linePtr = TkTextIndexGetLine(&dlPtr->index);

	    if (!linePtr->parentPtr || linePtr->parentPtr == (void *) 0x61616161) {
		Tcl_Panic("CheckDisplayLineConsisteny: expired index in saved display line");
	    }
	}
    }

    dlPtr = textPtr->dInfoPtr->cachedDLinePtr;
    if (dlPtr && dlPtr->chunkPtr) {
	const TkTextLine *linePtr = TkTextIndexGetLine(&dlPtr->index);

	if (!linePtr->parentPtr || linePtr->parentPtr == (void *) 0x61616161) {
	    Tcl_Panic("CheckDisplayLineConsisteny: expired index in cached display line");
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CheckLineMetricConsistency --
 *
 *	This function is called for consistency checking of display line
 *	metric information. Call this function only if all line metrics
 *	are up-to-date.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If anything suspicious is found in the display line metric information,
 *	the function panics.
 *
 *----------------------------------------------------------------------
 */

static void
CheckLineMetricConsistency(
    const TkText *textPtr)
{
    const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    unsigned epoch = textPtr->dInfoPtr->lineMetricUpdateEpoch;
    const TkTextLine *lastLinePtr;
    const TkTextLine *linePtr;
    unsigned lineNum = 0;
    unsigned reference;

    assert(textPtr->pixelReference >= 0);

    linePtr = TkBTreeGetStartLine(textPtr);
    lastLinePtr = TkBTreeGetLastLine(textPtr);

    if (textPtr->dInfoPtr->firstLineNo != TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr, NULL)) {
	Tcl_Panic("CheckLineMetricConsistency: firstLineNo is not up-to-date");
    }
    if (textPtr->dInfoPtr->lastLineNo != TkBTreeLinesTo(sharedTextPtr->tree, NULL, lastLinePtr, NULL)) {
	Tcl_Panic("CheckLineMetricConsistency: lastLineNo is not up-to-date");
    }

    reference = textPtr->pixelReference;

    while (linePtr != lastLinePtr) {
	const TkTextPixelInfo *pixelInfo = linePtr->pixelInfo + reference;
	const TkTextDispLineInfo *dispLineInfo = pixelInfo->dispLineInfo;

	if ((pixelInfo->epoch & EPOCH_MASK) != epoch) {
	    Tcl_Panic("CheckLineMetricConsistency: line metric info is not up-to-date");
	}
	if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
	    Tcl_Panic("CheckLineMetricConsistency: computation of this line is not yet complete");
	}

	linePtr = linePtr->nextPtr;
	lineNum += 1;

	while (linePtr != lastLinePtr && !linePtr->logicalLine) {
	    const TkTextPixelInfo *pixelInfo = linePtr->pixelInfo + reference;

	    if ((pixelInfo->epoch & EPOCH_MASK) != epoch) {
		Tcl_Panic("CheckLineMetricConsistency: line metric info is not up-to-date");
	    }
	    if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
		Tcl_Panic("CheckLineMetricConsistency: partial flag shouldn't be set");
	    }
	    if (pixelInfo->dispLineInfo) {
		Tcl_Panic("CheckLineMetricConsistency: merged line should not have display line info");
	    }
	    if (pixelInfo->height > 0) {
		Tcl_Panic("CheckLineMetricConsistency: merged line should not have a height");
	    }

	    linePtr = linePtr->nextPtr;
	    lineNum += 1;
	}

	if (!lastLinePtr->nextPtr) {
	    const TkTextPixelInfo *pixelInfo = lastLinePtr->pixelInfo + reference;

	    if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
		Tcl_Panic("CheckLineMetricConsistency: partial flag shouldn't be set in last line");
	    }
	    if (pixelInfo->dispLineInfo) {
		Tcl_Panic("CheckLineMetricConsistency: last line should not have display line info");
	    }
	    if (pixelInfo->height > 0) {
		Tcl_Panic("CheckLineMetricConsistency: last line should not have a height");
	    }
	}

	if (dispLineInfo) {
	    unsigned pixels = 0;
	    unsigned k;

	    if (dispLineInfo->numDispLines == 1) {
		Tcl_Panic("CheckLineMetricConsistency: this line should not have display line info");
	    }
	    for (k = 0; k < dispLineInfo->numDispLines; ++k) {
		const TkTextDispLineEntry *entry = dispLineInfo->entry + k;

		if (k == 0 && entry->byteOffset != 0) {
		    Tcl_Panic("CheckLineMetricConsistency: first display line (line %d) should "
			    "have byte offset zero", lineNum);
		}
		if ((entry + 1)->byteOffset <= entry->byteOffset) {
		    Tcl_Panic("CheckLineMetricConsistency: display line (line %d) has invalid byte "
			    "offset %d (previous is %d)", lineNum, (entry + 1)->byteOffset,
			    entry->byteOffset);
		}
		if (entry->height == 0) {
		    Tcl_Panic("CheckLineMetricConsistency: display line (%d) has zero height", lineNum);
		}
		pixels += entry->height;
	    }
	    if (pixels != pixelInfo->height) {
		Tcl_Panic("CheckLineMetricConsistency: sum of display line pixels is wrong (line %d)",
			lineNum);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCheckLineMetricUpdate --
 *
 *	This function is called for consistency checking of display line
 *	metric update information.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If anything suspicious is found in the display line metric update
 *	information, the function panics.
 *
 *----------------------------------------------------------------------
 */

void
TkTextCheckLineMetricUpdate(
    const TkText *textPtr)
{
    const TkRangeList *ranges;
    const TkRange *range;
    TkTextBTree tree;
    unsigned epoch;
    int n, total;

    assert(textPtr);

    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	return;
    }
    if (!textPtr->endMarker->sectionPtr || !textPtr->startMarker->sectionPtr) {
	/*
	 * Called inside unlink of start/end marker, in this case we cannot check
	 * (and we don't need a check here).
	 */
	return;
    }

    ranges = textPtr->dInfoPtr->lineMetricUpdateRanges;
    tree = textPtr->sharedTextPtr->tree;
    total = TkBTreeNumLines(tree, textPtr);

    if (!TkRangeListIsEmpty(ranges) && TkRangeListHigh(ranges) >= total) {
	Tcl_Panic("TkTextCheckLineMetricUpdate: line %d is out of range (max=%d)\n",
		TkRangeListHigh(ranges), total);
    }

    range = TkRangeListFirst(ranges);
    epoch = textPtr->dInfoPtr->lineMetricUpdateEpoch;

    for (n = 0; n < total - 1; ++n) {
	const TkTextPixelInfo *pixelInfo;

	if (range && range->low == n) {
	    n = range->high;
	    range = TkRangeListNext(ranges, range);
	    continue;
	}

	pixelInfo = TkBTreeLinePixelInfo(textPtr, TkBTreeFindLine(tree, textPtr, n));

	if (pixelInfo->epoch && (pixelInfo->epoch & EPOCH_MASK) != epoch) {
	    Tcl_Panic("TkTextCheckLineMetricUpdate: line %d is not up-to-date\n", n);
	}
	if (pixelInfo->epoch & PARTIAL_COMPUTED_BIT) {
	    Tcl_Panic("TkTextCheckLineMetricUpdate: line metric computation (line %d) is not "
		    "yet complete\n", n);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * CharChunkMeasureChars --
 *
 *	Determine the number of characters from a char chunk that will fit in
 *	the given horizontal span.
 *
 *	This is the same as MeasureChars (which see), but in the context of a
 *	char chunk, i.e. on a higher level of abstraction. Use this function
 *	whereever possible instead of plain MeasureChars, so that the right
 *	context is used automatically.
 *
 * Results:
 *	The return value is the number of bytes from the range of start to end
 *	in source that fit in the span given by startX and maxX. *nextXPtr is
 *	filled in with the x-coordinate at which the first character that
 *	didn't fit would be drawn, if it were to be drawn.
 *
 * Side effects:
 *	None.
 *--------------------------------------------------------------
 */

static int
CharChunkMeasureChars(
    TkTextDispChunk *chunkPtr,	/* Chunk from which to measure. */
    const char *chars,		/* Chars to use, instead of the chunk's own. Used by the layoutproc
    				 * during chunk setup. All other callers use NULL. Not NUL-terminated. */
    int charsLen,		/* Length of the "chars" parameter. */
    int start, int end,		/* The range of chars to measure inside the chunk (or inside the
    				 * additional chars). */
    int startX,			/* Starting x coordinate where the measured span will begin. */
    int maxX,			/* Maximum pixel width of the span. May be -1 for unlimited. */
    int flags,			/* Flags to pass to MeasureChars. */
    int *nextXPtr)		/* The function puts the newly calculated right border x-position of
    				 * the span here; can be NULL. */
{
    Tk_Font tkfont = chunkPtr->stylePtr->sValuePtr->tkfont;
    CharInfo *ciPtr = chunkPtr->clientData;
    int fit, rangeStart;

#if TK_LAYOUT_WITH_BASE_CHUNKS

    int widthUntilStart = 0;

    assert(chunkPtr->baseChunkPtr);

    if (!chars) {
	const Tcl_DString *baseChars = &chunkPtr->baseChunkPtr->baseChars;

	chars = Tcl_DStringValue(baseChars);
	charsLen = Tcl_DStringLength(baseChars);
	start += ciPtr->baseOffset;
	if (end == -1) {
	    assert(ciPtr->numBytes >= chunkPtr->wrappedAtSpace);
	    end = ciPtr->baseOffset + ciPtr->numBytes - chunkPtr->wrappedAtSpace;
	} else {
	    end += ciPtr->baseOffset;
	}
	if (chunkPtr->wrappedAtSpace) {
	    assert(charsLen >= 1);
	    charsLen -= 1;
	}
    }

    if (start != ciPtr->baseOffset) {
	MeasureChars(tkfont, chars, charsLen, 0, start, 0, -1, 0, &widthUntilStart);
    }

    startX = chunkPtr->baseChunkPtr->x + (startX - widthUntilStart - chunkPtr->x);
    rangeStart = 0;

#else

    rangeStart = start;

    if (!chars) {
	chars = ciPtr->u.chars;
	charsLen = ciPtr->numBytes;
    }

#endif

    if (end == -1) {
	end = charsLen;
    }

    fit = MeasureChars(tkfont, chars, charsLen, rangeStart, end - rangeStart,
	    startX, maxX, flags, nextXPtr);

    return MAX(0, fit - start);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextCharLayoutProc --
 *
 *	This function is the "layoutProc" for character segments.
 *
 * Results:
 *	If there is something to display for the chunk then a non-zero value
 *	is returned and the fields of chunkPtr will be filled in (see the
 *	declaration of TkTextDispChunk in tkText.h for details). If zero is
 *	returned it means that no characters from this chunk fit in the
 *	window. If -1 is returned it means that this segment just doesn't need
 *	to be displayed (never happens for text).
 *
 * Side effects:
 *	Memory is allocated to hold additional information about the chunk.
 *
 *--------------------------------------------------------------
 */

static bool
EndsWithSyllable(
    TkTextSegment *segPtr)
{
    if (segPtr->typePtr->group == SEG_GROUP_CHAR) {
	for (segPtr = segPtr->nextPtr; segPtr; segPtr = segPtr->nextPtr) {
	    switch (segPtr->typePtr->group) {
	    case SEG_GROUP_MARK:
		break;
	    case SEG_GROUP_HYPHEN:
		return true;
	    case SEG_GROUP_BRANCH:
		if (segPtr->typePtr == &tkTextBranchType) {
		    segPtr = segPtr->body.branch.nextPtr;
		    break;
		}
		/* fallthru */
	    default:
		return false;
	    }
	}
    }
    return false;
}

int
TkTextCharLayoutProc(
    const TkTextIndex *indexPtr,/* Index of first character to lay out (corresponds to segPtr and
    				 * offset). */
    TkTextSegment *segPtr,	/* Segment being layed out. */
    int byteOffset,		/* Byte offset within segment of first character to consider. */
    int maxX,			/* Chunk must not occupy pixels at this position or higher. */
    int maxBytes,		/* Chunk must not include more than this many characters. */
    bool noCharsYet,		/* 'true' means no characters have been assigned to this display
    				 * line yet. */
    TkWrapMode wrapMode,	/* How to handle line wrapping: TEXT_WRAPMODE_CHAR, TEXT_WRAPMODE_NONE,
    				 * TEXT_WRAPMODE_WORD, or TEXT_WRAPMODE_CODEPOINT. */
    TkTextSpaceMode spaceMode,	/* How to handle displaying spaces. Must be TEXT_SPACEMODE_NONE,
    				 * TEXT_SPACEMODE_EXACT, or TEXT_SPACEMODE_TRIM. */
    TkTextDispChunk *chunkPtr)	/* Structure to fill in with information about this chunk. The x
    				 * field has already been set by the caller. */
{
    Tk_Font tkfont;
    int nextX, bytesThatFit;
    Tk_FontMetrics fm;
    CharInfo *ciPtr;
    char const *p;

    assert(indexPtr->textPtr);
    assert(chunkPtr->clientData);

    /*
     * Figure out how many characters will fit in the space we've got. Include
     * the next character, even though it won't fit completely, if any of the
     * following is true:
     *
     *	 (a) the chunk contains no characters and the display line contains no
     *	     characters yet (i.e. the line isn't wide enough to hold even a
     *	     single character).
     *
     *	 (b) at least one pixel of the character is visible, we have not
     *	     already exceeded the character limit, and the next character is a
     *	     white space character.
     */

    tkfont = chunkPtr->stylePtr->sValuePtr->tkfont;
    ciPtr = chunkPtr->clientData;
    chunkPtr->layoutProcs = &layoutCharProcs;
    p = segPtr->body.chars + byteOffset;

    bytesThatFit = CharChunkMeasureChars(chunkPtr, ciPtr->u.chars, ciPtr->baseOffset + maxBytes,
	    ciPtr->baseOffset, -1, chunkPtr->x, maxX, TK_ISOLATE_END, &nextX);

    /*
     * NOTE: do not trim white spaces at the end of line, it would be impossible
     * for the user to see typos like mistakenly typing two consecutive spaces.
     */

    if (bytesThatFit < maxBytes) {
	if (bytesThatFit == 0 && noCharsYet) {
	    int chLen;

#if TCL_UTF_MAX > 4
	    /*
	     * HACK: Support of pseudo UTF-8 strings. Needed because of this
	     * bad hack with TCL_UTF_MAX > 4, the whole thing is amateurish.
	     * (See function GetLineBreakFunc() about the very severe problems
	     * with TCL_UTF_MAX > 4).
	     */

	    int ch;
	    chLen = TkUtfToUniChar(p, &ch);
#else
	    /*
	     * Proper implementation for UTF-8 strings:
	     */

	    Tcl_UniChar ch;
	    chLen = Tcl_UtfToUniChar(p, &ch);
#endif

	    /*
	     * At least one character should be contained in current display line.
	     */

	    bytesThatFit = CharChunkMeasureChars(chunkPtr, ciPtr->u.chars, ciPtr->baseOffset + chLen,
		    ciPtr->baseOffset, -1, chunkPtr->x, -1, 0, &nextX);
	}
	if (spaceMode == TEXT_SPACEMODE_TRIM) {
	    while (IsBlank(p[bytesThatFit])) {
		bytesThatFit += 1;
	    }
	}
	if (p[bytesThatFit] == '\n') {
	    /*
	     * A newline character takes up no space, so if the previous
	     * character fits then so does the newline.
	     */

	    bytesThatFit += 1;
	} else if (spaceMode == TEXT_SPACEMODE_NONE
		&& nextX <= maxX
		&& ((1 << wrapMode) & ((1 << TEXT_WRAPMODE_WORD) | (1 << TEXT_WRAPMODE_CODEPOINT)))
		&& IsBlank(p[bytesThatFit])
		&& !(bytesThatFit == 0
		    && chunkPtr->prevCharChunkPtr
		    && chunkPtr->prevCharChunkPtr->wrappedAtSpace)) {
	    /*
	     * Space characters are funny, in that they are considered to fit at the end
	     * of the line. Just give the space character whatever space is left.
	     */

	    nextX = maxX;
	    bytesThatFit += 1;

	    /* Do not wrap next chunk in this line. */
	    chunkPtr->wrappedAtSpace = true;
	}
	if (bytesThatFit == 0) {
	    return 0;
	}
    }

    Tk_GetFontMetrics(tkfont, &fm);

    /*
     * Fill in the chunk structure and allocate and initialize a CharInfo structure. If the
     * last character is a newline then don't bother to display it.
     */

    chunkPtr->endsWithSyllable =
	    p[bytesThatFit] == '\0' && indexPtr->textPtr->hyphenate && EndsWithSyllable(segPtr);
    chunkPtr->numBytes = bytesThatFit;
    chunkPtr->segByteOffset = byteOffset;
    chunkPtr->minAscent = fm.ascent + chunkPtr->stylePtr->sValuePtr->offset;
    chunkPtr->minDescent = fm.descent - chunkPtr->stylePtr->sValuePtr->offset;
    chunkPtr->minHeight = 0;
    chunkPtr->width = nextX - chunkPtr->x;
    chunkPtr->breakIndex =
	    ComputeBreakIndex(indexPtr->textPtr, chunkPtr, segPtr, byteOffset, wrapMode, spaceMode);

    ciPtr->numBytes = chunkPtr->numBytes;
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * CharDisplayProc --
 *
 *	This function is called to display a character chunk on the screen or
 *	in an off-screen pixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics are drawn.
 *
 *--------------------------------------------------------------
 */

static void
CharDisplayProc(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Chunk that is to be drawn. */
    int x,			/* X-position in dst at which to draw this chunk (may differ from
    				 * the x-position in the chunk because of scrolling). */
    int y,			/* Y-position at which to draw this chunk in dst. */
    int height,			/* Total height of line. */
    int baseline,		/* Offset of baseline from y. */
    Display *display,		/* Display to use for drawing. */
    Drawable dst,		/* Pixmap or window in which to draw chunk. */
    int screenY)		/* Y-coordinate in text window that corresponds to y. */
{
    if (chunkPtr->width > 0 && x + chunkPtr->width > 0) {
	/* The chunk has displayable content, and is not off-screen. */
	DisplayChars(textPtr, chunkPtr, x, y, baseline, display, dst);
    }
}

/*
 *--------------------------------------------------------------
 *
 * CharUndisplayProc --
 *
 *	This function is called when a character chunk is no longer going to
 *	be displayed. It frees up resources that were allocated to display the
 *	chunk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and other resources get freed.
 *
 *--------------------------------------------------------------
 */

static void
CharUndisplayProc(
    TkText *textPtr,		/* Overall information about text widget. */
    TkTextDispChunk *chunkPtr)	/* Chunk that is about to be freed. */
{
    CharInfo *ciPtr = chunkPtr->clientData;

    if (!ciPtr) {
	return;
    }

#if TK_LAYOUT_WITH_BASE_CHUNKS
    {
	TkTextDispChunk *baseChunkPtr = chunkPtr->baseChunkPtr;

	if (chunkPtr == baseChunkPtr) {
	    /*
	     * Base chunks are undisplayed first, when DLines are freed or
	     * partially freed, so this makes sure we don't access their data
	     * any more.
	     */

	    Tcl_DStringFree(&baseChunkPtr->baseChars);
	    DEBUG_ALLOC(tkTextCountDestroyBaseChars++);
	} else if (baseChunkPtr && ciPtr->numBytes > 0) {
	    /*
	     * When other char chunks are undisplayed, drop their characters
	     * from the base chunk. This usually happens, when they are last
	     * in a line and need to be re-layed out.
	     */

	    assert(ciPtr->baseOffset + ciPtr->numBytes == Tcl_DStringLength(&baseChunkPtr->baseChars));
	    Tcl_DStringSetLength(&baseChunkPtr->baseChars, ciPtr->baseOffset);
	    baseChunkPtr->baseWidth = 0;
	}

	if (chunkPtr->prevPtr) {
	    chunkPtr->x -= chunkPtr->prevPtr->xAdjustment;
	}

	chunkPtr->baseChunkPtr = NULL;
    }
#endif

    FreeCharInfo(textPtr, ciPtr);
    chunkPtr->clientData = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * HyphenUndisplayProc --
 *
 *	This function is called when a hyphen chunk is no longer going to
 *	be displayed. It frees up resources that were allocated to display the
 *	chunk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and other resources get freed.
 *
 *--------------------------------------------------------------
 */

static void
HyphenUndisplayProc(
    TkText *textPtr,		/* Overall information about text widget. */
    TkTextDispChunk *chunkPtr)	/* Chunk that is about to be freed. */
{
    TkTextSegment *hyphenPtr = chunkPtr->clientData;

    if (hyphenPtr) {
	TkBTreeFreeSegment(hyphenPtr);
    }
    chunkPtr->clientData = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DisplayChars --
 *
 *	This function is called to display characters on the screen or
 *	in an off-screen pixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics are drawn.
 *
 *--------------------------------------------------------------
 */

static GC
GetForegroundGC(
    const TkText *textPtr,
    const TkTextDispChunk *chunkPtr)
{
    const TkTextSegment *segPtr = ((const CharInfo *) chunkPtr->clientData)->segPtr;

    assert(chunkPtr->stylePtr);
    assert(chunkPtr->stylePtr->refCount > 0);

    if (segPtr->typePtr == &tkTextHyphenType) {
	if (chunkPtr->stylePtr->hyphenGC != None) {
	    return chunkPtr->stylePtr->hyphenGC;
	}
    } else if (segPtr == textPtr->dInfoPtr->endOfLineSegPtr) {
	if (chunkPtr->stylePtr->eolGC != None) {
	    return chunkPtr->stylePtr->eolGC;
	}
    } else if (segPtr == textPtr->dInfoPtr->endOfTextSegPtr) {
	if (chunkPtr->stylePtr->eotGC != None) {
	    return chunkPtr->stylePtr->eotGC;
	}
    }
    return chunkPtr->stylePtr->fgGC;
}

#if TK_DRAW_IN_CONTEXT
# if defined(_WIN32) || defined(__UNIX__)

/*****************************************************************************
 * We need this function for the emulation of context drawing, in this way the
 * context support can be pre-tested on platforms without sub-pixel accuracy.
 *****************************************************************************/

static void
DrawCharsInContext(
    Display *display,	/* Display on which to draw. */
    Drawable drawable,	/* Window or pixmap in which to draw. */
    GC gc,		/* Graphics context for drawing characters. */
    Tk_Font tkfont,	/* Font in which characters will be drawn; must be the same as font used
    			 * in GC. */
    const char *source,	/* UTF-8 string to be displayed. Need not be nul terminated. All Tk
    			 * meta-characters (tabs, control characters, and newlines) should be
			 * stripped out of the string that is passed to this function. If they are
			 * not stripped out, they will be displayed as regular printing characters. */
    int numBytes,	/* Number of bytes in string. */
    int rangeStart,	/* Index of first byte to draw. */
    int rangeLength,	/* Length of range to draw in bytes. */
    int x, int y,	/* Coordinates at which to place origin of the whole (not just the range)
    			 * string when drawing. */
    int xOffset)	/* Offset to x-coordinate, required for emulation of context drawing. */
{
    Tk_DrawChars(display, drawable, gc, tkfont, source + rangeStart, rangeLength, xOffset, y);
}

# else /* if !(defined(_WIN32) || defined(__UNIX__)) */

static void
DrawCharsInContext(
    Display *display,	/* Display on which to draw. */
    Drawable drawable,	/* Window or pixmap in which to draw. */
    GC gc,		/* Graphics context for drawing characters. */
    Tk_Font tkfont,	/* Font in which characters will be drawn; must be the same as font used
    			 * in GC. */
    const char *source,	/* UTF-8 string to be displayed. Need not be nul terminated. All Tk
    			 * meta-characters (tabs, control characters, and newlines) should be
			 * stripped out of the string that is passed to this function. If they are
			 * not stripped out, they will be displayed as regular printing characters. */
    int numBytes,	/* Number of bytes in string. */
    int rangeStart,	/* Index of first byte to draw. */
    int rangeLength,	/* Length of range to draw in bytes. */
    int x, int y,	/* Coordinates at which to place origin of the whole (not just the range)
    			 * string when drawing. */
    int xOffset)	/* Offset to x-coordinate, not needed here. */
{
    TkpDrawCharsInContext(display, drawable, gc, tkfont,
	    source, numBytes, rangeStart, rangeLength, x, y);
}

# endif /* defined(_WIN32) || defined(__UNIX__) */

static void
DrawChars(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Display the content of this chunk. */
    int x,			/* X-position in dst at which to draw. */
    int y,			/* Y-position at which to draw. */
    int offsetX,		/* Offset in x-direction. */
    int offsetBytes,		/* Offset in display string. */
    Display *display,		/* Display to use for drawing. */
    Drawable dst)		/* Pixmap or window in which to draw chunk. */
{
    const TkTextDispChunk *baseChunkPtr;
    int numBytes;

    assert(chunkPtr->baseChunkPtr);

    baseChunkPtr = chunkPtr->baseChunkPtr;
    numBytes = Tcl_DStringLength(&baseChunkPtr->baseChars);

    if (numBytes > offsetBytes) {
	const char *string;
	const CharInfo *ciPtr;
	const TextStyle *stylePtr;
	const StyleValues *sValuePtr;
	int xDisplacement, start, len;
	GC fgGC;

	string = Tcl_DStringValue(&baseChunkPtr->baseChars);
	ciPtr = chunkPtr->clientData;
	start = ciPtr->baseOffset + offsetBytes;
	len = ciPtr->numBytes - offsetBytes;

	assert(ciPtr->numBytes >= offsetBytes);

	if (len == 0 || (string[start + len - 1] == '\t' && --len == 0)) {
	    return;
	}

	stylePtr = chunkPtr->stylePtr;
	sValuePtr = stylePtr->sValuePtr;
	ciPtr = chunkPtr->clientData;
	xDisplacement = x - chunkPtr->x;
	fgGC = GetForegroundGC(textPtr, chunkPtr);

	/*
	 * Draw the text, underline, and overstrike for this chunk.
	 */

	DrawCharsInContext(display, dst, fgGC, sValuePtr->tkfont, string, numBytes,
		start, len, baseChunkPtr->x + xDisplacement, y - sValuePtr->offset,
		chunkPtr->x + textPtr->dInfoPtr->x);

	if (sValuePtr->underline) {
	    TkUnderlineCharsInContext(display, dst, stylePtr->ulGC, sValuePtr->tkfont, string,
		    numBytes, baseChunkPtr->x + xDisplacement, y - sValuePtr->offset,
		    start, start + len);
	}
	if (sValuePtr->overstrike) {
	    Tk_FontMetrics fm;

	    Tk_GetFontMetrics(sValuePtr->tkfont, &fm);
	    TkUnderlineCharsInContext(display, dst, stylePtr->ovGC, sValuePtr->tkfont, string,
		    numBytes, baseChunkPtr->x + xDisplacement,
		    y - sValuePtr->offset - fm.descent - (fm.ascent*3)/10,
		    start, start + len);
	}
    }
}

#else /* if !TK_DRAW_IN_CONTEXT */

static void
DrawChars(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Display the content of this chunk. */
    int x,			/* X-position in dst at which to draw. */
    int y,			/* Y-position at which to draw. */
    int offsetX,		/* Offset from x. */
    int offsetBytes,		/* Offset in display string. */
    Display *display,		/* Display to use for drawing. */
    Drawable dst)		/* Pixmap or window in which to draw chunk. */
{
    const CharInfo *ciPtr;
    int numBytes;

    ciPtr = chunkPtr->clientData;
    numBytes = ciPtr->numBytes;

    assert(offsetBytes >= ciPtr->baseOffset);

    if (numBytes > offsetBytes) {
	const TextStyle *stylePtr = chunkPtr->stylePtr;

	if (stylePtr->fgGC != None) {
	    const StyleValues *sValuePtr;
	    const char *string;
	    GC fgGC;

	    string = ciPtr->u.chars + offsetBytes;
	    numBytes -= offsetBytes;

	    if (string[numBytes - 1] == '\t' && --numBytes == 0) {
		return;
	    }

	    sValuePtr = stylePtr->sValuePtr;
	    fgGC = GetForegroundGC(textPtr, chunkPtr);

	    /*
	     * Draw the text, underline, and overstrike for this chunk.
	     */

	    Tk_DrawChars(display, dst, fgGC, sValuePtr->tkfont, string, numBytes,
		    offsetX, y - sValuePtr->offset);
	    if (sValuePtr->underline) {
		Tk_UnderlineChars(display, dst, stylePtr->ulGC, sValuePtr->tkfont,
			string, offsetX, y - sValuePtr->offset, 0, numBytes);

	    }
	    if (sValuePtr->overstrike) {
		Tk_FontMetrics fm;

		Tk_GetFontMetrics(sValuePtr->tkfont, &fm);
		Tk_UnderlineChars(display, dst, stylePtr->ovGC, sValuePtr->tkfont, string, offsetX,
			y - sValuePtr->offset - fm.descent - (fm.ascent*3)/10, 0, numBytes);
	    }
	}
    }
}

#endif /* TK_DRAW_IN_CONTEXT */

static void
DisplayChars(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Display the content of this chunk. */
    int x,			/* X-position in dst at which to draw. */
    int y,			/* Y-position at which to draw. */
    int baseline,		/* Offset of baseline from y. */
    Display *display,		/* Display to use for drawing. */
    Drawable dst)		/* Pixmap or window in which to draw chunk. */
{
    const TextStyle *stylePtr = chunkPtr->stylePtr;
    int offsetBytes, offsetX;

    assert(!stylePtr->sValuePtr->elide);

    if (stylePtr->fgGC == None) {
	return;
    }

    /*
     * If the text sticks out way to the left of the window, skip over the
     * characters that aren't in the visible part of the window. This is
     * essential if x is very negative (such as less than 32K); otherwise
     * overflow problems will occur in servers that use 16-bit arithmetic,
     * like X.
     */

    offsetX = x;
    offsetBytes = (x >= 0) ? CharChunkMeasureChars(chunkPtr, NULL, 0, 0, -1, x, 0, 0, &offsetX) : 0;
    DrawChars(textPtr, chunkPtr, x, y + baseline, offsetX, offsetBytes, display, dst);
}

#ifndef NDEBUG
/*
 *--------------------------------------------------------------
 *
 * TkpTextPrintDispChunk --
 *
 *	This function is for debugging only, printing the content of
 *	the given tag display chunk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

void
TkpTextPrintDispChunk(
    const TkText *textPtr,
    const TkTextDispChunk *chunkPtr)
{
    const DLine *dlPtr;
    int x, y, width, height;

    switch (chunkPtr->layoutProcs->type) {
    case TEXT_DISP_CHAR:
	fprintf(stdout, "CHAR=");
	if (chunkPtr->clientData) {
	    const CharInfo *ciPtr = (const CharInfo *) chunkPtr->clientData;
	    int i;

	    for (i = 0; i < ciPtr->numBytes; ++i) {
		char c = ciPtr->u.chars[i];

		switch (c) {
		case '\t': fprintf(stdout, "\\t"); break;
		case '\n': fprintf(stdout, "\\n"); break;
		case '\v': fprintf(stdout, "\\v"); break;
		case '\f': fprintf(stdout, "\\f"); break;
		case '\r': fprintf(stdout, "\\r"); break;

		default:
		    if (UCHAR(c) < 0x80 && isprint(c)) {
			fprintf(stdout, "%c", c);
		    } else {
			fprintf(stdout, "\\x%02u", (unsigned) UCHAR(c));
		    }
		    break;
		}
	    }
	} else {
	    fprintf(stdout, "<not yet displayed>");
	}
	break;

    case TEXT_DISP_HYPHEN: fprintf(stdout, "HYPHEN"); break;
    case TEXT_DISP_IMAGE:  fprintf(stdout, "IMAGE"); break;
    case TEXT_DISP_WINDOW: fprintf(stdout, "WINDOW"); break;
    case TEXT_DISP_ELIDED: fprintf(stdout, "ELIDED"); break;
    case TEXT_DISP_CURSOR: fprintf(stdout, "CURSOR"); break;
    }

    dlPtr = chunkPtr->dlPtr;
    x = chunkPtr->x + textPtr->dInfoPtr->x;
    y = dlPtr->y + dlPtr->spaceAbove;
    width = chunkPtr->width;
    height = dlPtr->height - dlPtr->spaceAbove - dlPtr->spaceBelow;
    fprintf(stdout, " [%d,%d-%d,%d]\n", x, y, x + width, y + height);
}
#endif /* !NDEBUG */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
