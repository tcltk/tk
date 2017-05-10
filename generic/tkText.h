/*
 * tkText.h --
 *
 *	Declarations shared among the files that implement text widgets.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1995 Sun Microsystems, Inc.
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXT
#define _TKTEXT

#ifndef _TK
# include "tk.h"
#endif

#ifndef _TKINT
# include "tkInt.h"
#endif

#include "tkTextUndo.h"
#include "tkBool.h"

#ifdef MAC_OSX_TK
/* required for TK_LAYOUT_WITH_BASE_CHUNKS */
# include "tkMacOSXInt.h"
#endif

#ifdef BUILD_tk
# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLEXPORT
#endif

#if TK_CHECK_ALLOCS
# define DEBUG_ALLOC(expr) expr
#else
# define DEBUG_ALLOC(expr)
#endif

#if TK_MAJOR_VERSION < 9

/* We are still supporting the deprecated -startline/-endline options. */
# define SUPPORT_DEPRECATED_STARTLINE_ENDLINE 1

/* We are still supporting invalid changes in readonly/disabled widgets. */
# define SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET 1

/*
 * The special index identifier "begin" currently has the lowest precedence,
 * because of portability reasons. But in a future Tk version it should have
 * the same precedence as the special index identifier "end".
 */
# define BEGIN_DOES_NOT_BELONG_TO_BASE 1

/* We are still supporting deprecated tag options. */
# define SUPPORT_DEPRECATED_TAG_OPTIONS 1

/* We are still supporting the deprecated commands "edit canundo/redo". */
# define SUPPORT_DEPRECATED_CANUNDO_REDO 1

#endif /* TK_MAJOR_VERSION < 9 */

#ifdef _MSC_VER
/* earlier versions of MSVC don't know snprintf, but _snprintf is compatible. */
# define snprintf _snprintf
#endif

/*
 * Forward declarations.
 */

struct TkTextUndoStack;
struct TkBitField;
struct TkTextUndoToken;
union TkTextTagSet;

/* We need a backport to version 8.5 */
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION == 5
typedef struct TkTextBTree_ *TkTextBTree;
#endif

/*
 * The data structure below defines the pixel information for a single line of text.
 */

typedef struct TkTextDispLineEntry {
    uint32_t height;		/* Height of display line in pixels. */
    uint32_t pixels;		/* Accumulated height of display lines. In last entry this attribute
    				 * will contain the old number of display lines. */
    uint32_t byteOffset:24;	/* Byte offet relative to logical line. */
    uint32_t hyphenRule:8;	/* Hyphenation rule applied to last char chunk of this display line. */
} TkTextDispLineEntry;

typedef struct TkTextDispLineInfo {
    uint32_t numDispLines;	/* Number of display lines (so far). */
    TkTextDispLineEntry entry[1];
				/* Cached information about the display line, this is required for
				 * long lines to avoid repeated display line height calculations
				 * when scrolling. If numDispLines <= 1 then this information is
				 * NULL, in this case attribute height of TkTextPixelInfo is
				 * identical to the pixel height of the (single) display line. */
} TkTextDispLineInfo;

typedef struct TkTextPixelInfo {
    uint32_t height;		/* Number of vertical pixels taken up by this line, whether
    				 * currently displayed or not. This number is only updated
				 * asychronously. Note that this number is the sum of
				 * dispLineInfo, but only when dispLineInfo != NULL. */
    uint32_t epoch;		/* Last epoch at which the pixel height was recalculated. */
    TkTextDispLineInfo *dispLineInfo;
				/* Pixel information for each display line, available only
     				 * if more than one display line exists, otherwise it is NULL. */
} TkTextPixelInfo;

/*
 * The data structure below defines a single logical line of text (from
 * newline to newline, not necessarily what appears on one display line of the
 * screen).
 */

struct TkTextSegment;

typedef struct TkTextLine {
    struct Node *parentPtr;	/* Pointer to parent node containing line. */
    struct TkTextLine *nextPtr;	/* Next in linked list of lines with same parent node in B-tree.
    				 * NULL means end of list. */
    struct TkTextLine *prevPtr;	/* Previous in linked list of lines. NULL means no predecessor. */
    struct TkTextSegment *segPtr;
				/* First in ordered list of segments that make up the line. */
    struct TkTextSegment *lastPtr;
				/* Last in ordered list of segments that make up the line. */
    union TkTextTagSet *tagonPtr;
				/* This set contains all tags used in this line. */
    union TkTextTagSet *tagoffPtr;
    				/* This set contains tag t if and only if at least one segment in
				 * this line does not use tag t, provided that tag t is also included
				 * in tagonPtr. */
    TkTextPixelInfo *pixelInfo;	/* Array containing the pixel information for each referring
    				 * text widget. */
    int32_t size;		/* Sum of sizes over all segments belonging to this line. */
    uint32_t numBranches;	/* Counting the number of branches on this line. Only count branches
    				 * connected with links, do not count branches pointing to a mark. */
    uint32_t numLinks:30;	/* Counting the number of links on this line. */
    uint32_t changed:1;		/* Will be set when the content of this logical line has changed. The
    				 * display stuff will use (and reset) this flag, but only for logical
				 * lines. The purpose of this flag is the acceleration of the line
				 * break information. */
    uint32_t logicalLine:1;	/* Flag whether this is the start of a logical line. */
} TkTextLine;

/*
 * -----------------------------------------------------------------------
 * Index structure containing line and byte index.
 * -----------------------------------------------------------------------
 */

typedef struct TkTextPosition {
    int32_t lineIndex;
    int32_t byteIndex;
} TkTextPosition;

/*
 * -----------------------------------------------------------------------
 * Structures for undo/redo mechanism.
 *
 * Note that TkTextUndoToken is a generic type, used as a base struct for
 * inheritance. Inheritance in C is portable due to C99 section 6.7.2.1
 * bullet point 13:
 *
 *	Within a structure object, the non-bit-field members and the units
 *	in which bit-fields reside have addresses that increase in the order
 *	in which they are declared. A pointer to a structure object, suitably
 *	converted, points to its initial member (or if that member is a
 *	bit-field, then to the unit in which it resides), and vice versa.
 *	There may be unnamed padding within a structure object, but not at
 *	beginning.
 *
 * This inheritance concept is also used in the portable GTK library.
 * -----------------------------------------------------------------------
 */

/* we need some forward declarations */
struct TkSharedText;
struct TkTextIndex;
struct TkTextUndoInfo;
struct TkTextUndoIndex;

typedef enum {
    TK_TEXT_UNDO_INSERT,	TK_TEXT_REDO_INSERT,
    TK_TEXT_UNDO_DELETE,	TK_TEXT_REDO_DELETE,
    TK_TEXT_UNDO_IMAGE,		TK_TEXT_REDO_IMAGE,
    TK_TEXT_UNDO_WINDOW,	TK_TEXT_REDO_WINDOW,
    TK_TEXT_UNDO_TAG,		TK_TEXT_REDO_TAG,
    TK_TEXT_UNDO_TAG_CLEAR,	TK_TEXT_REDO_TAG_CLEAR,
    TK_TEXT_UNDO_TAG_PRIORITY,	TK_TEXT_REDO_TAG_PRIORITY,
    TK_TEXT_UNDO_MARK_SET,	TK_TEXT_REDO_MARK_SET,
    TK_TEXT_UNDO_MARK_MOVE,	TK_TEXT_REDO_MARK_MOVE,
    TK_TEXT_UNDO_MARK_GRAVITY,	TK_TEXT_REDO_MARK_GRAVITY
} TkTextUndoAction;

typedef Tcl_Obj *(*TkTextGetUndoCommandProc)(
    const struct TkSharedText *sharedTextPtr,
    const struct TkTextUndoToken *item);

typedef void (*TkTextUndoProc)(
    struct TkSharedText *sharedTextPtr,
    struct TkTextUndoInfo *undoInfo,
    struct TkTextUndoInfo *redoInfo,
    bool isRedo);

typedef void (*TkTextDestroyUndoItemProc)(
    struct TkSharedText *sharedTextPtr,
    struct TkTextUndoToken *item,
    bool reused);

typedef void (*TkTextGetUndoRangeProc)(
    const struct TkSharedText *sharedTextPtr,
    const struct TkTextUndoToken *item,
    struct TkTextIndex *startIndex,
    struct TkTextIndex *endIndex);

typedef Tcl_Obj *(*TkTextInspectProc)(
    const struct TkSharedText *sharedTextPtr,
    const struct TkTextUndoToken *item);

typedef struct Tk_UndoType {
    TkTextUndoAction action;
    TkTextGetUndoCommandProc commandProc;	/* mandatory */
    TkTextUndoProc undoProc;			/* mandatory */
    TkTextDestroyUndoItemProc destroyProc;	/* optional */
    TkTextGetUndoRangeProc rangeProc;		/* mandatory */
    TkTextInspectProc inspectProc;		/* mandatory */
} Tk_UndoType;

/*
 * The struct below either contains a mark segment or a line/byte index pair.
 * This struct is portable due to C99 7.18.1.4.
 */

typedef struct TkTextUndoIndex {
    union {
	struct TkTextSegment *markPtr;	/* Predecessor/successor segment. */
	uintptr_t byteIndex;		/* Byte index in this line. */
    } u;
    int32_t lineIndex;	/* Line index, if -1 this struct contains a mark segment, otherwise
    			 * (if >= 0) this struct contains a line/byte index pair. */
} TkTextUndoIndex;

/*
 * This is a generic type, any "derived" struct must contain
 * 'const Tk_UndoType *' as the first member (see note above).
 */
typedef struct TkTextUndoToken {
    const Tk_UndoType *undoType;
} TkTextUndoToken;

/*
 * This is a generic type, any "derived" struct must also contain
 * these members. Especially the tokens for insert/delete must be
 * derived from this struct.
 */
typedef struct TkTextUndoTokenRange {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;
    TkTextUndoIndex endIndex;
} TkTextUndoTokenRange;

typedef struct TkTextUndoInfo {
    TkTextUndoToken *token;	/* The data of this undo/redo item. */
    uint32_t byteSize;		/* Byte size of this item. */
} TkTextUndoInfo;

/*
 * -----------------------------------------------------------------------
 * Segments: each line is divided into one or more segments, where each
 * segment is one of several things, such as a group of characters, a hyphen,
 * a mark, or an embedded widget. Each segment starts with a standard
 * header followed by a body that varies from type to type.
 * -----------------------------------------------------------------------
 */

/*
 * The data structure below defines the body of a segment that represents
 * a branch. A branch is adjusting the chain of segments, depending whether
 * elidden text will be processed or not.
 */

typedef struct TkTextBranch {
    struct TkTextSegment *nextPtr; /* The next in list of segments for the alternative branch. */
} TkTextBranch;

/*
 * The data structure below defines the body of a segment that represents
 * a link. A link is the connection point for a segment chain of elidden
 * text.
 */

typedef struct TkTextLink {
    struct TkTextSegment *prevPtr; /* Previous in list of segments for the alternative branch. */
} TkTextLink;

/*
 * The data structure below defines line segments that represent marks. There
 * is one of these for each mark in the text.
 */

typedef struct TkTextMarkChange {
    struct TkTextSegment *markPtr;
    				/* Pointer to mark segment which contains this item. */
    struct TkTextUndoToken *toggleGravity;
    				/* Undo token for "mark gravity". */
    struct TkTextUndoToken *moveMark;
    				/* Undo token for "mark move". */
    struct TkTextUndoToken *setMark;
    				/* Undo token for "mark set". */
    const struct Tk_SegType *savedMarkType;
    				/* Type of mark (left or right gravity) before "mark gravity". We
				 * need this information for optimizing succeeding calls of
				 * "mark gravity" with same mark. */
} TkTextMarkChange;

typedef struct TkTextMark {
    struct TkText *textPtr;	/* Overall information about text widget. */
    uintptr_t ptr;		/* [Address is even - real type is Tcl_HashEntry *]
				 *   Pointer to hash table entry for mark (in sharedTextPtr->markTable).
				 * [Address is odd - real type is const char *]
				 *   Name of mark, this is used iff the segment is preserved for undo. */
    TkTextMarkChange *changePtr;/* Pointer to retained undo tokens. */
} TkTextMark;

/*
 * A structure of the following type holds information for each window
 * embedded in a text widget. This information is only used by the file
 * tkTextWind.c
 */

typedef struct TkTextEmbWindowClient {
    struct TkText *textPtr;	/* Information about the overall text widget. */
    Tk_Window tkwin;		/* Window for this segment. NULL means that the window
    				 * hasn't been created yet. */
    Tcl_HashEntry *hPtr;	/* Pointer to hash table entry for mark
				 * (in sharedTextPtr->windowTable). */
    unsigned chunkCount;	/* Number of display chunks that refer to this window. */
    bool displayed;		/* Non-zero means that the window has been displayed on
    				 * the screen recently. */
    struct TkTextSegment *parent;
    struct TkTextEmbWindowClient *next;
} TkTextEmbWindowClient;

typedef struct TkTextEmbWindow {
    struct TkSharedText *sharedTextPtr;
				/* Information about the shared portion of the text widget. */
    Tk_Window tkwin;		/* Window for this segment. This is just a temporary value,
    				 * copied from 'clients', to make option table updating easier.
				 * NULL means that the window hasn't been created yet. */
    char *create;		/* Script to create window on-demand. NULL means no such script.
    				 * Malloc-ed. */
    int align;			/* How to align window in vertical space. See definitions in
    				 * tkTextWind.c. */
    int padX, padY;		/* Padding to leave around each side of window, in pixels. */
    bool stretch;		/* Should window stretch to fill vertical space of line
    				 * (except for pady)? */
    bool isOwner;		/* Should destroy the window when un-embed? This will only be
				 * done if the text widget is the owner. Default is true
				 * (this is compatible to older versions). */
    Tk_OptionTable optionTable;	/* Token representing the configuration specifications. */
    TkTextEmbWindowClient *clients;
				/* Linked list of peer-widget specific information for this
				 * embedded window. */
} TkTextEmbWindow;

/*
 * A structure of the following type holds information for each image embedded
 * in a text widget. This information is only used by the file tkTextImage.c
 */

typedef struct TkTextEmbImage {
    struct TkSharedText *sharedTextPtr;
				/* Information about the shared portion of the text widget.
				 * This is used when the image changes or is deleted. */
    char *imageString;		/* Name of the image for this segment. */
    char *imageName;		/* Name used by text widget to identify this image.
    				 * May be unique-ified. */
    char *name;			/* Name used in the hash table. Used by "image names" to
    				 * identify this instance of the image. */
    Tk_Image image;		/* Image for this segment. NULL means that the image hasn't
    				 * been created yet. */
    int imgWidth;		/* Width of displayed image. */
    int imgHeight;		/* Height of displayed image. */
    Tcl_HashEntry *hPtr;	/* Pointer to hash table entry for image
    				 * (in sharedTextPtr->imageTable).*/
    int align;			/* How to align image in vertical space. See definitions in
    				 * tkTextImage.c. */
    int padX, padY;		/* Padding to leave around each side of image, in pixels. */
    Tk_OptionTable optionTable;	/* Token representing the configuration specifications. */
} TkTextEmbImage;

/*
 * A structure of the following type is for the definition of the hyphenation
 * segments. Note that this structure is a derivation of a char segment. This
 * is portable due to C99 section 6.7.2.1 (see above - undo/redo).
 */

typedef struct TkTextHyphen {
    char chars[6];		/* Characters that make up character info. Actual length varies. */
    int8_t textSize;		/* Actual length of text, but not greater than 5. */
    int8_t rules;		/* Set of rules for this hyphen, will be set by tk_textInsert. */
} TkTextHyphen;

/*
 * The data structure below defines line segments.
 */

typedef struct TkTextSegment {
    const struct Tk_SegType *typePtr;
				/* Pointer to record describing segment's type. */
    struct TkTextSegment *nextPtr;
				/* Next in list of segments for this line, or NULL for end of list. */
    struct TkTextSegment *prevPtr;
				/* Previous in list of segments for this line, or NULL for start
				 * of list. */
    struct TkTextSection *sectionPtr;
    				/* The section where this segment belongs. */
    union TkTextTagSet *tagInfoPtr;
				/* Tag information for this segment, needed for testing whether
				 * this content is tagged with a specific tag. Used only if size > 0.
				 * (In case of segments with size == 0 memory will be wasted - these
				 * segments do not need this attribute - but the alternative would be
				 * a quite complex structure with some nested structs and unions, and
				 * this is quite inconvenient. Only marks and branches/links do not
				 * use this information, so the waste of memory is relatively low.) */
    int32_t size;		/* Size of this segment (# of bytes of index space it occupies). */
    uint32_t refCount:26;	/* Reference counter, don't delete until counter is zero, or
    				 * tree is gone. */
    uint32_t protectionFlag:1;	/* This (char) segment is protected, join is not allowed. */
    uint32_t insertMarkFlag:1;	/* This segment is the special "insert" mark? */
    uint32_t currentMarkFlag:1;	/* This segment is the special "current" mark? */
    uint32_t privateMarkFlag:1;	/* This mark segment is private (generated)? */
    uint32_t normalMarkFlag:1;	/* This mark segment is neither protected, nor special, nor private,
    				 * nor start or end marker. */
    uint32_t startEndMarkFlag:1;/* This segment is a start marker or an end marker? */

    union {
	char chars[1];		/* Characters that make up character info. Actual length varies
				 * to hold as many characters as needed. */
	TkTextHyphen hyphen;	/* Information about hyphen. */
	TkTextEmbWindow ew;	/* Information about embedded window. */
	TkTextEmbImage ei;	/* Information about embedded image. */
	TkTextMark mark;	/* Information about mark. */
	TkTextBranch branch;	/* Information about branch. */
	TkTextLink link;	/* Information about link. */
    } body;
} TkTextSegment;

/*
 * Macro that determines how much space to allocate for a specific segment:
 */

#define SEG_SIZE(bodyType) ((unsigned) (Tk_Offset(TkTextSegment, body) + sizeof(bodyType)))

/*
 * The data structure below defines sections of text segments. Each section
 * contains 40 line segments in average. In this way fast search for segments
 * is possible.
 */
typedef struct TkTextSection {
    struct TkTextLine *linePtr;	/* The line where this section belongs. */
    struct TkTextSection *nextPtr;
    				/* Next in list of sections, or NULL if last. */
    struct TkTextSection *prevPtr;
    				/* Previous in list of sections, or NULL if first. */
    struct TkTextSegment *segPtr;
    				/* First segment belonging to this section. */
    int32_t size:24;		/* Sum of size over all segments belonging to this section. */
    uint32_t length:8;		/* Number of segments belonging to this section. */
} TkTextSection;

/*
 * Data structures of the type defined below are used during the execution of
 * Tcl commands to keep track of various interesting places in a text. An
 * index is only valid up until the next modification to the character
 * structure of the b-tree so they can't be retained across Tcl commands.
 * However, mods to marks or tags don't invalidate indices.
 */

typedef struct TkTextIndex {
    TkTextBTree tree;		/* Tree containing desired position. */
    struct TkText *textPtr;	/* The associated text widget (required). */
    uint32_t stateEpoch;	/* The epoch of the segment pointer. */

    /*
     * The following attribtes should not be accessed directly, use the TkTextIndex*
     * functions if you want to set or get attributes.
     */

    struct {
	TkTextLine *linePtr;	/* Pointer to line containing position of interest. */
	TkTextSegment *segPtr;	/* Pointer to segment containing position
				 * of interest (NULL means not yet computed). */
	bool isCharSegment;	/* Whether 'segPtr' is a char segment (if not NULL). */
	int32_t byteIndex;	/* Index within line of desired character (0 means first one,
				 * -1 means not yet computed). */
	int32_t lineNo;		/* The line number of the line pointer. */
	int32_t lineNoRel;	/* The line number of the line pointer in associated text widget. */
    } priv;

    bool discardConsistencyCheck;
				/* This flag is for debugging only: in certain situations consistency
				 * checks should not be done (for example when inserting or deleting
				 * text). */
} TkTextIndex;

/*
 * Types for procedure pointers stored in TkTextDispChunk strutures:
 */

typedef struct TkTextDispChunk TkTextDispChunk;

typedef void 	Tk_ChunkDisplayProc(struct TkText *textPtr, TkTextDispChunk *chunkPtr,
		    int x, int y, int height, int baseline, Display *display, Drawable dst,
		    int screenY);
typedef void	Tk_ChunkUndisplayProc(struct TkText *textPtr, TkTextDispChunk *chunkPtr);
typedef int	Tk_ChunkMeasureProc(TkTextDispChunk *chunkPtr, int x);
typedef void	Tk_ChunkBboxProc(struct TkText *textPtr, TkTextDispChunk *chunkPtr,
		    int index, int y, int lineHeight, int baseline, int *xPtr, int *yPtr,
		    int *widthPtr, int *heightPtr);

/*
 * The structure below represents a chunk of stuff that is displayed together
 * on the screen. This structure is allocated and freed by generic display
 * code but most of its fields are filled in by segment-type-specific code.
 */

typedef enum {
    TEXT_DISP_CHAR   = 1 << 0, /* Character layout */
    TEXT_DISP_HYPHEN = 1 << 1, /* Hyphen layout */
    TEXT_DISP_ELIDED = 1 << 2, /* Elided content layout */
    TEXT_DISP_WINDOW = 1 << 3, /* Embedded window layout */
    TEXT_DISP_IMAGE  = 1 << 4, /* Embedded image layout */
    TEXT_DISP_CURSOR = 1 << 5, /* Insert cursor layout */
} TkTextDispType;

/* This constant can be used for a test whether the chunk has any content. */
#define TEXT_DISP_CONTENT (TEXT_DISP_CHAR|TEXT_DISP_HYPHEN|TEXT_DISP_WINDOW|TEXT_DISP_IMAGE)
/* This constant can be used for a test whether the chunk contains text. */
#define TEXT_DISP_TEXT    (TEXT_DISP_CHAR|TEXT_DISP_HYPHEN)

typedef struct TkTextDispChunkProcs {
    TkTextDispType type;	/* Layout type. */
    Tk_ChunkDisplayProc *displayProc;
				/* Procedure to invoke to draw this chunk on the display
				 * or an off-screen pixmap. */
    Tk_ChunkUndisplayProc *undisplayProc;
				/* Procedure to invoke when segment ceases to be displayed
				 * on screen anymore. */
    Tk_ChunkMeasureProc *measureProc;
				/* Procedure to find character under a given x-location. */
    Tk_ChunkBboxProc *bboxProc;	/* Procedure to find bounding box of character in chunk. */
} TkTextDispChunkProcs;

struct TkTextDispChunk {
    /*
     * The fields below are set by the type-independent code before calling
     * the segment-type-specific layoutProc. They should not be modified by
     * segment-type-specific code.
     */

    const struct TkTextDispLine *dlPtr;
    				/* Pointer to display line of this chunk. We need this for the retrieval
				 * of the y position.
				 */
    struct TkTextDispChunk *nextPtr;
				/* Next chunk in the display line or NULL for the end of the list. */
    struct TkTextDispChunk *prevPtr;
				/* Previous chunk in the display line or NULL for the start of the
				 * list. */
    struct TkTextDispChunk *prevCharChunkPtr;
				/* Previous char chunk in the display line, or NULL. */
    struct TkTextDispChunkSection *sectionPtr;
    				/* The section of this chunk. The section structure allows fast search
				 * for x positions, and character positions. */
    struct TextStyle *stylePtr;	/* Display information, known only to tkTextDisp.c. This attribute
    				 * is set iff the associated segment has tag information AND is not
				 * elided. */
    unsigned uniqID;		/* Unique identifier for this chunk, used by TkTextPickCurrent. */

    /*
     * The fields below are set by the layoutProc that creates the chunk.
     */

    const TkTextDispChunkProcs *layoutProcs;
    const char *brks;		/* Line break information of this chunk for TEXT_WRAPMODE_CODEPOINT. */
    ClientData clientData;	/* Additional information for use of displayProc and undisplayProc. */

    /*
     * The fields below are set by the type-independent code before calling
     * the segment-type-specific layoutProc. They should not be modified by
     * segment-type-specific code.
     */

    int32_t x;			/* X position of chunk, in pixels. This position is measured
    				 * from the left edge of the logical line, not from the left
				 * edge of the window (i.e. it doesn't change under horizontal
				 * scrolling). */

    /*
     * The fields below are set by the layoutProc that creates the chunk.
     */

    uint32_t byteOffset;	/* Byte offset relative to display line start. */
    uint32_t numBytes;		/* Number of bytes that will be used in the chunk. */
    uint32_t numSpaces;		/* Number of expandable spaces. */
    uint32_t segByteOffset;	/* Starting offset in corresponding char segment. */
    int32_t minAscent;		/* Minimum space above the baseline needed by this chunk. */
    int32_t minDescent;		/* Minimum space below the baseline needed by this chunk. */
    int32_t minHeight;		/* Minimum total line height needed by this chunk. */
    int32_t width;		/* Width of this chunk, in pixels. Initially set by
    				 * chunk-specific code, but may be increased to include tab
				 * or extra space at end of line. */
    int32_t additionalWidth;	/* Additional width when expanding spaces for full justification. */
    int32_t hyphenRules;	/* Allowed hyphenation rules for this (hyphen) chunk. */
    int32_t breakIndex:28;	/* Index within chunk of last acceptable position for a line
    				 * (break just before this byte index). <= 0 means don't break
				 * during or immediately after this chunk. */
    uint32_t wrappedAtSpace:1;	/* This flag will be set when the a chunk has been wrapped while
    				 * gobbling a trailing space. */
    uint32_t endsWithSyllable:1;/* This flag will be set when the corresponding sgement for
    				 * this chunk will be followed by a hyphen segment. */
    uint32_t skipFirstChar:1;	/* This flag will be set if the first byte has to be skipped due
    				 * to a spelling change. */
    uint32_t endOfLineSymbol:1;	/* This flag will be set if this chunk contains (only) the end of
    				 * line symbol. */

#if TK_LAYOUT_WITH_BASE_CHUNKS

    /*
     * Support of context drawing (Mac):
     */

    Tcl_DString baseChars;	/* Actual characters for the stretch of text, only defined in
    				 * base chunk. */
    int32_t baseWidth;		/* Width in pixels of the whole string, if known, else 0. */
    int32_t xAdjustment;	/* Adjustment of x-coord for next chunk. */
    struct TkTextDispChunk *baseChunkPtr;
    				/* Points to base chunk. */

#endif /* TK_LAYOUT_WITH_BASE_CHUNKS */
};

/*
 * The following structure describes one line of the display, which may be
 * either part or all of one line of the text. This structure will be defined
 * here because we want to provide inlined functions.
 */

typedef struct TkTextDispLine {
    TkTextIndex index;		/* Identifies first character in text that is displayed on this line. */
    struct TkTextDispLine *nextPtr;
    				/* Next in list of all display lines for this window. The list is
    				 * sorted in order from top to bottom. Note: the next DLine doesn't
				 * always correspond to the next line of text: (a) can have multiple
				 * DLines for one text line (wrapping), (b) can have elided newlines,
				 * and (c) can have gaps where DLine's have been deleted because
				 * they're out of date. */
    struct TkTextDispLine *prevPtr;
    				/* Previous in list of all display lines for this window. */
    TkTextDispChunk *chunkPtr;	/* Pointer to first chunk in list of all of those that are displayed
    				 * on this line of the screen. */
    TkTextDispChunk *firstCharChunkPtr;
    				/* Pointer to first chunk in list containing chars, window, or image. */
    TkTextDispChunk *lastChunkPtr;
    				/* Pointer to last chunk in list containing chars. */
    TkTextDispChunk *cursorChunkPtr;
    				/* Pointer to chunk which displays the insert cursor. */
    struct TkTextBreakInfo *breakInfo;
    				/* Line break information of logical line. */
    uint32_t displayLineNo;	/* The number of this display line relative to the related logical
    				 * line. */
    uint32_t hyphenRule;	/* Hyphenation rule applied to last char chunk (only if hyphenation
    				 * has been applied). */
    uint32_t byteCount;		/* Number of bytes accounted for by this display line, including a
    				 * trailing space or newline that isn't actually displayed. */
    bool invisible;		/* Whether this display line is invisible (no chunk with width > 0). */
    int32_t y;			/* Y-position at which line is supposed to be drawn (topmost pixel
    				 * of rectangular area occupied by line). */
    int32_t oldY;		/* Y-position at which line currently appears on display. This is
    				 * used to move lines by scrolling rather than re-drawing. If 'flags'
				 * have the OLD_Y_INVALID bit set, then we will never examine this
				 * field (which means line isn't currently visible on display and
				 * must be redrawn). */
    int32_t width;		/* Width of line, in pixels, not including any indent. */
    int32_t height;		/* Height of line, in pixels. */
    int32_t baseline;		/* Offset of text baseline from y, in pixels. */
    int32_t spaceAbove;		/* How much extra space was added to the top of the line because of
    				 * spacing options. This is included in height and baseline. */
    int32_t spaceBelow;		/* How much extra space was added to the bottom of the line because
    				 * of spacing options. This is included in height. */
    uint32_t length;		/* Total length of line, in pixels, including possible left indent. */
    uint32_t flags;		/* Various flag bits: see below for values. */
} TkTextDispLine;

/*
 * One data structure of the following type is used for each tag in a text
 * widget. These structures are kept in sharedTextPtr->tagTable and referred
 * to in other structures.
 */

typedef enum {
    TEXT_WRAPMODE_CHAR,
    TEXT_WRAPMODE_NONE,
    TEXT_WRAPMODE_WORD,
    TEXT_WRAPMODE_CODEPOINT,
    TEXT_WRAPMODE_NULL
} TkWrapMode;

/*
 * The spacing mode of the text widget:
 */

typedef enum {
    TEXT_SPACEMODE_NONE,
    TEXT_SPACEMODE_EXACT,
    TEXT_SPACEMODE_TRIM,
    TEXT_SPACEMODE_NULL,
} TkTextSpaceMode;

/*
 * The justification modes:
 */

typedef enum {
    TK_TEXT_JUSTIFY_LEFT,	/* The text is aligned along the left margin. This is the default. */
    TK_TEXT_JUSTIFY_RIGHT,	/* The text is aligned along the right margin. */
    TK_TEXT_JUSTIFY_FULL,	/* The text is aligned along the left margin, and word-spacing is
    				 * adjusted so that the text flush with both margins. */
    TK_TEXT_JUSTIFY_CENTER	/* The text is aligned to neither the left nor the right margin,
    				 * there is an even gap on each side of each line. */
} TkTextJustify;

/*
 * If the soft hyphen is the right neighbor of character "c", and the right neighbor is character
 * "k", then the ck hyphenation rule will be applied.
 */
#define TK_TEXT_HYPHEN_CK		1 /* de */
/*
 * Hungarian has an unusual hyphenation case which involves reinsertion of a root-letter, as in the
 * following example: "vissza" becomes "visz-sza". These special cases, occurring when the characters
 * are in the middle of a word are: "ccs" becomes "cs-cs", "ggy" becomes "gy-gy", "lly" becomes "ly-ly",
 * "nny" becomes "ny-ny", "tty" becomes "ty-ty", "zzs" becomes "zs-zs", "ssz" becomes "sz-sz".
 */
#define TK_TEXT_HYPHEN_DOUBLE_DIGRAPH	2 /* hu */
/*
 * If the soft hyphen is the right neighbor of any vowel, and the right neighbor is the same vowel,
 * then the doublevowel hyphenation rule will be applied.
 */
#define TK_TEXT_HYPHEN_DOUBLE_VOWEL	3 /* nl */
/*
 * In Catalan, a geminated consonant can be splitted: the word "paral·lel" hyphenates
 * into "paral-lel".
 */
#define TK_TEXT_HYPHEN_GEMINATION	4 /* ca */
/*
 * In Polish the hyphen will be repeated after line break, this means for example that "kong-fu"
 * becomes "kong- -fu".
 */
#define TK_TEXT_HYPHEN_REPEAT		5 /* pl */
/*
 * If the soft hyphen is the right neighbor of any vocal, and the right neighbor of any vocal with
 * trema (umlaut), then the trema hyphenation rule will be applied.
 */
#define TK_TEXT_HYPHEN_TREMA		6 /* nl */
/*
 * If the soft hyphen is the right neighbor of any consonant, and the right neighbor is the same
 * consonant, and the right consonant is followed by a vowel, then the tripleconsonant hyphenation
 * rule will be applied when not hyphenating.
 */
#define TK_TEXT_HYPHEN_TRIPLE_CONSONANT	7 /* de, nb, nn, no, sv */
/*
 * Mask of all defined hyphen rules.
 */
#define TK_TEXT_HYPHEN_MASK		((1 << (TK_TEXT_HYPHEN_TRIPLE_CONSONANT + 1)) - 1)

/*
 * Some constants for the mouse hovering:
 */

#define TK_TEXT_NEARBY_IS_UNDETERMINED	INT_MAX /* is not yet determined */
#define TK_TEXT_IS_NEARBY		INT_MIN /* is on border */


typedef struct TkTextTag {
    const char *name;		/* Name of this tag. This field is actually a pointer to the key
    				 * from the entry in 'sharedTextPtr->tagTable', so it needn't be
				 * freed explicitly. For "sel" tags this is just a static string,
				 * so again need not be freed. */
    const struct TkSharedText *sharedTextPtr;
				/* Shared section of all peers. */
    struct TkText *textPtr;
				/* If non-NULL, then this tag only applies to the given text widget
				 * (when there are peer widgets). */
    struct Node *rootPtr;	/* Pointer into the B-Tree at the lowest node that completely
    				 * dominates the ranges of text occupied by the tag. At this node
				 * there is no information about the tag. One or more children of
				 * the node do contain information about the tag. */
    uint32_t priority;		/* Priority of this tag within widget. 0 means lowest priority.
    				 * Exactly one tag has each integer value between 0 and numTags-1. */
    uint32_t index;		/* Unique index for fast tag lookup. It is guaranteed that the index
    				 * number is less than 'TkBitSize(sharedTextPtr->usedTags)'.*/
    uint32_t tagEpoch;		/* Epoch of creation time. */
    uint32_t refCount;		/* Number of objects referring to us. */
    bool isDisabled;		/* This tag is disabled? */

    /*
     * Information for tag collection [TkBTreeGetTags, TextInspectCmd, TkTextPickCurrent].
     */

    struct TkTextTag *nextPtr;	/* Will be set by TkBTreeGetTags, TkBTreeClearTags, and TextInsertCmd. */
    struct TkTextTag *succPtr;	/* Only TextInspectCmd will use this attribute. */
    uint32_t flag;		/* Only for temporary usage (currently only TextInspectCmd, and
    				 * EmbImageConfigure will use this attribute). */
    uint32_t epoch;		/* Only TkBTreeGetTags, TkBTreeGetSegmentTags, and TkBTreeClearTags
    				 * will use this attribute. */

    /*
     * Information for undo/redo.
     */

    TkTextUndoToken *recentTagAddRemoveToken;
    				/* Holds the undo information of last tag add/remove operation. */
    TkTextUndoToken *recentChangePriorityToken;
    				/* Holds the undo information of last tag lower/raise operation. */
    bool recentTagAddRemoveTokenIsNull;
    				/* 'recentTagAddRemoveToken' is null, this means the pointer still
				 * is valid, but should not be saved onto undo stack. */
    uint32_t savedPriority; 	/* Contains the priority before recentChangePriorityToken will be set. */
    int32_t undoTagListIndex;	/* Index to entry in 'undoTagList', is -1 if not in 'undoTagList'. */

    /*
     * Information for displaying text with this tag. The information belows
     * acts as an override on information specified by lower-priority tags.
     * If no value is specified, then the next-lower-priority tag on the text
     * determins the value. The text widget itself provides defaults if no tag
     * specifies an override.
     */

    Tk_3DBorder border;		/* Used for drawing background. NULL means no value specified here. */
    int borderWidth;		/* Width of 3-D border for background. */
    Tcl_Obj *borderWidthPtr;	/* Width of 3-D border for background. */
    Tcl_Obj *reliefPtr;		/* -relief option object. NULL means option not specified. */
    int relief;			/* 3-D relief for background. */
    Pixmap bgStipple;		/* Stipple bitmap for background. None means no value specified here. */
    char *indentBgString;	/* Background will be indented accordingly to the -lmargin1, and
    				 * -lmargin2 options. */
    bool indentBg;		/* Background will be indented accordingly to the -lmargin1, and
    				 * -lmargin2 options. */
    XColor *fgColor;		/* Foreground color for text. NULL means no value specified here. */
    Tk_Font tkfont;		/* Font for displaying text. NULL means no value specified here. */
    Pixmap fgStipple;		/* Stipple bitmap for text and other foreground stuff. None means
    				 * no value specified here.*/
    char *justifyString;	/* -justify option string (malloc-ed). NULL means option not
    				 * specified. */
    TkTextJustify justify;	/* How to justify text: TK_TEXT_JUSTIFY_LEFT, TK_TEXT_JUSTIFY_RIGHT,
    				 * TK_TEXT_JUSTIFY_CENTER, or TK_TEXT_JUSTIFY_FULL. Only valid if
				 * justifyString is non-NULL. */
    char *lMargin1String;	/* -lmargin1 option string (malloc-ed). NULL means option not
				 * specified. */
    int lMargin1;		/* Left margin for first display line of each text line, in pixels.
    				 * Only valid if lMargin1String is non-NULL. */
    char *lMargin2String;	/* -lmargin2 option string (malloc-ed). NULL means option not
    				 * specified. */
    int lMargin2;		/* Left margin for second and later display lines of each text line,
    				 * in pixels. Only valid if lMargin2String is non-NULL. */
    Tk_3DBorder lMarginColor;	/* Used for drawing background in left margins. This is used for both
    				 * lmargin1 and lmargin2. NULL means no value specified here. */
    char *offsetString;		/* -offset option string (malloc-ed). NULL means option not specified. */
    int offset;			/* Vertical offset of text's baseline from baseline of line. Used
    				 * for superscripts and subscripts. Only valid if offsetString is
				 * non-NULL. */
    char *overstrikeString;	/* -overstrike option string (malloc-ed). NULL means option not
    				 * specified. */
    bool overstrike;		/* True means draw horizontal line through middle of text. Only
    				 * valid if overstrikeString is non-NULL. */
    XColor *overstrikeColor;    /* Color for the overstrike. NULL means same color as foreground. */
    char *rMarginString;	/* -rmargin option string (malloc-ed). NULL means option not
    				 * specified. */
    int rMargin;		/* Right margin for text, in pixels. Only valid if rMarginString
    				 * is non-NULL. */
    Tk_3DBorder rMarginColor;	/* Used for drawing background in right margin. NULL means no value
    				 * specified here. */
    Tk_3DBorder selBorder;	/* Used for drawing background for selected text.
				 * NULL means no value specified here. */
    XColor *selFgColor;		/* Foreground color for selected text. NULL means no value specified
    				 * here. */
    char *spacing1String;	/* -spacing1 option string (malloc-ed). NULL means option not
    				 * specified. */
    int spacing1;		/* Extra spacing above first display line for text line. Only valid
    				 * if spacing1String is non-NULL. */
    char *spacing2String;	/* -spacing2 option string (malloc-ed). NULL means option not
    				 * specified. */
    int spacing2;		/* Extra spacing between display lines for the same text line. Only
    				 * valid if spacing2String is non-NULL. */
    char *spacing3String;	/* -spacing2 option string (malloc-ed). NULL means option not
    				 * specified. */
    int spacing3;		/* Extra spacing below last display line for text line. Only valid
    				 * if spacing3String is non-NULL. */
    Tcl_Obj *tabStringPtr;	/* -tabs option string. NULL means option not specified. */
    struct TkTextTabArray *tabArrayPtr;
				/* Info about tabs for tag (malloc-ed) or NULL. Corresponds to
				 * tabString. */
    int tabStyle;		/* One of TABULAR or WORDPROCESSOR or NONE (if not specified). */
    char *underlineString;	/* -underline option string (malloc-ed). NULL means option not
    				 * specified. */
    bool underline;		/* True means draw underline underneath text. Only valid if
    				 * underlineString is non-NULL. */
    XColor *underlineColor;     /* Color for the underline. NULL means same color as foreground. */
    XColor *eolColor;		/* Color for the end of line symbol. NULL means same color as
    				 * foreground. */
    XColor *hyphenColor;	/* Color for the soft hyphen character. NULL means same color as
    				 * foreground. */
    TkWrapMode wrapMode;	/* How to handle wrap-around for this tag. Must be TEXT_WRAPMODE_CHAR,
				 * TEXT_WRAPMODE_NONE, TEXT_WRAPMODE_WORD, TEXT_WRAPMODE_CODEPOINT, or
				 * TEXT_WRAPMODE_NULL to use wrapmode for whole widget. */
    TkTextSpaceMode spaceMode;	/* How to handle displaying spaces. Must be TEXT_SPACEMODE_NULL,
    				 * TEXT_SPACEMODE_NONE, TEXT_SPACEMODE_EXACT, or TEXT_SPACEMODE_TRIM. */
    Tcl_Obj *hyphenRulesPtr;	/* The hyphen rules string. */
    int hyphenRules;		/* The hyphen rules, only useful for soft hyphen segments. */
    Tcl_Obj *langPtr;		/* -lang option string. NULL means option not specified. */
    char lang[3];		/* The specified language for the text content, only enabled if not
    				 * NUL. */
    char *elideString;		/* -elide option string (malloc-ed). NULL means option not specified. */
    bool elide;			/* True means that data under this tag should not be displayed. */
    bool undo;			/* True means that any change of tagging with this tag will be pushed
    				 * on the undo stack (if undo stack is enabled), otherwise this tag
				 * will not regarded in the undo/redo process. */

    /*
     * Derived values, and the container for all the options.
     */

    bool affectsDisplay;	/* True means that this tag affects the way information is
    				 * displayed on the screen (so need to redisplay if tag changes). */
    bool affectsDisplayGeometry;/* True means that this tag affects the size with which
    				 * information is displayed on the screen (so need to recalculate
				 * line dimensions if tag changes). */
    Tk_OptionTable optionTable;	/* Token representing the configuration specifications. */
} TkTextTag;

/*
 * Some definitions for tag search, used by TkBTreeStartSearch, TkBTreeStartSearchBack:
 */

typedef enum {
    SEARCH_NEXT_TAGON,		/* Search for next range, this will skip the current range. */
    SEARCH_EITHER_TAGON_TAGOFF,	/* Search for next tagon/tagoff change. */
} TkTextSearchMode;

/*
 * The data structure below is used for searching a B-tree for transitions on
 * a single tag (or for all tag transitions). No code outside of tkTextBTree.c
 * should ever modify any of the fields in these structures, but it's OK to
 * use them for read-only information.
 */

typedef struct TkTextSearch {
    TkTextIndex curIndex;	/* Position of last tag transition returned by TkBTreeNextTag,
    				 * or index of start of segment containing starting position
				 * for search if TkBTreeNextTag hasn't been called yet, or
				 * same as stopIndex if search is over. */
    TkTextSegment *segPtr;	/* Actual tag segment returned by last call to TkBTreeNextTag,
    				 * or NULL if TkBTreeNextTag hasn't returned anything yet. */
    TkTextSegment *resultPtr;	/* Actual result of last search. */
    TkTextSegment *lastPtr;	/* Stop search before just before considering this segment. */
    TkTextLine *lastLinePtr;	/* The last line of the search range. */
    const TkTextTag *tagPtr;	/* Tag to search for. */
    struct TkText *textPtr;	/* Where we are searching. */
    TkTextSearchMode mode;	/* Search mode. */
    bool tagon;			/* We have to search for toggle on? */
    bool endOfText;		/* Search is ending at end of text? */
    int linesLeft;		/* Lines left to search (including curIndex and stopIndex).
    				 * When this becomes <= 0 the search is over. */
    int linesToEndOfText;	/* Add this to linesLeft when searching to end of text. */
} TkTextSearch;

/*
 * The following data structure describes a single tab stop. It must be kept
 * in sync with the 'tabOptionStrings' array in the function 'TkTextGetTabs'
 */

typedef enum { LEFT, RIGHT, CENTER, NUMERIC } TkTextTabAlign;

/*
 * The following are the supported styles of tabbing, used for the -tabstyle
 * option of the text widget. The last element is only used for tag options.
 */

typedef enum {
    TK_TEXT_TABSTYLE_TABULAR,
    TK_TEXT_TABSTYLE_WORDPROCESSOR,
    TK_TEXT_TABSTYLE_NONE
} TkTextTabStyle;

typedef struct TkTextTab {
    int location;		/* Offset in pixels of this tab stop from the left margin
    				 * (lmargin2) of the text. */
    TkTextTabAlign alignment;	/* Where the tab stop appears relative to the text. */
} TkTextTab;

typedef struct TkTextTabArray {
    int numTabs;		/* Number of tab stops. */
    double lastTab;		/* The accurate fractional pixel position of the last tab. */
    double tabIncrement;	/* The accurate fractional pixel increment between interpolated
    				 * tabs we have to create when we exceed numTabs. */
    TkTextTab tabs[1];		/* Array of tabs. The actual size will be numTabs. THIS FIELD
    				 * MUST BE THE LAST IN THE STRUCTURE. */
} TkTextTabArray;

/*
 * Enumeration defining the edit modes of the widget.
 */

typedef enum {
    TK_TEXT_EDIT_INSERT,	/* insert mode */
    TK_TEXT_EDIT_DELETE,	/* delete mode */
    TK_TEXT_EDIT_REPLACE,	/* replace mode */
    TK_TEXT_EDIT_OTHER		/* none of the above */
} TkTextEditMode;

/*
 * The following enum is used to define a type for the -state option of the Text widget.
 */

typedef enum {
    TK_TEXT_STATE_DISABLED,	/* Don't receive any text. */
    TK_TEXT_STATE_NORMAL,	/* Allows all operations. */
    TK_TEXT_STATE_READONLY	/* Do not allow user operations. */
} TkTextState;

/*
 * A data structure of the following type is shared between each text widget that are peers.
 */

struct TkRangeList;
struct TkText;

typedef struct TkSharedText {
    unsigned refCount;		/* Reference count this shared object. */
    TkTextBTree tree;		/* B-tree representation of text and tags for widget. */
    Tcl_HashTable tagTable;	/* Hash table that maps from tag names to pointers to TkTextTag
    				 * structures. The "sel" tag does not feature in this table, since
				 * there's one of those for each text peer. */
    unsigned numEnabledTags;	/* Number of tags currently enabled; needed to keep track of
    				 * priorities. */
    unsigned numTags;		/* Number of tags currently defined for widget. */
    unsigned numMarks;		/* Number of marks, not including private or special marks. */
    unsigned numPrivateMarks;	/* Number of private marks. */
    unsigned numImages;		/* Number of embedded images; for information only. */
    unsigned numWindows;	/* Number of embedded windows; for information only. */
    unsigned tagInfoSize;	/* The required index size for tag info sets. */
    unsigned tagEpoch;		/* Increase whenever a new tag has been created. */
    struct TkBitField *usedTags;
				/* Bit set of used tag indices. */
    struct TkBitField *elisionTags;
				/* Bit set of tags with elide information. */
    struct TkBitField *selectionTags;
				/* Bit set of all selection tags. */
    struct TkBitField *dontUndoTags;
				/* Bit set of all tags with -undo=no. */
    struct TkBitField *affectDisplayTags;
				/* Bit set of tags which are affecting the display. */
    struct TkBitField *notAffectDisplayTags;
				/* Bit set of tags which are *not* affecting the display. */
    struct TkBitField *affectDisplayNonSelTags;
				/* Bit set of tags which are affecting the display, but exclusive
				 * the special selection tags. */
    struct TkBitField *affectGeometryTags;
				/* Bit set of tags which are affecting the display geometry. */
    struct TkBitField *affectGeometryNonSelTags;
				/* Bit set of tags which are affecting the display geometry, but
				 * exclusive the special selection tags. */
    struct TkBitField *affectLineHeightTags;
				/* Bit set of tags which are affecting the line heigth. */
    TkTextTag **tagLookup;	/* Lookup vector for tags. */
    Tcl_HashTable markTable;	/* Hash table that maps from mark names to pointers to mark
    				 * segments. The special "insert" and "current" marks are not
				 * stored in this table, but directly accessed as fields of
				 * textPtr. */
    Tcl_HashTable windowTable;	/* Hash table that maps from window names to pointers to window
    				 * segments. If a window segment doesn't yet have an associated
				 * window, there is no entry for it here. */
    Tcl_HashTable imageTable;	/* Hash table that maps from image names to pointers to image
    				 * segments. If an image segment doesn't yet have an associated
				 * image, there is no entry for it here. */
    Tk_BindingTable tagBindingTable;
				/* Table of all tag bindings currently defined for this widget.
				 * NULL means that no bindings exist, so the table hasn't been
				 * created. Each "object" used for this table is the name of a
				 * tag. */
    TkTextSegment *startMarker;	/* The start marker, the content of this widget starts after this
    				 * merker. */
    TkTextSegment *endMarker;	/* If the end marker is at byte index zero, then the next newline
				 * does not belong to this widget, otherwise the next newline
				 * also belongs to this widget. */
    union TkTextTagSet *emptyTagInfoPtr;
    				/* Empty tag information. */
    unsigned numMotionEventBindings;
				/* Number of tags with bindings to motion events. */
    unsigned numElisionTags;	/* Number of tags with elideString. */
    bool allowUpdateLineMetrics;
				/* We don't allow line height computations before first Configure
				 * event has been accepted. */

    /*
     * Miscellanous information.
     */

    bool steadyMarks;		/* This option causes that any mark now simultaneous behaves like
    				 * an invisible character, this means that the relative order of
				 * marks will not change. */
    unsigned imageCount;	/* Used for creating unique image names. */
    unsigned countEmbWindows;	/* Used for counting embedded windows. */
    bool triggerWatchCmd;	/* Whether we should trigger the watch command for any peer. */
    bool triggerAlways;		/* Whether we should always trigger the watch command for any peer. */
    bool haveToSetCurrentMark;	/* Flag whether a position change of the "current" mark has
    				 * been postponed in any peer. */

    /*
     * Miscellaneous mutual data.
     */

    unsigned inspectEpoch;	/* Only used in TextInspectCmd. */
    unsigned pickEpoch;		/* Only used in TkTextPickCurrent. */
    TkTextSegment *protectionMark[2];
    				/* Protection markers for segments .*/
    struct TkText *mainPeer;	/* Needed for unrelated index lookup. */

    /*
     * Information for displaying.
     */

    Tcl_HashTable breakInfoTable;
    				/* Hash table that maps from logical line pointers to BreakInfos for
				 * this widget. Note that this table is used in display stuff, but
				 * for technical reasons we have to keep this table in shared
				 * resource, because it's a shared table. */
    bool breakInfoTableIsInitialized;
    				/* Flag whether breakInfoTable is initialized. */

    /*
     * Information related to the undo/redo functionality.
     */

    struct TkTextUndoStack *undoStack;
				/* The undo/redo stack. */
    int maxUndoDepth;		/* The maximum depth of the undo stack expressed as the
    				 * maximum number of compound statements. */
    int maxRedoDepth;		/* The maximum depth of the redo stack expressed as the
    				 * maximum number of compound statements. */
    int maxUndoSize;		/* The maximum number of bytes kept on the undo stack. */
    bool undo;			/* Non-zero means the undo/redo behaviour is enabled. */
    bool autoSeparators;	/* Non-zero means the separators will be inserted automatically. */
    bool isModified;		/* Flag indicating the computed 'modified' state of the text widget. */
    bool isAltered;		/* Flag indicating the computed 'altered' state of the text widget. */
    bool isIrreversible;	/* Flag indicating the computed 'irreversible' flag. Value
    				 * 'true' can never change to 'false', except the widget will
				 * be cleared, or the user is clearing. */
    bool userHasSetModifiedFlag;/* Flag indicating if the user has set the 'modified' flag.
    				 * Value 'true' is superseding the computed value, but value
				 * 'false' is only clearing to the initial state of this flag. */
    bool undoStackEvent;	/* Flag indicating whether <<UndoStack>> is already triggered. */
    unsigned undoLevel;		/* The undo level which corresponds to the unmodified state. */
    TkTextEditMode lastEditMode;/* Keeps track of what the last edit mode was. */
    int lastUndoTokenType;	/* Type of newest undo token on stack. */
    TkTextTag **undoTagList;	/* Array of tags, prepared for undo stack. */
    TkTextMarkChange *undoMarkList;
    				/* Array of mark changes, prepared for undo stack. */
    uint32_t undoTagListCount;	/* Number of entries in array 'undoTagList'. */
    uint32_t undoTagListSize;	/* Size of array 'undoTagList'. */
    				/* Array of undo entries for mark operations. */
    uint32_t undoMarkListCount;	/* Number of entries in array 'undoMarkList'. */
    uint32_t undoMarkListSize;	/* Size of array 'undoMarkList'. */
    uint32_t insertDeleteUndoTokenCount;
    				/* Count number of tokens on undo stack for insert/delete actions. */
    TkTextUndoIndex prevUndoStartIndex;
    				/* Start index (left position) of previous undo operation; only for
				 * 'insert' and 'delete'. */
    TkTextUndoIndex prevUndoEndIndex;
    				/* End index (right position) of previous undo operation; only for
				 * 'insert' and 'delete'. */

    /*
     * Keep track of all the peers.
     */

    struct TkText *peers;
    unsigned numPeers;
} TkSharedText;

/*
 * The following enum is used to define a type for the -insertunfocussed
 * option of the Text widget.
 */

typedef enum {
    TK_TEXT_INSERT_NOFOCUS_HOLLOW,
    TK_TEXT_INSERT_NOFOCUS_NONE,
    TK_TEXT_INSERT_NOFOCUS_SOLID
} TkTextInsertUnfocussed;

/*
 * The tagging modes:
 */

typedef enum {
    TK_TEXT_TAGGING_WITHIN,	/* The new text will receive any tags that are present on both
				 * the character before and the character after the insertion point.
				 * This is the default. */
    TK_TEXT_TAGGING_GRAVITY,	/* The new text will receive any tags that are present at one side
    				 * of the insertion point: if insert cursor has gravity right then
				 * receive the tags of the character after the insertion point,
				 * otherwise it will receive the tags of the character before the
				 * insertion point (supports Arabian, and the like). */
    TK_TEXT_TAGGING_NONE	/* The new text will not receive any tags from adjacent characters. */
} TkTextTagging;

/*
 * A data structure of the following type is kept for each text widget that
 * currently exists for this process:
 */

struct TkTextStringList;

typedef struct TkText {
    /*
     * Information related to and accessed by widget peers and the TkSharedText handling routines.
     */

    TkSharedText *sharedTextPtr;/* Shared section of all peers. */
    struct TkText *next;	/* Next in list of linked peers. */
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    TkTextLine *startLine;	/* First B-tree line to show, or NULL to start at the beginning.
    				 * Note that this feature is deprecated and should be removed some day.
				 */
    TkTextLine *endLine;	/* Last B-tree line to show, or NULL for up to the end.
    				 * Note that this feature is deprecated and should be removed some day.
				 */
#endif
    TkTextSegment *startMarker;	/* First B-Tree segment (mark) belonging to this widget. */
    TkTextSegment *endMarker;	/* Last B-Tree segment (mark) belonging to this widget */
    Tcl_Obj *newStartIndex;	/* New position for start marker. */
    Tcl_Obj *newEndIndex;	/* New position for end marker. */
    int pixelReference;		/* Counter into the current tree reference index corresponding
    				 * to this widget. */
    bool abortSelections;	/* Set to true whenever the text is modified in a way that interferes
    				 * with selection retrieval: used to abort incremental selection
				 * retrievals. */
    bool pendingAfterSync;	/* RunAfterSyncCmd is in event queue. */
    bool pendingFireEvent;	/* FireWidgetViewSyncEvent is in event queue. */
    bool sendSyncEvent;		/* Send <<WidgetViewSync>> event as soon as the line metric is
    				 * up-to-date, even if we have no sync state change. */
    bool prevSyncState;		/* Previous sync state of the line-height calculation. */
    bool dontRepick;		/* Set to 'true' during scroll operation, but only when -responsiveness
    				 * is greater than zero. */

    /*
     * Standard Tk widget information and text-widget specific items.
     */

    Tk_Window tkwin;		/* Window that embodies the text. NULL means that the window has been
    				 * destroyed but the data structures haven't yet been cleaned up.*/
    Display *display;		/* Display for widget. Needed, among other things, to allow resources
    				 * to be freed even after tkwin has gone away. */
    Tcl_Interp *interp;		/* Interpreter associated with widget. Used to delete widget command. */
    Tcl_Command widgetCmd;	/* Token for text's widget command. */
    TkTextState state;		/* Either TK_TEXT_STATE_NORMAL, TK_TEXT_STATE_READONLY, or
    				 * TK_TEXT_STATE_DISABLED. A text widget is also read-only when
				 * disabled. */

    /*
     * Default information for displaying (may be overridden by tags applied
     * to ranges of characters).
     */

    Tk_3DBorder border;		/* Structure used to draw 3-D border and default background. */
    int borderWidth;		/* Width of 3-D border to draw around entire widget. */
    int padX, padY;		/* Padding between text and window border. */
    int relief;			/* 3-d effect for border around entire widget: TK_RELIEF_RAISED etc. */
    int highlightWidth;		/* Width in pixels of highlight to draw around widget when it
    				 * has the focus. <= 0 means don't draw a highlight. */
    XColor *highlightBgColorPtr;/* Color for drawing traversal highlight area when highlight is off. */
    XColor *highlightColorPtr;	/* Color for drawing traversal highlight. */
    Tk_Cursor cursor;		/* Current cursor for window, or None. */
    XColor *fgColor;		/* Default foreground color for text. */
    XColor *eolColor;		/* Foreground color for end of line symbol, can be NULL. */
    XColor *eotColor;		/* Foreground color for end of text symbol, can be NULL. */
    Tcl_Obj *eolCharPtr;	/* Use this character for displaying end of line. Can be NULL or empty,
    				 * in this case the default char U+00B6 (pilcrow) will be used. */
    Tcl_Obj *eotCharPtr;	/* Use this character for displaying end of text. Can be NULL or empty,
    				 * in this case the default char U+00B6 (pilcrow) will be used. */
    XColor *hyphenColor;	/* Foreground color for soft hyphens, can be NULL. */
    Tk_Font tkfont;		/* Default font for displaying text. */
    int charWidth;		/* Width of average character in default font. */
    int spaceWidth;		/* Width of space character in default font. */
    int lineHeight;		/* Height of line in default font, including line spacing. */
    int spacing1;		/* Default extra spacing above first display line for each text line. */
    int spacing2;		/* Default extra spacing between display lines for the same text line. */
    int spacing3;		/* Default extra spacing below last display line for each text line. */
    Tcl_Obj *tabOptionPtr; 	/* Value of -tabs option string. */
    TkTextTabArray *tabArrayPtr;
				/* Information about tab stops (malloc'ed). NULL means perform
				 * default tabbing behavior. */
    int tabStyle;		/* One of TABULAR or WORDPROCESSOR. */
    TkTextJustify justify;	/* How to justify text: TK_TEXT_JUSTIFY_LEFT, TK_TEXT_JUSTIFY_RIGHT,
    				 * TK_TEXT_JUSTIFY_CENTER, or TK_TEXT_JUSTIFY_FULL. */
    Tcl_Obj *hyphenRulesPtr;	/* The hyphen rules string. */
    int hyphenRules;		/* The hyphen rules, only useful for soft hyphen segments. */
    Tcl_Obj *langPtr;		/* -lang option string. NULL means option not specified. */
    char lang[3];		/* The specified language for the text content, only enabled if not
    				 * NUL. */

    /*
     * Additional information used for displaying:
     */

    TkWrapMode wrapMode;	/* How to handle wrap-around. Must be TEXT_WRAPMODE_CHAR,
    				 * TEXT_WRAPMODE_WORD, TEXT_WRAPMODE_CODEPOINT, or TEXT_WRAPMODE_NONE. */
    TkTextSpaceMode spaceMode;	/* How to handle displaying spaces. Must be TEXT_SPACEMODE_NONE,
    				 * TEXT_SPACEMODE_EXACT, or TEXT_SPACEMODE_TRIM. */
    bool useHyphenSupport;	/* Indicating the hypenation support. */
    bool hyphenate;		/* Indicating whether the soft hyphens will be used for line breaks
    				 * (if not in state TK_TEXT_STATE_NORMAL). */
    bool useUniBreak;		/* Use library libunibreak for line break computation, otherwise the
    				 * internal algorithm will be used. */
    int width, height;		/* Desired dimensions for window, measured in characters. */
    bool setGrid;		/* Non-zero means pass gridding information to window manager. */
    int prevWidth, prevHeight;	/* Last known dimensions of window; used to detect changes in size. */
    TkTextIndex topIndex;	/* Identifies first character in top display line of window. */
    struct TextDInfo *dInfoPtr;	/* Information maintained by tkTextDisp.c. */
    bool showEndOfLine;		/* Flag whether the end of line symbol will be shown at end of
    				 * each logical line. */
    bool showEndOfText;		/* Flag whether the end of text symbol will be shown at end of text. */
    bool syncTime;		/* Synchronization timeout, used for line metric calculation, default is
    				 * 200. */

    /*
     * Information related to selection.
     */

    TkTextTag *selTagPtr;	/* Pointer to "sel" tag. Used to tell when a new selection
    				 * has been made. */
    Tk_3DBorder selBorder;	/* Border and background for selected characters. This is
    				 * a copy of information in *selTagPtr, so it shouldn't be
				 * explicitly freed. */
    Tk_3DBorder inactiveSelBorder;
				/* Border and background for selected characters when they
				 * don't have the focus. */
    int selBorderWidth;		/* Width of border around selection. */
    Tcl_Obj *selBorderWidthPtr;	/* Width of border around selection. */
    XColor *selFgColorPtr;	/* Foreground color for selected text. This is a copy of
    				 * information in *selTagPtr, so it shouldn't be explicitly freed. */
    XColor *inactiveSelFgColorPtr;
    				/* Foreground color for selected characters when they don't have
				 * the focus. */
    bool exportSelection;	/* Non-zero means tie "sel" tag to X selection. */
    TkTextSearch selSearch;	/* Used during multi-pass selection retrievals. */
    TkTextIndex selIndex;	/* Used during multi-pass selection retrievals. This index
    				 * identifies the next character to be returned from the
				 * selection. */

    /*
     * Information related to insertion cursor:
     */

    TkTextSegment *insertMarkPtr;
				/* Points to segment for "insert" mark. */
    Tk_3DBorder insertBorder;	/* Used to draw vertical bar for insertion cursor. */
    XColor *insertFgColorPtr;	/* Foreground color for text behind a block cursor.
    				 * NULL means no value specified here. */
    bool showInsertFgColor;	/* Flag whether insertFgColorPtr is relevant. */
    int insertWidth;		/* Total width of insert cursor. */
    int insertBorderWidth;	/* Width of 3-D border around insert cursor */
    TkTextInsertUnfocussed insertUnfocussed;
				/* How to display the insert cursor when the
				 * text widget does not have the focus. */
    int insertOnTime;		/* Number of milliseconds cursor should spend
				 * in "on" state for each blink. */
    int insertOffTime;		/* Number of milliseconds cursor should spend
				 * in "off" state for each blink. */
    Tcl_TimerToken insertBlinkHandler;
				/* Timer handler used to blink cursor on and off. */
    TkTextTagging tagging;	/* Tagging mode, used when inserting chars; the mode how to extend
				 * tagged ranges of characters. */

    /*
     * Information used for the watch of changes:
     */

    Tcl_Obj *watchCmd;		/* The command prefix for the "watch" command. */
    bool triggerAlways;		/* Whether we should trigger for any modification. */
    TkTextIndex insertIndex;	/* Saved position of insertion cursor. */

    /*
     * Information related to the language support functionality.
     */

    char *brksBuffer;		/* Buffer for line break information, will be filled by
    				 * TkTextComputeBreakLocations (for TEXT_WRAPMODE_CODEPOINT). */
    unsigned brksBufferSize;	/* Size of line break buffer. */

    /*
     * Information used for event bindings associated with tags:
     */

    TkTextSegment *currentMarkPtr;
				/* Pointer to segment for "current" mark, or NULL if none. */
    TkTextIndex currentMarkIndex;
    				/* The index of the "current" mark, needed for postponing the
				 * insertion of the "current" mark segment.
				 */
    bool haveToSetCurrentMark;	/* Flag whether a position change of the "current" mark has
    				 * been postponed. */
    XEvent pickEvent;		/* The event from which the current character was chosen.
    				 * Must be saved so that we can repick after modifications
				 * to the text. */
    union TkTextTagSet *curTagInfoPtr;
    				/* Set of tags associated with character at current mark. */
    uint32_t lastChunkID;	/* Cache chunk ID of last mouse hovering. */
    int32_t lastX;		/* Cache x coordinate of last mouse hovering. */
    int32_t lastLineY;		/* Cache y coordinate of the display line of last mouse hovering.
    				 * If lastLineY == INT_MAX then it is undetermined (initialized).
				 * If lastLineY == INT_MIN then it is on the border.
				 * Otherwise it's inside a display chunk. */

    /*
     * Miscellaneous additional information:
     */

    char *takeFocus;		/* Value of -takeFocus option; not used in the C code, but
    				 * used by keyboard traversal scripts. Malloc'ed, but may be NULL. */
    char *xScrollCmd;		/* Prefix of command to issue to update horizontal scrollbar
    				 * when view changes. */
    char *yScrollCmd;		/* Prefix of command to issue to update vertical scrollbar when
    				 * view changes. */
    unsigned flags;		/* Miscellaneous flags; see below for definitions. */
    Tk_OptionTable optionTable;	/* Token representing the configuration specifications. */
    unsigned refCount;		/* Number of objects referring to us. */
    bool blockCursorType;	/* false = standard insertion cursor, true = block cursor. */
    bool accelerateTagSearch;	/* Update B-Tree tag information for search? */
    int responsiveness;		/* The delay in ms before repick the mouse position (behavior when
				 * scrolling the widget). */
    unsigned uniqueIdCounter;	/* Used for the generation of unique mark names. */
    struct TkTextStringList *varBindingList;
    				/* Linked list of variables which should be unset when the widget
				 * will be destroyed. */
    bool sharedIsReleased;	/* Boolean value whether shared resource have been released. */

    /*
     * Copies of information from the shared section relating to the editor control mode:
     */

    bool steadyMarks;		/* false = behavior of original implementation,
    				 * true  = new editor control mode. */

    /*
     * Copies of information from the shared section relating to the undo/redo functonality:
     */

    bool undo;			/* Non-zero means the undo/redo behaviour is enabled. */
    int maxUndoDepth;		/* The maximum depth of the undo stack expressed as the
    				 * maximum number of compound statements. */
    int maxRedoDepth;		/* The maximum depth of the redo stack expressed as the
    				 * maximum number of compound statements. */
    int maxUndoSize;		/* The maximum number of bytes kept on the undo stack. */
    bool autoSeparators;	/* Non-zero means the separators will be inserted automatically. */

    /*
     * Support of sync command:
     */

    Tcl_Obj *afterSyncCmd;	/* Commands to be executed when lines are up to date */

#if TK_CHECK_ALLOCS
    unsigned widgetNumber;
#endif
} TkText;

/*
 * Flag values for TkText records:
 *
 * GOT_SELECTION:		Non-zero means we've already claimed the selection.
 * INSERT_ON:			Non-zero means insertion cursor should be displayed on screen.
 * HAVE_FOCUS:			Non-zero means this window has the input focus.
 * BUTTON_DOWN:			1 means that a mouse button is currently down;
 *				this is used to implement grabs for the duration of button presses.
 * UPDATE_SCROLLBARS:		Non-zero means scrollbar(s) should be updated
 *				during next redisplay operation.
 * NEED_REPICK			This appears unused and should probably be ignored.
 * OPTIONS_FREED		The widget's options have been freed.
 * DESTROYED			The widget is going away.
 * MEM_RELEASED			The memory of text widget has been released (only for debugging).
 */

#define GOT_SELECTION		(1 << 0)
#define INSERT_ON		(1 << 1)
#define HAVE_FOCUS		(1 << 2)
#define BUTTON_DOWN		(1 << 3)
#define UPDATE_SCROLLBARS	(1 << 4)
#define NEED_REPICK		(1 << 5)
#define OPTIONS_FREED		(1 << 6)
#define DESTROYED		(1 << 7)
#define MEM_RELEASED		(1 << 8)

/*
 * The categories of segment types:
 */

typedef enum {
    SEG_GROUP_CHAR    = 1 << 0,	/* tkTextCharType */
    SEG_GROUP_MARK    = 1 << 1,	/* tkTextLeftMarkType, tkTextRightMarkType */
    SEG_GROUP_HYPHEN  = 1 << 2,	/* tkTextHyphenType */
    SEG_GROUP_BRANCH  = 1 << 3,	/* tkTextBranchType, tkTextLinkType */
    SEG_GROUP_IMAGE   = 1 << 4,	/* tkTextEmbImageType */
    SEG_GROUP_WINDOW  = 1 << 5,	/* tkTextEmbWindowType */
    SEG_GROUP_PROTECT = 1 << 6,	/* tkTextProtectionMarkType */
    SEG_GROUP_TAG     = 1 << 7,	/* this is only needed for convenience */
} TkSegGroupType;

/*
 * Records of the following type define segment types in terms of a collection
 * of procedures that may be called to manipulate segments of that type.
 */

typedef bool Tk_SegDeleteProc(TkSharedText *sharedTextPtr, struct TkTextSegment *segPtr, int flags);
typedef bool Tk_SegReuseProc(TkSharedText *sharedTextPtr, struct TkTextSegment *segPtr);
typedef int Tk_SegLayoutProc(const struct TkTextIndex *indexPtr, TkTextSegment *segPtr,
		    int offset, int maxX, int maxChars, bool noCharsYet, TkWrapMode wrapMode,
		    TkTextSpaceMode spaceMode, struct TkTextDispChunk *chunkPtr);
typedef void Tk_SegCheckProc(const struct TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
typedef Tcl_Obj *Tk_SegInspectProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);

typedef struct Tk_SegType {
    const char *name;		/* Name of this kind of segment. */
    TkSegGroupType group;	/* Group information. */
    int gravity;		/* The gravity of this segment, one of GRAVITY_LEFT, GRAVITY_NEUTRAL,
    				 * GRAVITY_RIGHT. */
    Tk_SegDeleteProc *deleteProc;
				/* Procedure to call to delete segment. */
    Tk_SegReuseProc *restoreProc;
    				/* Restore a preserved segment. This will be done when performing
    				 * an undo. */
    Tk_SegLayoutProc *layoutProc;
				/* Returns size information when figuring out what to display
				 * in window. */
    Tk_SegCheckProc *checkProc;	/* Called during consistency checks to check internal consistency
    				 * of segment. */
    Tk_SegInspectProc *inspectProc;
     				/* Called when creating the information for "inspect". */
} Tk_SegType;

/*
 * These items are the gravity values:
 */

enum { GRAVITY_LEFT, GRAVITY_NEUTRAL, GRAVITY_RIGHT };

/*
 * The following type and items describe different flags for text widget items
 * to count. They are used in both tkText.c and tkTextIndex.c, in
 * 'CountIndices', 'TkTextIndexBackChars', 'TkTextIndexForwChars', and
 * 'TkTextIndexCount'.
 */

typedef enum {
    COUNT_HYPHENS		= 1 << 1,
    COUNT_TEXT			= 1 << 2,
    COUNT_CHARS			= COUNT_HYPHENS | COUNT_TEXT,
    COUNT_INDICES		= 1 << 3,
    COUNT_DISPLAY		= 1 << 4,
    COUNT_DISPLAY_CHARS		= COUNT_CHARS | COUNT_DISPLAY,
    COUNT_DISPLAY_HYPHENS	= COUNT_HYPHENS | COUNT_DISPLAY,
    COUNT_DISPLAY_TEXT		= COUNT_TEXT | COUNT_DISPLAY,
    COUNT_DISPLAY_INDICES	= COUNT_INDICES | COUNT_DISPLAY
} TkTextCountType;

/*
 * Some definitions for line break support, must coincide with the defintions
 * in /usr/include/linebreak.h:
 */

#define LINEBREAK_MUSTBREAK	0 /* Break is mandatory */
#define LINEBREAK_ALLOWBREAK	1 /* Break is allowed */
#define LINEBREAK_NOBREAK	2 /* No break is possible */
#define LINEBREAK_INSIDEACHAR	3 /* Inside UTF-8 sequence */

/*
 * Flags for the delete function (Tk_SegDeleteProc):
 *
 * TREE_GONE		The entire tree is being deleted, so everything must get cleaned up.
 * DELETE_BRANCHES	The branches and links will be deleted.
 * DELETE_MARKS		The marks will be deleted.
 * DELETE_INCLUSIVE	The deletion of the marks includes also the marks given as arguments
 *			for the range.
 * DELETE_CLEANUP	We have to delete anyway, due to a cleanup.
 */

#define TREE_GONE		(1 << 0)
#define DELETE_BRANCHES		(1 << 1)
#define DELETE_MARKS		(1 << 2)
#define DELETE_INCLUSIVE	(1 << 3)
#define DELETE_CLEANUP		(1 << 4)
#define DELETE_LASTLINE		(1 << 5)

/*
 * The following definition specifies the maximum number of characters needed
 * in a string to hold a position specifier.
 */

#define TK_POS_CHARS		30

/*
 * Mask used for those options which may impact the text content
 * of individual lines displayed in the widget.
 */

#define TK_TEXT_LINE_REDRAW		(1 << 0)
#define TK_TEXT_LINE_REDRAW_BOTTOM_LINE	(1 << 1)

/*
 * Mask used for those options which may impact the pixel height calculations
 * of individual lines displayed in the widget.
 */

#define TK_TEXT_LINE_GEOMETRY		(1 << 2)

/*
 * Mask used for those options which should invoke the line metric update
 * immediately.
 */

#define TK_TEXT_SYNCHRONIZE		(1 << 3)

/*
 * Mask used for those options which may impact the start and end lines/index
 * used in the widget.
 */

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
# define TK_TEXT_LINE_RANGE		(1 << 4)
# define TK_TEXT_INDEX_RANGE		((1 << 5)|TK_TEXT_LINE_RANGE)
#else
# define TK_TEXT_INDEX_RANGE		(1 << 4)
#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

#if SUPPORT_DEPRECATED_TAG_OPTIONS
# define TK_TEXT_DEPRECATED_OVERSTRIKE_FG	(1 << 6)
# define TK_TEXT_DEPRECATED_UNDERLINE_FG	(1 << 7)
#endif /* SUPPORT_DEPRECATED_TAG_OPTIONS */

/*
 * Used as 'action' values in calls to TkTextInvalidateLineMetrics
 */

typedef enum {
    TK_TEXT_INVALIDATE_ONLY,
    TK_TEXT_INVALIDATE_INSERT,
    TK_TEXT_INVALIDATE_DELETE,
    TK_TEXT_INVALIDATE_ELIDE,
    TK_TEXT_INVALIDATE_REINSERTED
} TkTextInvalidateAction;

/*
 * Used as special 'pickPlace' values in calls to TkTextSetYView. Zero or
 * positive values indicate a number of pixels.
 */

#define TK_TEXT_PICKPLACE	-1
#define TK_TEXT_NOPIXELADJUST	-2

/*
 * Declarations for variables shared among the text-related files:
 */

MODULE_SCOPE bool tkBTreeDebug;
MODULE_SCOPE bool tkTextDebug;
MODULE_SCOPE const Tk_SegType tkTextCharType;
MODULE_SCOPE const Tk_SegType tkTextBranchType;
MODULE_SCOPE const Tk_SegType tkTextLinkType;
MODULE_SCOPE const Tk_SegType tkTextLeftMarkType;
MODULE_SCOPE const Tk_SegType tkTextRightMarkType;
MODULE_SCOPE const Tk_SegType tkTextHyphenType;
MODULE_SCOPE const Tk_SegType tkTextEmbImageType;
MODULE_SCOPE const Tk_SegType tkTextEmbWindowType;
MODULE_SCOPE const Tk_SegType tkTextProtectionMarkType;

/*
 * Convenience constants for a better readability of TkTextFindDisplayLineStartEnd call:
 */

enum { DISP_LINE_START = false, DISP_LINE_END = true };

/*
 * Helper for guarded deallocation.
 */

#define FREE_SEGMENT(ptr) { \
    assert(ptr->typePtr); \
    assert(!(ptr->typePtr = NULL)); \
    free(ptr); }

/*
 * We need a callback function for tag changes. The return value informs whether
 * this operation is undoable.
 */

typedef bool TkTextTagChangedProc(
    const TkSharedText *sharedTextPtr,
    TkText *textPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    const TkTextTag *tagPtr,
    bool affectsDisplayGeometry);

/*
 * Callback function for TkTextPerformWatchCmd().
 */

typedef void (*TkTextWatchGetIndexProc)(TkText *textPtr, TkTextIndex *indexPtr, void *clientData);

/*
 * These flages are needed for TkTextInspectOptions():
 */

#define INSPECT_DONT_RESOLVE_COLORS     (1 << 0)
#define INSPECT_DONT_RESOLVE_FONTS      (1 << 1)
#define INSPECT_INCLUDE_DATABASE_CONFIG (1 << 2)
#define INSPECT_INCLUDE_SYSTEM_CONFIG   (1 << 3)
#define INSPECT_INCLUDE_DEFAULT_CONFIG  (1 << 4)
#define INSPECT_INCLUDE_SYSTEM_COLORS   (1 << 5)

/*
 * Declarations for procedures that are used by the text-related files but
 * shouldn't be used anywhere else in Tk (or by Tk clients):
 */

inline TkSharedText *	TkBTreeGetShared(TkTextBTree tree);
inline int		TkBTreeGetNumberOfDisplayLines(const TkTextPixelInfo *pixelInfo);
MODULE_SCOPE void	TkBTreeAdjustPixelHeight(const TkText *textPtr,
			TkTextLine *linePtr, int newPixelHeight, unsigned mergedLogicalLines,
			    unsigned oldNumDispLines);
MODULE_SCOPE void	TkBTreeUpdatePixelHeights(const TkText *textPtr, TkTextLine *linePtr,
			    int numLines, unsigned epoch);
MODULE_SCOPE void	TkBTreeResetDisplayLineCounts(TkText *textPtr, TkTextLine *linePtr,
			    unsigned numLines);
MODULE_SCOPE bool	TkBTreeHaveElidedSegments(const TkSharedText *sharedTextPtr);
inline TkTextPixelInfo * TkBTreeLinePixelInfo(const TkText *textPtr, TkTextLine *linePtr);
MODULE_SCOPE bool	TkBTreeCharTagged(const TkTextIndex *indexPtr, const TkTextTag *tagPtr);
MODULE_SCOPE void	TkBTreeCheck(TkTextBTree tree);
MODULE_SCOPE TkTextBTree TkBTreeCreate(TkSharedText *sharedTextPtr, unsigned epoch);
MODULE_SCOPE void	TkBTreeAddClient(TkTextBTree tree, TkText *textPtr, int defaultHeight);
MODULE_SCOPE void	TkBTreeClientRangeChanged(TkText *textPtr, unsigned defaultHeight);
MODULE_SCOPE void	TkBTreeRemoveClient(TkTextBTree tree, TkText *textPtr);
MODULE_SCOPE void	TkBTreeDestroy(TkTextBTree tree);
MODULE_SCOPE int	TkBTreeLoad(TkText *textPtr, Tcl_Obj *content, bool validOptions);
MODULE_SCOPE void	TkBTreeDeleteIndexRange(TkSharedText *sharedTextPtr,
			    TkTextIndex *index1Ptr, TkTextIndex *index2Ptr,
			    int flags, TkTextUndoInfo *undoInfo);
inline unsigned		TkBTreeEpoch(TkTextBTree tree);
inline unsigned		TkBTreeIncrEpoch(TkTextBTree tree);
inline struct Node	* TkBTreeGetRoot(TkTextBTree tree);
MODULE_SCOPE TkTextLine * TkBTreeFindLine(TkTextBTree tree, const TkText *textPtr, unsigned line);
MODULE_SCOPE TkTextLine * TkBTreeFindPixelLine(TkTextBTree tree,
			    const TkText *textPtr, int pixels, int32_t *pixelOffset);
MODULE_SCOPE TkTextLine * TkBTreeGetLogicalLine(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, TkTextLine *linePtr);
MODULE_SCOPE TkTextLine * TkBTreeNextLogicalLine(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, TkTextLine *linePtr);
inline TkTextLine *	TkBTreePrevLogicalLine(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, TkTextLine *linePtr);
MODULE_SCOPE TkTextLine * TkBTreeNextDisplayLine(TkText *textPtr, TkTextLine *linePtr,
			    unsigned *displayLineNo, unsigned offset);
MODULE_SCOPE TkTextLine * TkBTreePrevDisplayLine(TkText *textPtr, TkTextLine *linePtr,
			    unsigned *displayLineNo, unsigned offset);
MODULE_SCOPE TkTextSegment * TkBTreeFindStartOfElidedRange(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, const TkTextSegment *segPtr);
MODULE_SCOPE TkTextSegment * TkBTreeFindEndOfElidedRange(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, const TkTextSegment *segPtr);
inline TkTextTag *	TkBTreeGetTags(const TkTextIndex *indexPtr);
MODULE_SCOPE TkTextTag * TkBTreeGetSegmentTags(const TkSharedText *sharedTextPtr,
			    const TkTextSegment *segPtr, const TkText *textPtr, bool *containsSelection);
MODULE_SCOPE const char * TkBTreeGetLang(const TkText *textPtr, const TkTextSegment *segPtr);
MODULE_SCOPE void	TkBTreeInsertChars(TkTextBTree tree, TkTextIndex *indexPtr, const char *string,
			    union TkTextTagSet *tagInfoPtr, TkTextTag *hyphenTagPtr,
			    TkTextUndoInfo *undoInfo);
MODULE_SCOPE TkTextSegment *TkBTreeMakeCharSegment(const char *string, unsigned length,
			    union TkTextTagSet *tagInfoPtr);
MODULE_SCOPE void	TkBTreeMakeUndoIndex(const TkSharedText *sharedTextPtr,
			    TkTextSegment *segPtr, TkTextUndoIndex *indexPtr);
MODULE_SCOPE void	TkBTreeUndoIndexToIndex(const TkSharedText *sharedTextPtr,
			    const TkTextUndoIndex *srcPtr, TkTextIndex *dstPtr);
MODULE_SCOPE Tcl_Obj *	TkBTreeUndoTagInspect(const TkSharedText *sharedTextPtr,
			    const TkTextUndoToken *item);
MODULE_SCOPE bool	TkBTreeJoinUndoInsert(TkTextUndoToken *token1, unsigned byteSize1,
			    TkTextUndoToken *token2, unsigned byteSize2);
MODULE_SCOPE bool	TkBTreeJoinUndoDelete(TkTextUndoToken *token1, unsigned byteSize1,
			    TkTextUndoToken *token2, unsigned byteSize2);
MODULE_SCOPE void	TkBTreeReInsertSegment(const TkSharedText *sharedTextPtr,
			    const TkTextUndoIndex *indexPtr, TkTextSegment *segPtr);
MODULE_SCOPE unsigned	TkBTreeLinesTo(TkTextBTree tree, const TkText *textPtr,
			    const TkTextLine *linePtr, int *deviation);
MODULE_SCOPE unsigned	TkBTreePixelsTo(const TkText *textPtr, const TkTextLine *linePtr);
MODULE_SCOPE void	TkBTreeLinkSegment(const TkSharedText *sharedTextPtr,
			    TkTextSegment *segPtr, TkTextIndex *indexPtr);
inline TkTextLine *	TkBTreeGetStartLine(const TkText *textPtr);
inline TkTextLine *	TkBTreeGetLastLine(const TkText *textPtr);
inline TkTextLine *	TkBTreeNextLine(const TkText *textPtr, TkTextLine *linePtr);
inline TkTextLine *	TkBTreePrevLine(const TkText *textPtr, TkTextLine *linePtr);
MODULE_SCOPE bool	TkBTreeMoveForward(TkTextIndex *indexPtr, unsigned byteCount);
MODULE_SCOPE bool	TkBTreeMoveBackward(TkTextIndex *indexPtr, unsigned byteCount);
MODULE_SCOPE bool	TkBTreeNextTag(TkTextSearch *searchPtr);
MODULE_SCOPE bool	TkBTreePrevTag(TkTextSearch *searchPtr);
MODULE_SCOPE TkTextSegment * TkBTreeFindNextTagged(const TkTextIndex *indexPtr1,
			    const TkTextIndex *indexPtr2, const struct TkBitField *discardTags);
MODULE_SCOPE TkTextSegment * TkBTreeFindPrevTagged(const TkTextIndex *indexPtr1,
			    const TkTextIndex *indexPtr2, bool discardSelection);
MODULE_SCOPE TkTextSegment * TkBTreeFindNextUntagged(const TkTextIndex *indexPtr1,
			    const TkTextIndex *indexPtr2, const struct TkBitField *discardTags);
MODULE_SCOPE unsigned	TkBTreeNumPixels(const TkText *textPtr);
MODULE_SCOPE unsigned	TkBTreeSize(const TkTextBTree tree, const TkText *textPtr);
MODULE_SCOPE unsigned	TkBTreeCountSize(const TkTextBTree tree, const TkText *textPtr,
			    const TkTextLine *linePtr1, const TkTextLine *linePtr2);
inline unsigned		TkBTreeCountLines(const TkTextBTree tree, const TkTextLine *linePtr1,
			    const TkTextLine *linePtr2);
MODULE_SCOPE void	TkBTreeStartSearch(const TkTextIndex *index1Ptr,
			    const TkTextIndex *index2Ptr, const TkTextTag *tagPtr,
			    TkTextSearch *searchPtr, TkTextSearchMode mode);
MODULE_SCOPE void	TkBTreeStartSearchBack(const TkTextIndex *index1Ptr,
			    const TkTextIndex *index2Ptr, const TkTextTag *tagPtr,
			    TkTextSearch *searchPtr, TkTextSearchMode mode);
MODULE_SCOPE void	TkBTreeLiftSearch(TkTextSearch *searchPtr);
MODULE_SCOPE bool	TkBTreeTag(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *index1Ptr, const TkTextIndex *index2Ptr,
			    TkTextTag *tagPtr, bool add, TkTextUndoInfo *undoInfo,
			    TkTextTagChangedProc changedProc);
MODULE_SCOPE TkTextTag * TkBTreeClearTags(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *index1Ptr, const TkTextIndex *index2Ptr,
			    TkTextUndoInfo *undoInfo, bool discardSelection,
			    TkTextTagChangedProc changedProc);
MODULE_SCOPE void	TkBTreeUpdateElideInfo(TkText *textPtr, TkTextTag *tagPtr);
MODULE_SCOPE void	TkBTreeUnlinkSegment(const TkSharedText *sharedTextPtr, TkTextSegment *segPtr);
MODULE_SCOPE void	TkBTreeFreeSegment(TkTextSegment *segPtr);
MODULE_SCOPE unsigned	TkBTreeChildNumber(const TkTextBTree tree, const TkTextLine *linePtr,
			    unsigned *depth);
MODULE_SCOPE unsigned	TkBTreeLinesPerNode(const TkTextBTree tree);
MODULE_SCOPE const union TkTextTagSet * TkBTreeRootTagInfo(const TkTextBTree tree);
MODULE_SCOPE void	TkTextBindProc(ClientData clientData, XEvent *eventPtr);
MODULE_SCOPE void	TkTextSelectionEvent(TkText *textPtr);
MODULE_SCOPE int	TkConfigureText(Tcl_Interp *interp, TkText *textPtr, int objc,
			    Tcl_Obj *const objv[]);
MODULE_SCOPE const TkTextSegment * TkTextGetUndeletableNewline(const TkTextLine *lastLinePtr);
MODULE_SCOPE void	TkTextPerformWatchCmd(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const char *operation,
			    TkTextWatchGetIndexProc index1Proc, ClientData index1ProcData,
			    TkTextWatchGetIndexProc index2Proc, ClientData index2ProcData,
			    const char *arg1, const char *arg2, const char *arg3, bool userFlag);
MODULE_SCOPE bool	TkTextTriggerWatchCmd(TkText *textPtr, const char *operation,
			    const char *index1, const char *index2, const char *arg1, const char *arg2,
			    const char *arg3, bool userFlag);
MODULE_SCOPE void	TkTextUpdateAlteredFlag(TkSharedText *sharedTextPtr);
MODULE_SCOPE bool	TkTextIndexBbox(TkText *textPtr,
			    const TkTextIndex *indexPtr, bool extents, int *xPtr, int *yPtr,
			    int *widthPtr, int *heightPtr, int *charWidthPtr, Tcl_UniChar *thisChar);
MODULE_SCOPE int	TkTextCharLayoutProc(const TkTextIndex *indexPtr, TkTextSegment *segPtr,
			    int byteOffset, int maxX, int maxBytes, bool noCharsYet,
			    TkWrapMode wrapMode, TkTextSpaceMode spaceMode, TkTextDispChunk *chunkPtr);
MODULE_SCOPE void	TkTextCreateDInfo(TkText *textPtr);
MODULE_SCOPE bool	TkTextGetDLineInfo(TkText *textPtr, const TkTextIndex *indexPtr,
			    bool extents, int *xPtr, int *yPtr, int *widthPtr, int *heightPtr,
			    int *basePtr);
MODULE_SCOPE int	TkTextBindEvent(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[],
			     TkSharedText *sharedTextPtr, Tk_BindingTable *bindingTablePtr,
			     const char *name);
MODULE_SCOPE void	TkTextTagFindStartOfRange(TkText *textPtr, const TkTextTag *tagPtr,
			    const TkTextIndex *currentPtr, TkTextIndex *resultPtr);
MODULE_SCOPE void	TkTextTagFindEndOfRange(TkText *textPtr, const TkTextTag *tagPtr,
			    const TkTextIndex *currentPtr, TkTextIndex *resultPtr);
MODULE_SCOPE TkTextTag * TkTextClearTags(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2,
			    bool discardSelection);
MODULE_SCOPE void	TkTextClearSelection(TkSharedText *sharedTextPtr,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
MODULE_SCOPE void	TkTextUpdateTagDisplayFlags(TkTextTag *tagPtr);
MODULE_SCOPE TkTextTag * TkTextCreateTag(TkText *textPtr, const char *tagName, bool *newTag);
MODULE_SCOPE TkTextTag * TkTextFindTag(const TkText *textPtr, const char *tagName);
MODULE_SCOPE int	TkConfigureTag(Tcl_Interp *interp, TkText *textPtr, const char *tagName,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE void	TkTextEnableTag(TkSharedText *sharedTextPtr, TkTextTag *tagPtr);
MODULE_SCOPE void	TkTextSortTags(unsigned numTags, TkTextTag **tagArrayPtr);
MODULE_SCOPE void	TkTextFreeDInfo(TkText *textPtr);
MODULE_SCOPE void	TkTextResetDInfo(TkText *textPtr);
MODULE_SCOPE void	TkTextDeleteBreakInfoTableEntries(Tcl_HashTable *breakInfoTable);
MODULE_SCOPE void	TkTextPushTagPriorityUndo(TkSharedText *sharedTextPtr, TkTextTag *tagPtr,
			    unsigned priority);
MODULE_SCOPE void	TkTextPushTagPriorityRedo(TkSharedText *sharedTextPtr, TkTextTag *tagPtr,
			    unsigned priority);
MODULE_SCOPE void	TkTextInspectUndoTagItem(const TkSharedText *sharedTextPtr,
			    const TkTextTag *tagPtr, Tcl_Obj* objPtr);
MODULE_SCOPE void	TkTextTagAddRetainedUndo(TkSharedText *sharedTextPtr, TkTextTag *tagPtr);
MODULE_SCOPE void	TkTextPushUndoTagTokens(TkSharedText *sharedTextPtr, TkTextTag *tagPtr);
MODULE_SCOPE void	TkTextReleaseUndoTagToken(TkSharedText *sharedTextPtr, TkTextTag *tagPtr);
MODULE_SCOPE void	TkTextPushUndoMarkTokens(TkSharedText *sharedTextPtr,
			    TkTextMarkChange *changePtr);
MODULE_SCOPE void	TkTextReleaseUndoMarkTokens(TkSharedText *sharedTextPtr,
			    TkTextMarkChange *changePtr);
MODULE_SCOPE void	TkTextInspectUndoMarkItem(const TkSharedText *sharedTextPtr,
			    const TkTextMarkChange *changePtr, Tcl_Obj* objPtr);
MODULE_SCOPE bool	TkTextTagChangedUndoRedo(const TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *index1Ptr, const TkTextIndex *index2Ptr,
			    const TkTextTag *tagPtr, bool affectsDisplayGeometry);
MODULE_SCOPE void	TkTextReplaceTags(TkText *textPtr, TkTextSegment *segPtr, bool undoable,
			    Tcl_Obj *tagListPtr);
MODULE_SCOPE void	TkTextFindTags(Tcl_Interp *interp, TkText *textPtr, const TkTextSegment *segPtr,
			    bool discardSelection);
MODULE_SCOPE bool	TkTextDeleteTag(TkText *textPtr, TkTextTag *tagPtr, Tcl_HashEntry *hPtr);
MODULE_SCOPE void	TkTextReleaseTag(TkSharedText *sharedTextPtr, TkTextTag *tagPtr,
			    Tcl_HashEntry *hPtr);
MODULE_SCOPE void	TkTextFontHeightChanged(TkText *textPtr);
MODULE_SCOPE int	TkTextTestRelation(Tcl_Interp *interp, int relation, const char *op);
MODULE_SCOPE int	TkTextAttemptToModifyDisabledWidget(Tcl_Interp *interp);
MODULE_SCOPE int	TkTextAttemptToModifyDeadWidget(Tcl_Interp *interp);
MODULE_SCOPE bool	TkTextReleaseIfDestroyed(TkText *textPtr);
MODULE_SCOPE bool	TkTextDecrRefCountAndTestIfDestroyed(TkText *textPtr);
MODULE_SCOPE void	TkTextFreeAllTags(TkText *textPtr);
inline bool		TkTextGetIndexFromObj(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *objPtr,
			    TkTextIndex *indexPtr);
MODULE_SCOPE TkTextTabArray * TkTextGetTabs(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *stringPtr);
MODULE_SCOPE void	TkTextInspectOptions(TkText *textPtr, const void *recordPtr,
			    Tk_OptionTable optionTable, Tcl_DString *result, int flags);
MODULE_SCOPE void	TkTextFindDisplayLineStartEnd(TkText *textPtr, TkTextIndex *indexPtr, bool end);
MODULE_SCOPE unsigned	TkTextCountDisplayLines(TkText *textPtr, const TkTextIndex *indexFrom,
			    const TkTextIndex *indexTo);
MODULE_SCOPE void	TkTextFindDisplayIndex(TkText *textPtr, TkTextIndex *indexPtr,
			    int displayLineOffset, int *xOffset);
MODULE_SCOPE bool	TkTextIndexBackChars(const TkText *textPtr, const TkTextIndex *srcPtr,
			    int count, TkTextIndex *dstPtr, TkTextCountType type);
MODULE_SCOPE Tcl_UniChar TkTextIndexGetChar(const TkTextIndex *indexPtr);
MODULE_SCOPE unsigned	TkTextIndexCountBytes(const TkTextIndex *index1Ptr,
			    const TkTextIndex *index2Ptr);
MODULE_SCOPE unsigned	TkTextIndexCount(const TkText *textPtr,
			    const TkTextIndex *index1Ptr, const TkTextIndex *index2Ptr,
			    TkTextCountType type);
MODULE_SCOPE bool	TkTextIndexForwChars(const TkText *textPtr, const TkTextIndex *srcPtr,
			    int count, TkTextIndex *dstPtr, TkTextCountType type);
MODULE_SCOPE void	TkTextIndexOfX(TkText *textPtr, int x, TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexYPixels(TkText *textPtr, const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextComputeBreakLocations(Tcl_Interp *interp, const char *text, unsigned len,
			    const char *lang, char *brks);
MODULE_SCOPE bool	TkTextTestLangCode(Tcl_Interp *interp, Tcl_Obj *langCodePtr);
MODULE_SCOPE int	TkTextParseHyphenRules(TkText *textPtr, Tcl_Obj *objPtr, int *rulesPtr);
MODULE_SCOPE void	TkTextLostSelection(ClientData clientData);
MODULE_SCOPE void	TkTextConfigureUndoStack(TkSharedText *sharedTextPtr, int maxUndoDepth,
			    int maxByteSize);
MODULE_SCOPE void	TkTextConfigureRedoStack(TkSharedText *sharedTextPtr, int maxRedoDepth);
MODULE_SCOPE void	TkTextPushUndoToken(TkSharedText *sharedTextPtr, void *token,
			    unsigned byteSize);
MODULE_SCOPE void	TkTextPushRedoToken(TkSharedText *sharedTextPtr, void *token,
			    unsigned byteSize);
MODULE_SCOPE void	TkTextUndoAddMoveSegmentItem(TkSharedText *sharedTextPtr,
			    TkTextSegment *oldPos, TkTextSegment *newPos);
MODULE_SCOPE TkTextIndex * TkTextMakeCharIndex(TkTextBTree tree, TkText *textPtr,
			    int lineIndex, int charIndex, TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextSegmentIsElided(const TkText *textPtr, const TkTextSegment *segPtr);
MODULE_SCOPE void	TkTextDispAllocStatistic();
MODULE_SCOPE bool	TkTextLineIsElided(const TkSharedText *sharedTextPtr, const TkTextLine *linePtr,
			    const TkText *textPtr);
MODULE_SCOPE bool	TkTextIsElided(const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextTestTag(const TkTextIndex *indexPtr, const TkTextTag *tagPtr);
inline bool		TkTextIsDeadPeer(const TkText *textPtr);
MODULE_SCOPE void	TkTextGenerateWidgetViewSyncEvent(TkText *textPtr, bool sendImmediately);
MODULE_SCOPE void	TkTextRunAfterSyncCmd(TkText *textPtr);
MODULE_SCOPE void	TkTextInvalidateLineMetrics(TkSharedText *sharedTextPtr, TkText *textPtr,
			    TkTextLine *linePtr, unsigned lineCount, TkTextInvalidateAction action);
MODULE_SCOPE void	TkTextUpdateLineMetrics(TkText *textPtr, unsigned lineNum, unsigned endLine);
MODULE_SCOPE int	TkTextMarkCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE TkTextSegment * TkTextFindMark(const TkText *textPtr, const char *name);
MODULE_SCOPE TkTextSegment * TkTextFreeMarks(TkSharedText *sharedTextPtr, bool retainPrivateMarks);
MODULE_SCOPE bool	TkTextMarkNameToIndex(TkText *textPtr, const char *name, TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextMarkSegToIndex(TkText *textPtr,
			    TkTextSegment *markPtr, TkTextIndex *indexPtr);
MODULE_SCOPE TkTextSegment * TkTextMakeStartEndMark(TkText *textPtr, Tk_SegType const *typePtr);
MODULE_SCOPE TkTextSegment * TkTextMakeMark(TkText *textPtr, const char *name);
MODULE_SCOPE TkTextSegment * TkTextMakeNewMark(TkSharedText *sharedTextPtr, const char *name);
MODULE_SCOPE void	TkTextUnsetMark(TkText *textPtr, TkTextSegment *markPtr);
inline bool		TkTextIsStartEndMarker(const TkTextSegment *segPtr);
inline bool		TkTextIsSpecialMark(const TkTextSegment *segPtr);
inline bool		TkTextIsPrivateMark(const TkTextSegment *segPtr);
inline bool		TkTextIsSpecialOrPrivateMark(const TkTextSegment *segPtr);
inline bool		TkTextIsNormalOrSpecialMark(const TkTextSegment *segPtr);
inline bool		TkTextIsNormalMark(const TkTextSegment *segPtr);
inline bool		TkTextIsStableMark(const TkTextSegment *segPtr);
MODULE_SCOPE const char * TkTextMarkName(const TkSharedText *sharedTextPtr, const TkText *textPtr,
			    const TkTextSegment *markPtr);
MODULE_SCOPE void	TkTextUpdateCurrentMark(TkSharedText *sharedTextPtr);
MODULE_SCOPE void	TkTextSaveCursorIndex(TkText *textPtr);
MODULE_SCOPE bool	TkTextTriggerWatchCursor(TkText *textPtr);
MODULE_SCOPE void	TkTextInsertGetBBox(TkText *textPtr, int x, int y, int height, XRectangle *bbox);
MODULE_SCOPE bool	TkTextDrawBlockCursor(TkText *textPtr);
MODULE_SCOPE bool	TkTextGetCursorBbox(TkText *textPtr, int *x, int *y, int *w, int *h);
MODULE_SCOPE unsigned	TkTextGetCursorWidth(TkText *textPtr, int *x, int *offs);
MODULE_SCOPE void	TkTextEventuallyRepick(TkText *textPtr);
MODULE_SCOPE bool	TkTextPendingSync(const TkText *textPtr);
MODULE_SCOPE void	TkTextPickCurrent(TkText *textPtr, XEvent *eventPtr);
MODULE_SCOPE union TkTextTagSet * TkTextGetTagSetFromChunk(const TkTextDispChunk *chunkPtr);
MODULE_SCOPE int	TkTextGetXPixelFromChunk(const TkText *textPtr,
			    const TkTextDispChunk *chunkPtr);
MODULE_SCOPE int	TkTextGetYPixelFromChunk(const TkText *textPtr,
			    const TkTextDispChunk *chunkPtr);
inline const TkTextDispChunk * TkTextGetFirstChunkOfNextDispLine(const TkTextDispChunk *chunkPtr);
inline const TkTextDispChunk * TkTextGetLastChunkOfPrevDispLine(const TkTextDispChunk *chunkPtr);
MODULE_SCOPE int	TkTextGetFirstXPixel(const TkText *textPtr);
MODULE_SCOPE int	TkTextGetFirstYPixel(const TkText *textPtr);
MODULE_SCOPE int	TkTextGetLastXPixel(const TkText *textPtr);
MODULE_SCOPE int	TkTextGetLastYPixel(const TkText *textPtr);
MODULE_SCOPE unsigned	TkTextCountVisibleImages(const TkText *textPtr);
MODULE_SCOPE unsigned	TkTextCountVisibleWindows(const TkText *textPtr);
MODULE_SCOPE const TkTextDispChunk * TkTextPixelIndex(TkText *textPtr, int x, int y,
			    TkTextIndex *indexPtr, bool *nearest);
MODULE_SCOPE Tcl_Obj *	TkTextNewIndexObj(const TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextRedrawRegion(TkText *textPtr, int x, int y, int width, int height);
MODULE_SCOPE bool	TkTextRedrawTag(const TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *index1Ptr, const TkTextIndex *index2Ptr,
			    const TkTextTag *tagPtr, bool affectsDisplayGeometry);
MODULE_SCOPE void	TkTextRelayoutWindow(TkText *textPtr, int mask);
MODULE_SCOPE void	TkTextCheckLineMetricUpdate(const TkText *textPtr);
MODULE_SCOPE void	TkTextCheckDisplayLineConsistency(const TkText *textPtr);
MODULE_SCOPE int	TkTextScanCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE int	TkTextSeeCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE void	TkTextSetYView(TkText *textPtr, TkTextIndex *indexPtr, int pickPlace);
MODULE_SCOPE int	TkTextTagCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE int	TkTextImageCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE bool	TkTextImageIndex(TkText *textPtr, const char *name, TkTextIndex *indexPtr);
MODULE_SCOPE TkTextSegment * TkTextMakeImage(TkText *textPtr, Tcl_Obj *options);
MODULE_SCOPE int	TkTextWindowCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE bool	TkTextWindowIndex(TkText *textPtr, const char *name, TkTextIndex *indexPtr);
MODULE_SCOPE TkTextSegment * TkTextMakeWindow(TkText *textPtr, Tcl_Obj *options);
MODULE_SCOPE int	TkTextYviewCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE void	TkTextGetViewOffset(TkText *textPtr, int *x, int *y);
MODULE_SCOPE void	TkTextWinFreeClient(Tcl_HashEntry *hPtr, TkTextEmbWindowClient *client);
MODULE_SCOPE void	TkTextIndexSetPosition(TkTextIndex *indexPtr,
			    int byteIndex, TkTextSegment *segPtr);
MODULE_SCOPE int	TkTextSegToIndex(const TkTextSegment *segPtr);
MODULE_SCOPE bool	TkTextIndexGetFromString(Tcl_Interp *interp, struct TkText *textPtr,
			    const char *string, unsigned lengthOfString, struct TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexPrint(const TkSharedText *sharedTextPtr, const TkText *textPtr,
				const struct TkTextIndex *indexPtr, char *string);
MODULE_SCOPE void	TkTextIndexSetByteIndex(TkTextIndex *indexPtr, int byteIndex);
MODULE_SCOPE void	TkTextIndexSetByteIndex2(TkTextIndex *indexPtr,
			    TkTextLine *linePtr, int byteIndex);
inline void		TkTextIndexSetEpoch(TkTextIndex *indexPtr, unsigned epoch);
MODULE_SCOPE void	TkTextIndexSetSegment(TkTextIndex *indexPtr, TkTextSegment *segPtr);
inline void		TkTextIndexSetPeer(TkTextIndex *indexPtr, TkText *textPtr);
MODULE_SCOPE bool	TkTextIndexIsEmpty(const TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextIndexSetLine(TkTextIndex *indexPtr, TkTextLine *linePtr);
MODULE_SCOPE void	TkTextIndexSetToStartOfLine(TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextIndexSetToStartOfLine2(TkTextIndex *indexPtr, TkTextLine *linePtr);
MODULE_SCOPE void	TkTextIndexSetToEndOfLine2(TkTextIndex *indexPtr, TkTextLine *linePtr);
MODULE_SCOPE void	TkTextIndexSetToLastChar(TkTextIndex *indexPtr);
inline void		TkTextIndexSetToLastChar2(TkTextIndex *indexPtr, TkTextLine *linePtr);
MODULE_SCOPE void	TkTextIndexSetupToStartOfText(TkTextIndex *indexPtr, TkText *textPtr,
			    TkTextBTree tree);
MODULE_SCOPE void	TkTextIndexSetupToEndOfText(TkTextIndex *indexPtr, TkText *textPtr,
			    TkTextBTree tree);
MODULE_SCOPE bool	TkTextIndexAddToByteIndex(TkTextIndex *indexPtr, int numBytes);
inline TkTextLine *	TkTextIndexGetLine(const TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexGetByteIndex(const TkTextIndex *indexPtr);
MODULE_SCOPE unsigned	TkTextIndexGetLineNumber(const TkTextIndex *indexPtr, const TkText *textPtr);
inline TkTextSegment *	TkTextIndexGetSegment(const TkTextIndex *indexPtr);
MODULE_SCOPE TkTextSegment * TkTextIndexGetContentSegment(const TkTextIndex *indexPtr, int *offset);
MODULE_SCOPE TkTextSegment * TkTextIndexGetFirstSegment(const TkTextIndex *indexPtr, int *offset);
inline TkSharedText *	TkTextIndexGetShared(const TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextIndexClear(TkTextIndex *indexPtr, TkText *textPtr);
MODULE_SCOPE void	TkTextIndexClear2(TkTextIndex *indexPtr, TkText *textPtr, TkTextBTree tree);
inline void		TkTextIndexInvalidate(TkTextIndex *indexPtr);
MODULE_SCOPE void	TkTextIndexToByteIndex(TkTextIndex *indexPtr);
inline void		TkTextIndexMakePersistent(TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexIsZero(const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexIsStartOfLine(const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexIsEndOfLine(const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexIsStartOfText(const TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexIsEndOfText(const TkTextIndex *indexPtr);
inline bool		TkTextIndexSameLines(const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
MODULE_SCOPE bool	TkTextIndexIsEqual(const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
MODULE_SCOPE int	TkTextIndexCompare(const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
inline void		TkTextIndexSave(TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexRebuild(TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexRestrictToStartRange(TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexRestrictToEndRange(TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextIndexEnsureBeforeLastChar(TkTextIndex *indexPtr);
MODULE_SCOPE bool	TkTextSkipElidedRegion(TkTextIndex *indexPtr);

/*
 * Debugging info macros:
 */

#if defined(NDEBUG) || defined(TK_TEXT_NDEBUG)
# define TK_BTREE_DEBUG(expr)
#else
# define TK_BTREE_DEBUG(expr)	{ if (tkBTreeDebug) { expr; } }
#endif

#define TK_TEXT_DEBUG(expr)	{ if (tkTextDebug) { expr; } }

/*
 * Backport definitions for Tk 8.6/8.5.
 */

#if TK_MAJOR_VERSION == 8 && TK_MINOR_VERSION < 7

# if TCL_UTF_MAX > 4
#  define TkUtfToUniChar Tcl_UtfToUniChar
# else /* if TCL_UTF_MAX <= 4 */
inline int TkUtfToUniChar(const char *src, int *chPtr);
# endif /* TCL_UTF_MAX > 4 */

#endif /* end of backport for 8.6/8.5 */

/*
 * Backport definitions for Tk 8.5. Tk 8.6/8.7 under Mac OS X has event loop
 * issues, so backporting is important.
 */

#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION == 5

#ifndef CLANG_ASSERT
# define CLANG_ASSERT(expr) assert(expr)
#endif

#ifndef DEF_TEXT_INSERT_UNFOCUSSED
# define DEF_TEXT_INSERT_UNFOCUSSED "none"
#endif

MODULE_SCOPE struct TkTextSegment * TkTextSetMark(struct TkText *textPtr, const char *name,
			    struct TkTextIndex *indexPtr);
MODULE_SCOPE int	TkBTreeNumLines(TkTextBTree tree, const struct TkText *textPtr);
MODULE_SCOPE int	TkTextGetIndex(Tcl_Interp *interp, struct TkText *textPtr,
			    const char *string, struct TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextIndexBackBytes(const struct TkText *textPtr,
			    const struct TkTextIndex *srcPtr, int count, struct TkTextIndex *dstPtr);
MODULE_SCOPE int	TkTextIndexForwBytes(const struct TkText *textPtr,
			    const struct TkTextIndex *srcPtr, int count, struct TkTextIndex *dstPtr);
MODULE_SCOPE struct TkTextIndex *TkTextMakeByteIndex(TkTextBTree tree, const struct TkText *textPtr,
			    int lineIndex, int byteIndex, struct TkTextIndex *indexPtr);
MODULE_SCOPE int	TkTextPrintIndex(const struct TkText *textPtr,
			    const struct TkTextIndex *indexPtr, char *string);
MODULE_SCOPE int	TkTextXviewCmd(struct TkText *textPtr, Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
MODULE_SCOPE void	TkTextChanged(struct TkSharedText *sharedTextPtr, struct TkText *textPtr,
			    const struct TkTextIndex *index1Ptr, const struct TkTextIndex *index2Ptr);
MODULE_SCOPE int	TkBTreeNumLines(TkTextBTree tree, const struct TkText *textPtr);
MODULE_SCOPE void	TkTextInsertDisplayProc(struct TkText *textPtr, struct TkTextDispChunk *chunkPtr,
			    int x, int y, int height, int baseline, Display *display, Drawable dst,
			    int screenY);

# define TkNewWindowObj(tkwin) Tcl_NewStringObj(Tk_PathName(tkwin), -1)
# define Tcl_BackgroundException(interp, code) Tcl_BackgroundError(interp)

/*
 * Windows needs this.
 */

# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLIMPORT

#endif /* TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION == 5 */

#ifdef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkTextPriv.h"
#endif

#endif /* _TKTEXT */
/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
