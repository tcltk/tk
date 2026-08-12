/**
 * 2026-07-30
 *
 * The Mojibake shell
 *
 * https://mojibake.zaerl.com
 * https://github.com/zaerl/mojibake
 *
 * This file is an amalgamation of all Mojibake shell source files. It is
 * automatically generated. Do not edit. If you want to generate it, run
 * `make amalgamation`
 *
 * MIT License
 * 
 * Copyright (c) 2021-present, Francesco Bigiarini
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */

#include "mojibake.h"

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#ifndef MJB_SHELL_H
#define MJB_SHELL_H

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include "../utf16.h"
    #include "getopt/getopt.h"
    #include <io.h>
    #include <windows.h>
    #define isatty _isatty
    #ifndef STDOUT_FILENO
        #define STDOUT_FILENO _fileno(stdout)
    #endif
#else
    #include <getopt.h>
    #include <signal.h>
    #include <sys/select.h>
    #include <termios.h>
    #include <unistd.h>
#endif
// clang-format on

#include "mojibake.h"

typedef void (*mjbsh_screen_fn)(const char *input);

typedef enum {
    MJBSH_KEY_UP,
    MJBSH_KEY_DOWN,
    MJBSH_KEY_LEFT,
    MJBSH_KEY_RIGHT
} mjbsh_key;

typedef void (*mjbsh_key_fn)(mjbsh_key key);

typedef enum {
    INTERPRET_MODE_CODEPOINT,
    INTERPRET_MODE_CHARACTER
} mjbsh_interpret_mode;

typedef enum {
    OUTPUT_MODE_PLAIN,
    OUTPUT_MODE_JSON
} mjbsh_output_mode;

typedef int (*mjbsh_command_function)(int argc, char *const argv[], unsigned int flags);

typedef struct mjb_command {
    const char *name;
    const char *description;
    mjbsh_command_function function;
    unsigned int flags;
    int max_arguments;
    bool accepts_codepoint_list;
} mjbsh_command;

extern int cmd_show_colors;
extern bool cmd_show_allowed_symbols;
extern unsigned int cmd_verbose;
extern mjbsh_interpret_mode cmd_interpret_mode;
extern mjbsh_output_mode cmd_output_mode;
extern unsigned int cmd_json_indent;

bool mjbsh_print_escaped_character(const char *buffer_utf8);
void mjbsh_print_json_result(const char *output, size_t output_size);
void mjbsh_print_codepoint(mjb_codepoint codepoint);
void mjbsh_print_break_symbol(mjb_break_type bt);

// Color formatting helper functions
const char *mjbsh_green(void);
const char *mjbsh_red(void);
const char *mjbsh_yellow(void);
const char *mjbsh_reset(void);
void mjbsh_show_cursor(bool show);

void mjbsh_value(const char *label, unsigned int nl, const char *format, ...);
void mjbsh_null(const char *label, unsigned int nl);
void mjbsh_bool(const char *label, unsigned int nl, bool value);
void mjbsh_numeric(const char *label, unsigned int nl, unsigned int value);
void mjbsh_id_name(const char *label, unsigned int id, const char *name, unsigned int nl);
void mjbsh_normalization(const char *buffer_utf8, size_t utf8_length, mjb_normalization form,
    const char *name, const char *label, unsigned int nl);
void mjbsh_codepoint(const char *label, unsigned int nl, mjb_codepoint codepoint);
int mjbsh_error(const char *format, ...);

bool mjbsh_parse_codepoint(const char *input, mjb_codepoint *codepoint);

const char *mjbsh_ji(void);
const char *mjbsh_jnl(void);

// Utils
mjb_codepoint mjbsh_control_picture_codepoint(mjb_codepoint codepoint);
bool mjbsh_property_is_bool(mjb_property property);

// Maps
const char *mjbsh_category_name(mjb_category category);
char *mjbsh_ccc_name(mjb_canonical_combining_class ccc);
const char *mjbsh_bidi_name(mjb_bidi_class bidi);
const char *mjbsh_decomposition_name(mjb_decomposition decomposition);
// const char *mjbsh_line_breaking_class_name(mjb_line_breaking_class line_breaking_class);
const char *mjbsh_east_asian_width_name(mjb_east_asian_width east_asian_width);

// Characters
bool mjbsh_next_character(mjb_character *character, mjb_character_position type);
bool mjbsh_next_array_character(mjb_character *character, mjb_character_position type);
bool mjbsh_next_string_character(mjb_character *character, mjb_character_position type);
bool mjbsh_next_escaped_character(mjb_character *character, mjb_character_position type);

// Screen
void mjbsh_clear_screen(void);
void mjbsh_screen_mode(mjbsh_screen_fn fn, mjbsh_key_fn key_fn);

// Commands
int mjbsh_bidi_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_break_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_case_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_character_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_codepoint_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_emoji_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_filter_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_locale_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_normalize_command(int argc, char *const argv[], unsigned int flags);
int mjbsh_normalize_string_command(int argc, char *const argv[], unsigned int flags);

#endif // MJB_SHELL_H

#include <errno.h>

// ----------
// Start of sources
// ----------

// ----------
// getopt/getopt.h
// ----------

/**
 * The Mojibake library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 *
 * getopt.h - Windows compatibility layer for getopt
 *
 * This is a minimal implementation of getopt/getopt_long for Windows.
 * Based on public domain implementations.
 */

#ifdef _WIN32

// #pragma once

#ifndef MJB_GETOPT_H
#define MJB_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

// External variables used by getopt
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

// Option structure for getopt_long
struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

// Argument types
#define no_argument 0
#define required_argument 1
#define optional_argument 2

// Function declarations
int getopt(int argc, char *const argv[], const char *optstring);
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts,
    int *longindex);

#ifdef __cplusplus
}
#endif

#endif // MJB_GETOPT_H

#endif // _WIN32

// ----------
// getopt/getopt.c
// ----------

/**
 * The Mojibake library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 *
 * getopt.h - Windows compatibility layer for getopt
 *
 * This is a minimal implementation of getopt/getopt_long for Windows.
 * Based on public domain implementations.
 */

#ifdef _WIN32

// #include <stdio.h>
// #include <string.h>

// #include "getopt.h"

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

static char *nextchar = NULL;

int getopt(int argc, char *const argv[], const char *optstring) {
    if(optind == 0) {
        optind = 1;
    }

    optarg = NULL;

    if(nextchar == NULL || *nextchar == '\0') {
        if(optind >= argc || argv[optind] == NULL || argv[optind][0] != '-' ||
            argv[optind][1] == '\0') {
            return -1;
        }

        if(strcmp(argv[optind], "--") == 0) {
            ++optind;

            return -1;
        }

        nextchar = argv[optind] + 1;
        ++optind;
    }

    char c = *nextchar++;
    const char *option = strchr(optstring, c);

    if(option == NULL || c == ':') {
        optopt = c;

        if(opterr && *optstring != ':') {
            fprintf(stderr, "Unknown option: -%c\n", c);
        }

        return '?';
    }

    if(option[1] == ':') {
        if(*nextchar != '\0') {
            optarg = nextchar;
            nextchar = NULL;
        } else if(optind < argc) {
            optarg = argv[optind++];
        } else {
            optopt = c;

            if(opterr && *optstring != ':') {
                fprintf(stderr, "Option requires an argument: -%c\n", c);
            }

            return (optstring[0] == ':') ? ':' : '?';
        }
    }

    return c;
}

int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts,
    int *longindex) {
    if(optind == 0) {
        optind = 1;
    }

    optarg = NULL;

    if(optind >= argc || argv[optind] == NULL || argv[optind][0] != '-') {
        return -1;
    }

    if(strcmp(argv[optind], "--") == 0) {
        ++optind;

        return -1;
    }

    // Handle long options (--option)
    if(argv[optind][0] == '-' && argv[optind][1] == '-') {
        const char *name = argv[optind] + 2;
        const char *equals = strchr(name, '=');
        size_t name_len = equals ? (size_t)(equals - name) : strlen(name);

        for(int i = 0; longopts[i].name != NULL; i++) {
            if(strncmp(name, longopts[i].name, name_len) == 0 &&
                strlen(longopts[i].name) == name_len) {
                if(longindex) {
                    *longindex = i;
                }

                ++optind;

                if(longopts[i].has_arg == required_argument ||
                    longopts[i].has_arg == optional_argument) {
                    if(equals) {
                        optarg = (char *)(equals + 1);
                    } else if(longopts[i].has_arg == required_argument) {
                        if(optind < argc) {
                            optarg = argv[optind++];
                        } else {
                            if(opterr) {
                                fprintf(stderr, "Option --%s requires an argument\n",
                                    longopts[i].name);
                            }

                            return '?';
                        }
                    }
                }

                if(longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;

                    return 0;
                } else {
                    return longopts[i].val;
                }
            }
        }

        if(opterr) {
            fprintf(stderr, "Unknown option: %s\n", argv[optind]);
        }

        ++optind;

        return '?';
    }

    // Handle short options (fallback to getopt())
    return getopt(argc, argv, optstring);
}

#endif // _WIN32

// ----------
// characters.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "shell.h"

bool mjbsh_next_character(mjb_character *character, mjb_character_position type) {
    printf("%sU+%04X%s%s", mjbsh_green(), (unsigned int)character->codepoint, mjbsh_reset(),
        (type & MJB_POSITION_LAST) ? "" : " ");

    return true;
}

bool mjbsh_next_array_character(mjb_character *character, mjb_character_position type) {
    printf("%u%s", character->codepoint, (type & MJB_POSITION_LAST) ? "" : ", ");

    return true;
}

bool mjbsh_next_string_character(mjb_character *character, mjb_character_position type) {
    char buffer_utf8[5];
    size_t size = mjb_codepoint_encode(character->codepoint, buffer_utf8, 5, MJB_ENC_UTF_8);

    if(!size) {
        return false;
    }

    printf("%s", buffer_utf8);

    return true;
}

bool mjbsh_next_escaped_character(mjb_character *character, mjb_character_position type) {
    char buffer_utf8[5];
    size_t size = mjb_codepoint_encode(character->codepoint, buffer_utf8, 5, MJB_ENC_UTF_8);

    if(!size) {
        return false;
    }

    if(!mjbsh_print_escaped_character(buffer_utf8)) {
        printf("%s", buffer_utf8);
    }

    return true;
}

// ----------
// maps.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "shell.h"

// # @missing: 0000..10FFFF; General_Category; Unassigned
static const char *mjbsh_category_names[] = {
    "Other, not assigned",        // MJB_CATEGORY_CN
    "Letter, uppercase",          // MJB_CATEGORY_LU
    "Letter, lowercase",          // MJB_CATEGORY_LL
    "Letter, titlecase",          // MJB_CATEGORY_LT
    "Letter, modifier",           // MJB_CATEGORY_LM
    "Letter, other",              // MJB_CATEGORY_LO
    "Mark, non-spacing",          // MJB_CATEGORY_MN
    "Mark, spacing combining",    // MJB_CATEGORY_MC
    "Mark, enclosing",            // MJB_CATEGORY_ME
    "Number, decimal digit",      // MJB_CATEGORY_ND
    "Number, letter",             // MJB_CATEGORY_NL
    "Number, other",              // MJB_CATEGORY_NO
    "Punctuation, connector",     // MJB_CATEGORY_PC
    "Punctuation, dash",          // MJB_CATEGORY_PD
    "Punctuation, open",          // MJB_CATEGORY_PS
    "Punctuation, close",         // MJB_CATEGORY_PE
    "Punctuation, initial quote", // MJB_CATEGORY_PI
    "Punctuation, final quote",   // MJB_CATEGORY_PF
    "Punctuation, other",         // MJB_CATEGORY_PO
    "Symbol, math",               // MJB_CATEGORY_SM
    "Symbol, currency",           // MJB_CATEGORY_SC
    "Symbol, modifier",           // MJB_CATEGORY_SK
    "Symbol, other",              // MJB_CATEGORY_SO
    "Separator, space",           // MJB_CATEGORY_ZS
    "Separator, line",            // MJB_CATEGORY_ZL
    "Separator, paragraph",       // MJB_CATEGORY_ZP
    "Other, control",             // MJB_CATEGORY_CC
    "Other, format",              // MJB_CATEGORY_CF
    "Other, surrogate",           // MJB_CATEGORY_CS
    "Other, private use"          // MJB_CATEGORY_CO
};

static const char *mjbsh_ccc_names[] = {
    "Not Reordered",
    "Overlay",
    NULL,
    NULL,
    NULL,
    NULL,
    "Han Reading",
    "Nukta",
    "Kana Voicing",
    "Virama",
};

static const char *mjbsh_ccc_names_200_to_240[] = {
    "Attached Below Left", // MJB_CCC_ATTACHED_BELOW_LEFT
    NULL,
    "Attached Below", // MJB_CCC_ATTACHED_BELOW
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    "Attached Above", // MJB_CCC_ATTACHED_ABOVE
    NULL, "Attached Above Right", NULL, "Below Left", NULL, "Below", NULL, "Below Right", NULL,
    "Left", NULL, "Right", NULL, "Above Left", NULL, "Above", NULL, "Above Right", "Double Below",
    "Double Above"
};

static const char *mjbsh_bidi_names[] = {
    "None",                       // MJB_BIDI_NONE
    "Left-to-right",              // MJB_BIDI_L
    "Right-to-left",              // MJB_BIDI_R
    "Right-to-left arabic",       // MJB_BIDI_AL
    "European number",            // MJB_BIDI_EN
    "European number separator",  // MJB_BIDI_ES
    "European number terminator", // MJB_BIDI_ET
    "Arabic number",              // MJB_BIDI_AN
    "Common number separator",    // MJB_BIDI_CS
    "Nonspacing mark",            // MJB_BIDI_NSM
    "Boundary neutral",           // MJB_BIDI_BN
    "Paragraph separator",        // MJB_BIDI_B
    "Segment separator",          // MJB_BIDI_S
    "Whitespace",                 // MJB_BIDI_WS
    "Other neutrals",             // MJB_BIDI_ON
    "Left-to-right embedding",    // MJB_BIDI_LRE
    "Left-to-right override",     // MJB_BIDI_LRO
    "Right-to-left embedding",    // MJB_BIDI_RLE
    "Right-to-left override",     // MJB_BIDI_RLO
    "Pop directional format",     // MJB_BIDI_PDF
    "Left-to-right isolate",      // MJB_BIDI_LRI
    "Right-to-left isolate",      // MJB_BIDI_RLI
    "First strong isolate",       // MJB_BIDI_FSI
    "Pop directional isolate",    // MJB_BIDI_PDI
};

static const char *mjbsh_decomposition_names[] = {
    "None",          // MJB_DECOMPOSITION_NONE
    "Canonical",     // MJB_DECOMPOSITION_CANONICAL
    "Circle",        // MJB_DECOMPOSITION_CIRCLE
    "Compatibility", // MJB_DECOMPOSITION_COMPAT
    "Final",         // MJB_DECOMPOSITION_FINAL
    "Font",          // MJB_DECOMPOSITION_FONT
    "Fraction",      // MJB_DECOMPOSITION_FRACTION
    "Initial",       // MJB_DECOMPOSITION_INITIAL
    "Isolated",      // MJB_DECOMPOSITION_ISOLATED
    "Medial",        // MJB_DECOMPOSITION_MEDIAL
    "Narrow",        // MJB_DECOMPOSITION_NARROW
    "No break",      // MJB_DECOMPOSITION_NOBREAK
    "Small",         // MJB_DECOMPOSITION_SMALL
    "Square",        // MJB_DECOMPOSITION_SQUARE
    "Sub",           // MJB_DECOMPOSITION_SUB
    "Super",         // MJB_DECOMPOSITION_SUPER
    "Vertical",      // MJB_DECOMPOSITION_VERTICAL
    "Wide",          // MJB_DECOMPOSITION_WIDE
};

static const char *mjbsh_east_asian_width_names[] = {
    "Not set",    // MJB_EAW_NOT_SET
    "Ambiguous",  // MJB_EAW_AMBIGUOUS
    "Full-width", // MJB_EAW_FULL_WIDTH
    "Half-width", // MJB_EAW_HALF_WIDTH
    "Neutral",    // MJB_EAW_NEUTRAL
    "Narrow",     // MJB_EAW_NARROW
    "Wide"        // MJB_EAW_WIDE
};

const char *mjbsh_decomposition_name(mjb_decomposition decomposition) {
    size_t decomposition_index = (size_t)(unsigned int)decomposition;

    if(decomposition_index > MJB_DECOMPOSITION_WIDE) {
        return "Unknown";
    }

    return mjbsh_decomposition_names[decomposition_index];
}

const char *mjbsh_category_name(mjb_category category) {
    size_t category_index = (size_t)(unsigned int)category;

    if(category_index >= MJB_CATEGORY_COUNT) {
        return "Unknown";
    }

    return mjbsh_category_names[category_index];
}

char *mjbsh_ccc_name(mjb_canonical_combining_class ccc) {
    size_t ccc_index = (size_t)(unsigned int)ccc;

    if(ccc_index > MJB_CCC_BELOW_IOTA) {
        return strdup("Unknown");
    }

    if(ccc_index < MJB_CCC_10) {
        return strdup(mjbsh_ccc_names[ccc_index] ? mjbsh_ccc_names[ccc_index] : "Unknown");
    }

    if(ccc_index <= MJB_CCC_36 || ccc_index == MJB_CCC_84 || ccc_index == MJB_CCC_91 ||
        ccc_index == MJB_CCC_103 || ccc_index == MJB_CCC_107 || ccc_index == MJB_CCC_118 ||
        ccc_index == MJB_CCC_122 || ccc_index == MJB_CCC_129 || ccc_index == MJB_CCC_130 ||
        ccc_index == MJB_CCC_132) {
        char *str = (char *)malloc(8);

        if(str) {
            snprintf(str, 8, "CCC%zu", ccc_index);
        }

        return str;
    }

    if(ccc_index >= MJB_CCC_ATTACHED_BELOW_LEFT && ccc_index <= MJB_CCC_DOUBLE_ABOVE) {
        size_t index = ccc_index - MJB_CCC_ATTACHED_BELOW_LEFT;

        return strdup(mjbsh_ccc_names_200_to_240[index] ? mjbsh_ccc_names_200_to_240[index] :
                                                          "Unknown");
    }

    if(ccc_index == MJB_CCC_BELOW_IOTA) {
        return strdup("Iota_Subscript");
    }

    return strdup(mjbsh_ccc_names[0]);
}

const char *mjbsh_bidi_name(mjb_bidi_class bidi) {
    size_t bidi_index = (size_t)(unsigned int)bidi;

    if(bidi_index >= MJB_BIDI_CLASS_COUNT) {
        return "Unknown";
    }

    return mjbsh_bidi_names[bidi_index];
}

const char *mjbsh_east_asian_width_name(mjb_east_asian_width east_asian_width) {
    size_t east_asian_width_index = (size_t)(unsigned int)east_asian_width;

    if(east_asian_width_index >= MJB_EAW_COUNT) {
        return "Unknown";
    }

    return mjbsh_east_asian_width_names[east_asian_width_index];
}

// ----------
// screen.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "shell.h"

// clang-format off
#ifdef _WIN32
// Windows terminal state structure
typedef struct mjb_terminal_state {
    HANDLE h_stdin;
    DWORD orig_mode;
} terminal_state;
#else
// Unix terminal state structure
typedef struct termios terminal_state;
#endif
// clang-format on

// Global state for signal handling
static bool in_raw_mode = false;
static terminal_state *saved_term_state = NULL;

// Cleanup function to restore terminal and show cursor
static void mjbsh_cleanup_terminal(void) {
    if(in_raw_mode && saved_term_state != NULL) {
        mjbsh_show_cursor(true);
        fflush(stdout);

#ifdef _WIN32
        SetConsoleMode(saved_term_state->h_stdin, saved_term_state->orig_mode);
#else
        tcsetattr(STDIN_FILENO, TCSAFLUSH, saved_term_state);
#endif

        in_raw_mode = false;
        saved_term_state = NULL;
    }
}

#ifdef _WIN32
// Windows console control handler
static BOOL WINAPI mjbsh_console_ctrl_handler(DWORD ctrl_type) {
    switch(ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            mjbsh_cleanup_terminal();

            return FALSE; // Let default handler terminate
        default:
            return FALSE;
    }
}
#else
// POSIX signal handler
static void mjbsh_signal_handler(int signum) {
    mjbsh_cleanup_terminal();

    // Re-raise the signal with default handler
    signal(signum, SIG_DFL);
    raise(signum);
}
#endif

#ifdef _WIN32
static void mjbsh_set_raw_mode(terminal_state *term_state) {
    term_state->h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(term_state->h_stdin, &term_state->orig_mode);

    // Disable echo and line input
    DWORD mode = term_state->orig_mode;
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    SetConsoleMode(term_state->h_stdin, mode);
}

static void mjbsh_restore_mode(terminal_state *term_state) {
    SetConsoleMode(term_state->h_stdin, term_state->orig_mode);
}
#else
static void mjbsh_set_raw_mode(terminal_state *term_state) {
    terminal_state raw = *term_state;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void mjbsh_restore_mode(terminal_state *term_state) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, term_state);
}
#endif

void mjbsh_clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void mjbsh_screen_mode(mjbsh_screen_fn fn, mjbsh_key_fn key_fn) {
    terminal_state term_state;

#ifdef _WIN32
    // Windows-specific initialization
    term_state.h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(term_state.h_stdin, &term_state.orig_mode);
#else
    // POSIX-specific initialization
    tcgetattr(STDIN_FILENO, &term_state);
#endif

    // Set up signal handling
    saved_term_state = &term_state;

#ifdef _WIN32
    SetConsoleCtrlHandler(mjbsh_console_ctrl_handler, TRUE);
#else
    signal(SIGINT, mjbsh_signal_handler);
    signal(SIGTERM, mjbsh_signal_handler);
    signal(SIGHUP, mjbsh_signal_handler);
#endif

    char input_buffer[1024] = { 0 };
    size_t buffer_pos = 0;
    if(fn != NULL) {
        fn("");
    }

    mjbsh_set_raw_mode(&term_state);

    in_raw_mode = true;
    mjbsh_show_cursor(false);

#ifdef _WIN32
    // UTF-16 decode state for surrogate pair tracking across key events
    uint8_t utf16_state = MJB_UTF_ACCEPT;
    mjb_codepoint utf16_cp = 0;
#endif

    while(1) {
#ifdef _WIN32
        INPUT_RECORD ir;
        DWORD events_read;
        DWORD wait_result = WaitForSingleObject(term_state.h_stdin, 10);

        if(wait_result == WAIT_OBJECT_0 &&
            ReadConsoleInput(term_state.h_stdin, &ir, 1, &events_read) && events_read > 0 &&
            ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            WCHAR wc = ir.Event.KeyEvent.uChar.UnicodeChar;
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;

            if(wc == 3) { // Ctrl+C
                break;
            } else if(wc == 0) { // Extended key (arrow keys, function keys, etc.)
                if(key_fn != NULL) {
                    switch(vk) {
                        case VK_UP:
                            key_fn(MJBSH_KEY_UP);
                            break;
                        case VK_DOWN:
                            key_fn(MJBSH_KEY_DOWN);
                            break;
                        case VK_RIGHT:
                            key_fn(MJBSH_KEY_RIGHT);
                            break;
                        case VK_LEFT:
                            key_fn(MJBSH_KEY_LEFT);
                            break;
                        default:
                            break;
                    }
                }
            } else if(wc == 8) { // BACKSPACE
                if(buffer_pos > 0) {
                    --buffer_pos;

                    // Walk back over UTF-8 continuation bytes
                    while(buffer_pos > 0 &&
                        ((unsigned char)input_buffer[buffer_pos] & 0xC0) == 0x80) {
                        --buffer_pos;
                    }

                    input_buffer[buffer_pos] = '\0';

                    if(fn != NULL) {
                        fn(input_buffer);
                    }
                }
            } else {
                // Decode UTF-16 unit to codepoint; handles surrogate pairs across events
                utf16_state = mjb_utf16_decode_step(utf16_state, (uint8_t)(wc & 0xFF),
                    (uint8_t)((wc >> 8) & 0xFF), &utf16_cp, false);

                if(utf16_state == MJB_UTF_ACCEPT) {
                    char utf8[5] = { 0 };
                    unsigned int utf8_len = mjb_codepoint_encode(utf16_cp, utf8, 5, MJB_ENC_UTF_8);

                    if(utf8_len > 0 && buffer_pos + utf8_len < sizeof(input_buffer) - 1) {
                        for(unsigned int i = 0; i < utf8_len; i++) {
                            input_buffer[buffer_pos++] = utf8[i];
                        }

                        input_buffer[buffer_pos] = '\0';

                        if(fn != NULL) {
                            fn(input_buffer);
                        }
                    }
                }
            }
        }
#else
        // Unix input handling with select()
        char c;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

        if(ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t bytes_read = read(STDIN_FILENO, &c, 1);

            if(bytes_read > 0) {
                if(c == 3) { // Ctrl+C
                    break;
                } else if(c == 27) { // Escape sequence (arrow keys, etc.)
                    fd_set seq_fds;
                    FD_ZERO(&seq_fds);
                    FD_SET(STDIN_FILENO, &seq_fds);

                    struct timeval seq_timeout;
                    seq_timeout.tv_sec = 0;
                    seq_timeout.tv_usec = 50000;

                    if(select(STDIN_FILENO + 1, &seq_fds, NULL, NULL, &seq_timeout) > 0) {
                        char seq[2];
                        ssize_t n = read(STDIN_FILENO, seq, 1);

                        if(n > 0 && seq[0] == '[') {
                            n = read(STDIN_FILENO, seq, 1);

                            if(n > 0 && key_fn != NULL) {
                                switch(seq[0]) {
                                    case 'A':
                                        key_fn(MJBSH_KEY_UP);
                                        break;
                                    case 'B':
                                        key_fn(MJBSH_KEY_DOWN);
                                        break;
                                    case 'C':
                                        key_fn(MJBSH_KEY_RIGHT);
                                        break;
                                    case 'D':
                                        key_fn(MJBSH_KEY_LEFT);
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }
                    }
                } else if(c == 127 || c == 8) { // DELETE or BACKSPACE
                    if(buffer_pos > 0) {
                        --buffer_pos;
                        input_buffer[buffer_pos] = '\0';

                        if(fn != NULL) {
                            fn(input_buffer);
                        }
                    }
                } else {
                    if(buffer_pos < sizeof(input_buffer) - 1) {
                        input_buffer[buffer_pos] = c;
                        ++buffer_pos;
                        input_buffer[buffer_pos] = '\0';

                        if(fn != NULL) {
                            fn(input_buffer);
                        }
                    }
                }
            }
        }
#endif
    }

    // Show cursor and restore terminal mode
    in_raw_mode = false;

    mjbsh_show_cursor(true);
    mjbsh_restore_mode(&term_state);
    mjbsh_clear_screen();

    // Restore signal handlers
#ifdef _WIN32
    SetConsoleCtrlHandler(mjbsh_console_ctrl_handler, FALSE);
#else
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
#endif

    saved_term_state = NULL;
}

// ----------
// shell.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "shell.h"

// Global command-line option variables
int cmd_show_colors = 0;
bool cmd_show_allowed_symbols = false;
unsigned int cmd_verbose = 0;
mjbsh_interpret_mode cmd_interpret_mode = INTERPRET_MODE_CHARACTER;
mjbsh_output_mode cmd_output_mode = OUTPUT_MODE_PLAIN;
unsigned int cmd_json_indent = 0;

static mjb_codepoint current_codepoint = MJB_CODEPOINT_NOT_VALID;

// JSON indent levels. Having such an array speeds up the code.
static const char *indents[11] = {
    "",
    " ",
    "  ",
    "    ",
    "      ",
    "        ",
    "          ",
    "            ",
    "              ",
    "                ",
    "                  ",
};

// Color table: [0] = no color, [1] = color enabled
typedef enum {
    MJBSH_COLOR_GREEN = 0,
    MJBSH_COLOR_RED,
    MJBSH_COLOR_YELLOW,
    MJBSH_COLOR_RESET
} mjbsh_color_id;

static const char *mjbsh_color_table[2][4] = { { "", "", "", "" },
    { "\x1B[32m", "\x1B[31m", "\x1B[33m", "\x1B[0m" } };

static inline const char *mjbsh_color(mjbsh_color_id id) {
    return mjbsh_color_table[cmd_show_colors != 0 && cmd_output_mode != OUTPUT_MODE_JSON][(int)id];
}

static void mjbsh_to_json_key(const char *label, char *buf, size_t bufsize) {
    size_t i = 0;
    while(i < bufsize - 1 && label[i] != '\0') {
        char c = label[i];
        if(c == ' ' || c == '-') {
            c = '_';
        } else {
            c = (char)tolower((unsigned char)c);
        }
        buf[i] = c;
        ++i;
    }
    buf[i] = '\0';
}

static void mjbsh_json_field(const char *label) {
    char key[256];
    mjbsh_to_json_key(label, key, sizeof(key));
    printf("%s%s\"%s\":%s", mjbsh_ji(), mjbsh_ji(), key, cmd_json_indent > 0 ? " " : "");
}

static void mjbsh_json_nested_field(const char *label) {
    char key[256];
    mjbsh_to_json_key(label, key, sizeof(key));
    printf("%s%s%s\"%s\":%s", mjbsh_ji(), mjbsh_ji(), mjbsh_ji(), key,
        cmd_json_indent > 0 ? " " : "");
}

static void mjbsh_print_nl(unsigned int nl) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        printf("%s%s", nl >= 1 ? "," : "", mjbsh_jnl());
    } else {
        puts("");
    }
}

static bool mjbsh_next_current_character(mjb_character *character, mjb_character_position type) {
    current_codepoint = character->codepoint;

    return false;
}

bool mjbsh_print_escaped_character(const char *buffer_utf8) {
    unsigned char c = (unsigned char)buffer_utf8[0];

    switch(c) {
        case '"':
            printf("\\\"");
            return true;
        case '\\':
            printf("\\\\");
            return true;
        case '\b':
            printf("\\b");
            return true;
        case '\f':
            printf("\\f");
            return true;
        case '\n':
            printf("\\n");
            return true;
        case '\r':
            printf("\\r");
            return true;
        case '\t':
            printf("\\t");
            return true;
    }

    if(c <= 0x1F) {
        printf("\\u%04X", c);

        return true;
    }

    return false;
}

static void mjbsh_print_json_string(const char *value) {
    if(value == NULL) {
        value = "";
    }

    for(const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        char c = (char)*p;

        if(mjbsh_print_escaped_character(&c)) {
            continue;
        }

        putchar(c);
    }
}

void mjbsh_print_json_result(const char *output, size_t output_size) {
    printf("{%s%s\"output\":%s\"", mjbsh_jnl(), mjbsh_ji(), cmd_json_indent > 0 ? " " : "");

    for(size_t i = 0; output != NULL && i < output_size; ++i) {
        if(!mjbsh_print_escaped_character(output + i)) {
            putchar((unsigned char)output[i]);
        }
    }

    printf("\"%s}\n", mjbsh_jnl());
}

void mjbsh_print_codepoint(mjb_codepoint codepoint) {
    if(codepoint == MJB_CODEPOINT_NOT_VALID) {
        return;
    }

    mjb_codepoint picture_codepoint = mjbsh_control_picture_codepoint(codepoint);
    bool is_control_picture = picture_codepoint != codepoint;

    char buffer_utf8[5];
    mjb_codepoint_encode(picture_codepoint, buffer_utf8, 5, MJB_ENC_UTF_8);

    if(is_control_picture) {
        printf("%s%s%s", mjbsh_yellow(), buffer_utf8, mjbsh_reset());
    } else {
        printf("%s", buffer_utf8);
    }
}

void mjbsh_print_break_symbol(mjb_break_type bt) {
    if(bt == MJB_BT_ALLOWED || bt == MJB_BT_MANDATORY) {
        printf("%s%s%s", mjbsh_green(), bt == MJB_BT_MANDATORY ? "!" : "÷", mjbsh_reset());

        return;
    }

    if(cmd_show_allowed_symbols && bt == MJB_BT_NO_BREAK) {
        printf("%s×%s", mjbsh_red(), mjbsh_reset());
    }
}

// Color formatting helper functions
const char *mjbsh_green(void) {
    return mjbsh_color(MJBSH_COLOR_GREEN);
}

const char *mjbsh_red(void) {
    return mjbsh_color(MJBSH_COLOR_RED);
}

const char *mjbsh_yellow(void) {
    return mjbsh_color(MJBSH_COLOR_YELLOW);
}

const char *mjbsh_reset(void) {
    return mjbsh_color(MJBSH_COLOR_RESET);
}

void mjbsh_show_cursor(bool show) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        return;
    }

    printf("\x1B[?25%s", show ? "h" : "l");
}

static void mjbsh_value_args(const char *label, unsigned int nl, const char *format, va_list args) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_json_field(label);
        printf("\"%s", mjbsh_green());

        va_list length_args;
        va_copy(length_args, args);
        int length = vsnprintf(NULL, 0, format, length_args);
        va_end(length_args);

        if(length >= 0) {
            char *value = (char *)malloc((size_t)length + 1);

            if(value != NULL) {
                va_list value_args;
                va_copy(value_args, args);
                vsnprintf(value, (size_t)length + 1, format, value_args);
                va_end(value_args);

                mjbsh_print_json_string(value);
                free(value);
            }
        }
    } else {
        printf("%s: %s", label, mjbsh_green());

        va_list value_args;
        va_copy(value_args, args);
        vprintf(format, value_args);
        va_end(value_args);
    }

    printf("%s%s", mjbsh_reset(), cmd_output_mode == OUTPUT_MODE_JSON ? "\"" : "");

    mjbsh_print_nl(nl);
}

void mjbsh_value(const char *label, unsigned int nl, const char *format, ...) {
    va_list args;
    va_start(args, format);
    mjbsh_value_args(label, nl, format, args);
    va_end(args);
}

static void mjbsh_print_generic_value(const char *label, unsigned int nl, const char *value) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_json_field(label);
        printf("%s%s%s", mjbsh_green(), value, mjbsh_reset());
    } else {
        printf("%s: %s%s%s", label, mjbsh_green(), value, mjbsh_reset());
    }

    mjbsh_print_nl(nl);
}

void mjbsh_null(const char *label, unsigned int nl) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_generic_value(label, nl, "null");
    } else {
        mjbsh_value(label, nl, "N/A");
    }
}

void mjbsh_bool(const char *label, unsigned int nl, bool value) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_generic_value(label, nl, value ? "true" : "false");
    } else {
        mjbsh_print_generic_value(label, nl, value ? "Y" : "N");
    }
}

void mjbsh_numeric(const char *label, unsigned int nl, unsigned int value) {
    char num_str[32];
    snprintf(num_str, sizeof(num_str), "%u", value);

    mjbsh_print_generic_value(label, nl, num_str);
}

void mjbsh_id_name(const char *label, unsigned int id, const char *name, unsigned int nl) {
    if(name == NULL) {
        name = "Unknown";
    }

    if(cmd_output_mode != OUTPUT_MODE_JSON) {
        mjbsh_value(label, nl, "[%d] %s", id, name);
        return;
    }

    // {"label": {"code": id, "value": "name"}}
    mjbsh_json_field(label);
    printf("{%s", mjbsh_jnl());

    mjbsh_json_nested_field("code");
    printf("%s%u%s,%s", mjbsh_green(), id, mjbsh_reset(), mjbsh_jnl());

    mjbsh_json_nested_field("value");
    printf("\"%s", mjbsh_green());
    mjbsh_print_json_string(name);
    printf("%s\"%s%s%s}", mjbsh_reset(), mjbsh_jnl(), mjbsh_ji(), mjbsh_ji());

    mjbsh_print_nl(nl);
}

void mjbsh_normalization(const char *buffer_utf8, size_t utf8_length, mjb_normalization form,
    const char *name, const char *label, unsigned int nl) {
    bool is_json = cmd_output_mode == OUTPUT_MODE_JSON;

    mjb_result result;
    bool ret = mjb_normalize(buffer_utf8, utf8_length, MJB_ENC_UTF_8, form, MJB_ENC_UTF_8,
                   &result) == MJB_STATUS_OK;

    if(ret) {
        if(is_json) {
            printf("%s%s\"%s\":%s\"%s", mjbsh_ji(), mjbsh_ji(), name,
                cmd_json_indent == 0 ? "" : " ", mjbsh_green());
            if(result.output_size > 0 &&
                mjb_for_each_character(result.output, result.output_size, MJB_ENC_UTF_8,
                    mjbsh_next_escaped_character) != MJB_STATUS_OK) {
                goto cleanup;
            }
            printf("%s\",%s", mjbsh_reset(), mjbsh_jnl());
        } else {
            mjbsh_value(is_json ? name : label, true, "%s", result.output);
        }
    } else {
        mjbsh_null(is_json ? name : label, 1);

        return;
    }

    if(is_json) {
        printf("%s%s\"%s_normalization\":%s[%s", mjbsh_ji(), mjbsh_ji(), name,
            cmd_json_indent == 0 ? "" : " ", mjbsh_green());
    } else {
        printf("%s normalization: %s", label, mjbsh_green());
    }

    if(result.output_size > 0 &&
        mjb_for_each_character(result.output, result.output_size, MJB_ENC_UTF_8,
            is_json ? mjbsh_next_array_character : mjbsh_next_character) != MJB_STATUS_OK) {
        goto cleanup;
    }

    if(is_json) {
        printf("%s]", mjbsh_reset());
    } else {
        printf("%s", mjbsh_reset());
    }

    mjbsh_print_nl(nl);

cleanup:
    if(result.output != NULL && result.output != buffer_utf8) {
        mjb_free(result.output);
    }
}

void mjbsh_codepoint(const char *label, unsigned int nl, mjb_codepoint codepoint) {
    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_json_field(label);
        printf("%s%u%s", mjbsh_green(), (unsigned int)codepoint, mjbsh_reset());
    } else {
        printf("%s: %sU+%04X%s", label, mjbsh_green(), (unsigned int)codepoint, mjbsh_reset());
    }

    mjbsh_print_nl(nl);
}

int mjbsh_error(const char *format, ...) {
    bool is_json = cmd_output_mode == OUTPUT_MODE_JSON;
    va_list args;
    va_start(args, format);

    if(is_json) {
        printf("{%s", mjbsh_jnl());
        mjbsh_value_args("Error", 0, format, args);
        printf("}%s", mjbsh_jnl());
    } else {
        fprintf(stderr, "Error: %s", mjbsh_red());
        vfprintf(stderr, format, args);
        fprintf(stderr, "%s\n", mjbsh_reset());
    }

    va_end(args);

    return 1;
}

bool mjbsh_parse_codepoint(const char *input, mjb_codepoint *codepoint) {
    char *endptr;

    if(input == NULL || codepoint == NULL) {
        return false;
    }

    if(cmd_interpret_mode == INTERPRET_MODE_CODEPOINT) {
        errno = 0;
        unsigned long value = 0;
        const char *start = input;

        if(strncmp(input, "U+", 2) == 0 || strncmp(input, "u+", 2) == 0) {
            // Parse as hex after "U+" prefix
            start = input + 2;
            value = strtoul(start, &endptr, 16);
        } else {
            // Try parsing as hex
            value = strtoul(start, &endptr, 16);
        }

        if(endptr == start || *endptr != '\0' || errno == ERANGE || value > MJB_CODEPOINT_MAX) {
            return false;
        }

        *codepoint = (mjb_codepoint)value;

        return true;
    } else {
        if(mjb_for_each_character(input, strlen(input), MJB_ENC_UTF_8,
               mjbsh_next_current_character) != MJB_STATUS_OK) {
            return false;
        }

        if(current_codepoint == MJB_CODEPOINT_NOT_VALID) {
            return false;
        }

        *codepoint = current_codepoint;
        current_codepoint = MJB_CODEPOINT_NOT_VALID;

        return true;
    }
}

const char *mjbsh_ji(void) {
    if(cmd_output_mode != OUTPUT_MODE_JSON) {
        return "";
    }

    return indents[cmd_json_indent];
}

const char *mjbsh_jnl(void) {
    if(cmd_output_mode != OUTPUT_MODE_JSON) {
        return "";
    }

    return cmd_json_indent == 0 ? "" : "\n";
}

mjb_codepoint mjbsh_control_picture_codepoint(mjb_codepoint codepoint) {
    if(codepoint < 0x20) {
        // Add 0x2400 to the codepoint to make it a printable character by using the
        // "Control Pictures" block.
        codepoint += 0x2400;
    } else if(codepoint == 0x20) {
        codepoint = 0x2423;
    } else if(codepoint == 0x7F) {
        // The delete character.
        codepoint = 0x2421;
    }

    return codepoint;
}

bool mjbsh_property_is_bool(mjb_property property) {
    // See mjb_property enum in unicode.h
    return property == MJB_PR_NFD_QUICK_CHECK || property == MJB_PR_NFKD_QUICK_CHECK ||
        property >= MJB_PR_ASCII_HEX_DIGIT;
}

// ----------
// commands/bidi.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

static const char *bidi_dir_name(mjb_direction dir) {
    switch(dir) {
        case MJB_DIRECTION_LTR:
            return "Left-to-right";
        case MJB_DIRECTION_RTL:
            return "Right-to-left";
        default:
            return "Auto";
    }
}

static const char *bidi_dir_json(mjb_direction dir) {
    switch(dir) {
        case MJB_DIRECTION_LTR:
            return "ltr";
        case MJB_DIRECTION_RTL:
            return "rtl";
        default:
            return "auto";
    }
}

static int mjbsh_bidi_revolve(const char *input) {
    size_t input_size = strlen(input);
    bool is_json = (cmd_output_mode == OUTPUT_MODE_JSON);

    mjb_bidi_paragraph para;
    mjb_status status = mjb_bidi_resolve(input, input_size, MJB_ENC_UTF_8, MJB_DIRECTION_AUTO,
        &para);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    size_t *visual_order = para.count > 0 ? (size_t *)malloc(para.count * sizeof(size_t)) : NULL;

    if(para.count > 0 && visual_order == NULL) {
        mjb_bidi_paragraph_free(&para);

        return mjbsh_error("bidi: could not allocate visual order");
    }

    if(para.count > 0 &&
        mjb_bidi_reorder_line(&para, 0, para.count, visual_order) != MJB_STATUS_OK) {
        free(visual_order);
        mjb_bidi_paragraph_free(&para);

        return mjbsh_error("bidi: line reordering failed");
    }

    if(is_json) {
        printf("{%s", mjbsh_jnl());
    }

    mjbsh_numeric("Paragraph level", 1, para.paragraph_level);
    mjbsh_value("Direction", 1, "%s",
        is_json ? bidi_dir_json(para.direction) : bidi_dir_name(para.direction));

    if(is_json) {
        printf("%s%s\"chars\":%s[%s", mjbsh_ji(), mjbsh_ji(), cmd_json_indent == 0 ? "" : " ",
            para.count > 0 ? mjbsh_jnl() : "");
    }

    for(size_t i = 0; i < para.count; ++i) {
        mjb_bidi_char c = para.chars[i];

        if(is_json) {
            printf("%s%s{%s", mjbsh_ji(), mjbsh_ji(), mjbsh_jnl());
        }

        unsigned int previous_indent = cmd_json_indent;
        cmd_json_indent *= 2;

        if(!is_json) {
            puts("");
        }

        mjbsh_value("Codepoint", 1, "U+%04X", (unsigned int)c.codepoint);
        mjbsh_numeric("Level", 1, c.level);
        mjbsh_numeric("Resolved class", 1, c.resolved_class);
        mjbsh_value("Mirror glyph", 0, "U+%04X", (unsigned int)c.mirroring_glyph);

        cmd_json_indent = previous_indent;

        if(is_json) {
            printf("%s%s}%s%s", mjbsh_ji(), mjbsh_ji(),
                para.count > 1 && i != para.count - 1 ? "," : "", mjbsh_jnl());
        }
    }

    if(is_json) {
        printf("%s%s],%s", mjbsh_ji(), mjbsh_ji(), mjbsh_jnl());
    }

    if(is_json) {
        printf("%s%s\"visual_order\":%s[%s", mjbsh_ji(), mjbsh_ji(),
            cmd_json_indent == 0 ? "" : " ", mjbsh_green());
    } else {
        printf("\nVisual order: %s", mjbsh_green());
    }

    for(size_t i = 0; i < para.count; ++i) {
        if(is_json) {
            printf("%zu%s", visual_order[i], i == para.count - 1 ? "" : ", ");
        } else {
            printf("%zu%s", visual_order[i], i == para.count - 1 ? "" : " ");
        }
    }

    printf("%s%s", mjbsh_reset(), is_json ? "]" : "");

    if(is_json) {
        printf("%s}", mjbsh_jnl());
    }

    free(visual_order);
    mjb_bidi_paragraph_free(&para);

    return 0;
}

int mjbsh_bidi_command(int argc, char *const argv[], unsigned int flags) {
    return mjbsh_bidi_revolve(argv[0]);
}

// ----------
// commands/break.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

typedef enum {
    MJBSH_BREAK_MODE_ALL,
    MJBSH_BREAK_MODE_GRAPHEME,
    MJBSH_BREAK_MODE_WORD,
    MJBSH_BREAK_MODE_LINE,
    MJBSH_BREAK_MODE_SENTENCE
} mjbsh_break_mode;

typedef struct {
    mjb_next_state grapheme;
    mjb_next_word_state word;
    mjb_next_line_state line;
    mjb_next_sentence_state sentence;
} mjbsh_break_state;

static bool mjbsh_break_parse_mode(const char *value, mjbsh_break_mode *mode) {
    if(strcmp(value, "all") == 0) {
        *mode = MJBSH_BREAK_MODE_ALL;

        return true;
    }

    if(strcmp(value, "grapheme") == 0 || strcmp(value, "graphemes") == 0) {
        *mode = MJBSH_BREAK_MODE_GRAPHEME;
        return true;
    }

    if(strcmp(value, "word") == 0 || strcmp(value, "words") == 0) {
        *mode = MJBSH_BREAK_MODE_WORD;

        return true;
    }

    if(strcmp(value, "line") == 0 || strcmp(value, "lines") == 0) {
        *mode = MJBSH_BREAK_MODE_LINE;

        return true;
    }

    if(strcmp(value, "sentence") == 0 || strcmp(value, "sentences") == 0) {
        *mode = MJBSH_BREAK_MODE_SENTENCE;

        return true;
    }

    return false;
}

static bool mjbsh_break_should_print(mjbsh_break_mode selected, mjbsh_break_mode current) {
    return selected == MJBSH_BREAK_MODE_ALL || selected == current;
}

typedef mjb_break_type (*mjbsh_break_function)(const char *input, size_t input_size,
    mjbsh_break_state *state);

static mjb_break_type mjbsh_next_grapheme_break(const char *input, size_t input_size,
    mjbsh_break_state *state) {
    return mjb_next_grapheme_break(input, input_size, MJB_ENC_UTF_8, &state->grapheme);
}

static mjb_break_type mjbsh_next_word_break(const char *input, size_t input_size,
    mjbsh_break_state *state) {
    return mjb_next_word_break(input, input_size, MJB_ENC_UTF_8, &state->word);
}

static mjb_break_type mjbsh_next_line_break(const char *input, size_t input_size,
    mjbsh_break_state *state) {
    return mjb_next_line_break(input, input_size, MJB_ENC_UTF_8, &state->line);
}

static mjb_break_type mjbsh_next_sentence_break(const char *input, size_t input_size,
    mjbsh_break_state *state) {
    return mjb_next_sentence_break(input, input_size, MJB_ENC_UTF_8, &state->sentence);
}

static const char *mjbsh_break_type_name(mjb_break_type type) {
    switch(type) {
        case MJB_BT_MANDATORY:
            return "mandatory";
        case MJB_BT_NO_BREAK:
            return "no_break";
        case MJB_BT_ALLOWED:
            return "allowed";
        case MJB_BT_NOT_SET:
        default:
            return "not_set";
    }
}

static void mjbsh_print_break_json_field(const char *name, bool *first) {
    printf("%s%s%s\"%s\":%s", *first ? "" : ",", mjbsh_jnl(), mjbsh_ji(), name,
        cmd_json_indent == 0 ? "" : " ");
    *first = false;
}

static void mjbsh_print_break_json_array(const char *name, const char *input, size_t input_size,
    mjbsh_break_function next_break, bool *first_field) {
    mjbsh_break_state state;
    bool first_break = true;
    mjb_break_type type;

    memset(&state, 0, sizeof(state));

    mjbsh_print_break_json_field(name, first_field);
    putchar('[');

    while((type = next_break(input, input_size, &state)) != MJB_BT_NOT_SET) {
        printf("%s%s\"%s\"", first_break ? "" : ",", first_break || cmd_json_indent == 0 ? "" : " ",
            mjbsh_break_type_name(type));
        first_break = false;
    }

    putchar(']');
}

static void mjbsh_print_break_json(const char *input, size_t input_size, size_t input_real_size,
    size_t terminal_width, mjb_status terminal_width_status, mjbsh_break_mode mode) {
    bool first_field = true;

    putchar('{');

    mjbsh_print_break_json_field("raw_input_size", &first_field);
    printf("%zu", input_size);

    mjbsh_print_break_json_field("real_input_size", &first_field);
    printf("%zu", input_real_size);

    mjbsh_print_break_json_field("terminal_width", &first_field);

    if(terminal_width_status == MJB_STATUS_OK) {
        printf("%zu", terminal_width);
    } else {
        printf("null");
    }

    mjbsh_print_break_json_field("raw_bytes", &first_field);
    putchar('[');

    for(size_t i = 0; i < input_size; ++i) {
        printf("%s%s%u", i == 0 ? "" : ",", i == 0 || cmd_json_indent == 0 ? "" : " ",
            (unsigned char)input[i]);
    }

    putchar(']');

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_GRAPHEME)) {
        mjbsh_print_break_json_array("grapheme_breaks", input, input_size,
            mjbsh_next_grapheme_break, &first_field);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_WORD)) {
        mjbsh_print_break_json_array("word_breaks", input, input_size, mjbsh_next_word_break,
            &first_field);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_LINE)) {
        mjbsh_print_break_json_array("line_breaks", input, input_size, mjbsh_next_line_break,
            &first_field);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_SENTENCE)) {
        mjbsh_print_break_json_array("sentence_breaks", input, input_size,
            mjbsh_next_sentence_break, &first_field);
    }

    printf("%s}\n", mjbsh_jnl());
}

static void mjbsh_print_first_iteration(mjb_break_type first_bt, mjb_break_type bt, bool is_eot,
    mjb_codepoint previous_codepoint, mjb_codepoint current_codepoint) {
    mjbsh_print_break_symbol(first_bt);

    // First iteration: print the starting codepoint
    mjbsh_print_codepoint(previous_codepoint != MJB_CODEPOINT_NOT_VALID ? previous_codepoint :
                                                                          current_codepoint);

    mjbsh_print_break_symbol(bt);

    // If previous was valid, print current; if not, we already printed current
    if(previous_codepoint != MJB_CODEPOINT_NOT_VALID && !is_eot) {
        mjbsh_print_codepoint(current_codepoint);
    }
}

static void mjbsh_print_iteration(bool is_eot, mjb_break_type bt, mjb_codepoint current_codepoint) {
    mjbsh_print_break_symbol(bt);

    if(!is_eot) {
        mjbsh_print_codepoint(current_codepoint);
    }
}

static void mjbsh_print_grapheme_breaks(const char *input, size_t input_size) {
    bool first = true;
    mjb_break_type bt;

    printf("\n\nGrapheme cluster segmentation:\n");

    mjb_next_state segment_state;
    segment_state.index = 0;

    while((bt = mjb_next_grapheme_break(input, input_size, MJB_ENC_UTF_8, &segment_state)) !=
        MJB_BT_NOT_SET) {
        bool is_eot = (segment_state.index > input_size);

        if(first) {
            mjbsh_print_first_iteration(MJB_BT_ALLOWED, bt, is_eot,
                segment_state.previous_codepoint, segment_state.current_codepoint);

            first = false;
        } else {
            mjbsh_print_iteration(is_eot, bt, segment_state.current_codepoint);
        }
    }
}

static void mjbsh_print_word_breaks(const char *input, size_t input_size) {
    bool first = true;
    mjb_break_type bt;

    printf("\n\nWord breaking:\n");

    mjb_next_word_state word_state;
    word_state.index = 0;

    while((bt = mjb_next_word_break(input, input_size, MJB_ENC_UTF_8, &word_state)) !=
        MJB_BT_NOT_SET) {
        bool is_eot = (word_state.index > input_size);

        if(first) {
            mjbsh_print_first_iteration(MJB_BT_ALLOWED, bt, is_eot, word_state.previous_codepoint,
                word_state.current_codepoint);

            first = false;
        } else {
            mjbsh_print_iteration(is_eot, bt, word_state.current_codepoint);
        }
    }
}

static void mjbsh_print_line_breaks(const char *input, size_t input_size) {
    bool first = true;
    mjb_break_type bt;

    printf("\n\nLine breaking:\n");

    mjb_next_line_state line_state;
    line_state.index = 0;

    while((bt = mjb_next_line_break(input, input_size, MJB_ENC_UTF_8, &line_state)) !=
        MJB_BT_NOT_SET) {
        bool is_eot = (line_state.index > input_size);

        if(first) {
            mjbsh_print_first_iteration(MJB_BT_NO_BREAK, bt, is_eot, line_state.previous_codepoint,
                line_state.current_codepoint);

            first = false;
        } else {
            mjbsh_print_iteration(is_eot, bt, line_state.current_codepoint);
        }
    }
}

static void mjbsh_print_sentence_breaks(const char *input, size_t input_size) {
    bool first = true;
    mjb_break_type bt;

    printf("\n\nSentence breaking:\n");

    mjb_next_sentence_state sentence_state;
    sentence_state.index = 0;

    while((bt = mjb_next_sentence_break(input, input_size, MJB_ENC_UTF_8, &sentence_state)) !=
        MJB_BT_NOT_SET) {
        bool is_eot = (sentence_state.index > input_size);

        if(first) {
            mjbsh_print_first_iteration(MJB_BT_ALLOWED, bt, is_eot,
                sentence_state.previous_codepoint, sentence_state.current_codepoint);

            first = false;
        } else {
            mjbsh_print_iteration(is_eot, bt, sentence_state.current_codepoint);
        }
    }
}

static void mjbsh_print_break_analysis(const char *input, mjbsh_break_mode mode) {
    size_t input_size = strlen(input);
    size_t input_real_size = mjb_count_codepoints(input, input_size, MJB_ENC_UTF_8);
    size_t terminal_width = 0;
    mjb_status terminal_width_status = mjb_terminal_width(input, input_size, MJB_ENC_UTF_8,
        MJB_TERMINAL_WIDTH_NARROW, &terminal_width);

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_break_json(input, input_size, input_real_size, terminal_width,
            terminal_width_status, mode);
        return;
    }

    printf("Raw input size: %s%zu%s\n", mjbsh_red(), input_size, mjbsh_reset());
    printf("Real input size: %s%zu%s\n", mjbsh_yellow(), input_real_size, mjbsh_reset());
    if(terminal_width_status == MJB_STATUS_OK) {
        printf("Terminal width: %s%zu%s\n", mjbsh_green(), terminal_width, mjbsh_reset());
    } else {
        printf("Terminal width: %sunsupported%s (%s)\n", mjbsh_yellow(), mjbsh_reset(),
            mjb_status_message(terminal_width_status));
    }

    printf("\nRaw bytes: ");

    for(size_t i = 0; i < input_size; ++i) {
        unsigned char byte = (unsigned char)input[i];

        if(byte >= 0x21 && byte <= 0x7E) {
            printf("%c", byte);
        } else if(byte == 0x0A || byte == 0x0D) {
            printf("%s<%02X>%s%c", mjbsh_yellow(), byte, mjbsh_reset(), byte);
        } else {
            printf("%s<%02X>%s", mjbsh_yellow(), byte, mjbsh_reset());
        }
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_GRAPHEME)) {
        mjbsh_print_grapheme_breaks(input, input_size);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_WORD)) {
        mjbsh_print_word_breaks(input, input_size);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_LINE)) {
        mjbsh_print_line_breaks(input, input_size);
    }

    if(mjbsh_break_should_print(mode, MJBSH_BREAK_MODE_SENTENCE)) {
        mjbsh_print_sentence_breaks(input, input_size);
    }
}

static void mjbsh_display_break_output(const char *input) {
    mjbsh_clear_screen();
    printf("Break the input\n");
    printf("Ctrl+C to exit\n");

    if(input == NULL || strlen(input) == 0) {
        fflush(stdout);

        return;
    }

    mjbsh_print_break_analysis(input, MJBSH_BREAK_MODE_ALL);
    fflush(stdout);
}

static void mjbsh_handle_key(mjbsh_key key) {
    switch(key) {
        case MJBSH_KEY_LEFT:
            break;
        case MJBSH_KEY_RIGHT:
            break;
        case MJBSH_KEY_UP:
        case MJBSH_KEY_DOWN:
        default:
            break;
    }
}

int mjbsh_break_command(int argc, char *const argv[], unsigned int flags) {
    if(argc != 0) {
        mjbsh_break_mode mode = MJBSH_BREAK_MODE_ALL;
        const char *input = argv[0];

        if(argc > 1) {
            if(!mjbsh_break_parse_mode(argv[0], &mode)) {
                return mjbsh_error("break: unknown mode: %s", argv[0]);
            }

            input = argv[1];
        }

        mjbsh_print_break_analysis(input, mode);

        return 0;
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        return mjbsh_error("break: JSON output requires an input");
    }

    mjbsh_screen_mode(mjbsh_display_break_output, mjbsh_handle_key);

    return 0;
}

// ----------
// commands/character.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

static bool mjbsh_output_next_character(mjb_character *character, mjb_character_position type) {
    char buffer_utf8[5];
    unsigned int utf8_length = mjb_codepoint_encode(character->codepoint, buffer_utf8, 5,
        MJB_ENC_UTF_8);

    if(utf8_length == 0) {
        return false;
    }

    bool is_json = cmd_output_mode == OUTPUT_MODE_JSON;

    if(is_json) {
        if(type & MJB_POSITION_FIRST) {
            printf("[%s%s{%s", mjbsh_jnl(), mjbsh_ji(), mjbsh_jnl());
        } else {
            printf("%s{%s", mjbsh_ji(), mjbsh_jnl());
        }
    } else {
        if(!(type & MJB_POSITION_FIRST)) {
            puts("");
        }
    }

    mjbsh_value("Codepoint", 1, "U+%04X", (unsigned int)character->codepoint);
    mjbsh_value("Name", 1, "%s", character->name);

    if(is_json) {
        printf("%s%s\"character\":%s\"%s", mjbsh_ji(), mjbsh_ji(), cmd_json_indent == 0 ? "" : " ",
            mjbsh_green());

        if(!mjbsh_print_escaped_character(buffer_utf8)) {
            printf("%s", buffer_utf8);
        }

        printf("%s\",%s", mjbsh_reset(), mjbsh_jnl());
    } else {
        mjbsh_value("Character", 1, "%s", buffer_utf8);
    }

    // Hex UTF-8
    if(is_json) {
        printf("%s%s\"hex_utf_8\":%s[%s", mjbsh_ji(), mjbsh_ji(), cmd_json_indent == 0 ? "" : " ",
            mjbsh_green());
    } else {
        printf("Hex UTF-8: %s", mjbsh_green());
    }

    for(size_t i = 0; i < utf8_length; ++i) {
        if(is_json) {
            printf("%u%s", (unsigned char)buffer_utf8[i], i == utf8_length - 1 ? "" : ", ");
        } else {
            printf("%02X%s", (unsigned char)buffer_utf8[i], i == utf8_length - 1 ? "" : " ");
        }
    }

    printf("%s%s", mjbsh_reset(), is_json ? "]" : "");

    if(is_json) {
        printf(",%s", mjbsh_jnl());
    }

    if(cmd_verbose > 0) {
        mjb_encoding other_encodings[] = { MJB_ENC_UTF_16BE, MJB_ENC_UTF_16LE, MJB_ENC_UTF_32BE,
            MJB_ENC_UTF_32LE };

        const char *other_encodings_names[] = { "16BE", "16LE", "32BE", "32LE" };

        const char *other_encodings_labels[] = { "16be", "16le", "32be", "32le" };

        for(size_t i = 0; i < 4; ++i) {
            char buffer[5];
            unsigned int length = mjb_codepoint_encode(character->codepoint, buffer, 5,
                other_encodings[i]);

            if(is_json) {
                printf("%s%s\"hex_utf_%s\":%s[%s", mjbsh_ji(), mjbsh_ji(),
                    other_encodings_labels[i], cmd_json_indent == 0 ? "" : " ", mjbsh_green());
            } else {
                printf("\nHex UTF-%s: %s", other_encodings_names[i], mjbsh_green());
            }

            for(size_t j = 0; j < length; ++j) {
                if(is_json) {
                    printf("%u%s", (unsigned char)buffer[j], j == length - 1 ? "" : ", ");
                } else {
                    printf("%02X%s", (unsigned char)buffer[j], j == length - 1 ? "" : " ");
                }
            }

            printf("%s%s", mjbsh_reset(), is_json ? "]" : "");

            if(is_json) {
                printf(",%s", mjbsh_jnl());
            }
        }
    }

    if(!is_json) {
        puts("");
    }

    if(cmd_verbose > 0) {
        mjbsh_normalization(buffer_utf8, utf8_length, MJB_NORMALIZATION_NFD, "nfd", "NFD", 1);
        mjbsh_normalization(buffer_utf8, utf8_length, MJB_NORMALIZATION_NFC, "nfc", "NFC", 1);
        mjbsh_normalization(buffer_utf8, utf8_length, MJB_NORMALIZATION_NFKD, "nfkd", "NFKD", 1);
        mjbsh_normalization(buffer_utf8, utf8_length, MJB_NORMALIZATION_NFKC, "nfkc", "NFKC", 1);

        // Flush stdout here to ensure the normalization is printed before the next character
        fflush(stdout);
    }

    mjbsh_id_name("Category", character->category, mjbsh_category_name(character->category),
        cmd_verbose == 0 ? 0 : 1);

    if(cmd_verbose > 0) {
        char *cc_name = mjbsh_ccc_name(character->combining);

        mjbsh_id_name("Combining", character->combining, cc_name, 1);

        free(cc_name);

        const char *bi_name = mjbsh_bidi_name((mjb_bidi_class)character->bidirectional);

        mjbsh_id_name("Bidirectional", character->bidirectional, bi_name, 1);

        mjb_plane plane = mjb_codepoint_plane(character->codepoint);
        const char *plane_name = mjb_plane_name(plane, false);

        mjbsh_id_name("Plane", plane, plane_name, 1);

        mjb_block_info block;
        memset(&block, 0, sizeof(block));
        bool valid_block = mjb_codepoint_block(character->codepoint, &block) == MJB_STATUS_OK;

        // Flush stdout here to ensure the block is printed before the next character
        fflush(stdout);

        if(valid_block) {
            mjbsh_id_name("Block", block.id, block.name, 1);
        }

        const char *d_name = mjbsh_decomposition_name(character->decomposition);

        mjbsh_id_name("Decomposition", character->decomposition, d_name, 1);
    }

    if(cmd_verbose > 0) {
        if(character->decimal == MJB_NUMBER_NOT_VALID) {
            mjbsh_null("Decimal", 1);
        } else {
            mjbsh_numeric("Decimal", 1, character->decimal);
        }

        if(character->digit == MJB_NUMBER_NOT_VALID) {
            mjbsh_null("Digit", 1);
        } else {
            mjbsh_numeric("Digit", 1, character->digit);
        }

        if(character->numeric[0] != '\0') {
            mjbsh_value("Numeric", 1, "%s", character->numeric);
        } else {
            mjbsh_null("Numeric", 1);
        }

        mjbsh_bool("Mirrored", 1, character->mirrored);

        if(character->uppercase != 0) {
            mjbsh_codepoint("Simple Uppercase Mapping", 1, character->uppercase);
        } else {
            mjbsh_null("Simple Uppercase Mapping", 1);
        }

        if(character->lowercase != 0) {
            mjbsh_codepoint("Simple Lowercase Mapping", 1, (unsigned int)character->lowercase);
        } else {
            mjbsh_null("Simple Lowercase Mapping", 1);
        }

        if(character->titlecase != 0) {
            mjbsh_codepoint("Simple Titlecase Mapping", cmd_verbose == 1 ? 0 : 1,
                character->titlecase);
        } else {
            mjbsh_null("Simple Titlecase Mapping", cmd_verbose == 1 ? 0 : 1);
        }
    }

    if(cmd_verbose > 1) {
        // Pre-scan properties to find the last one that will be printed.
        uint8_t properties[MJB_PR_BUFFER_SIZE] = { 0 };
        size_t last_prop = MJB_PR_COUNT;

        for(size_t i = 0; i < MJB_PR_COUNT; ++i) {
            mjb_property property = (mjb_property)i;
            bool is_bool = mjbsh_property_is_bool(property);
            bool binary_value = false;
            int32_t integer_value = 0;
            mjb_status status = is_bool ?
                mjb_codepoint_property_binary(character->codepoint, property, &binary_value) :
                mjb_codepoint_property_int(character->codepoint, property, &integer_value);

            if(status == MJB_STATUS_OK && (!is_bool || binary_value)) {
                properties[i] = is_bool ? 1 : (uint8_t)integer_value;
                last_prop = i;
            }
        }

        bool has_properties = last_prop < MJB_PR_COUNT;

        mjb_east_asian_width east_asian_width;
        bool eaw_valid = mjb_codepoint_east_asian_width(character->codepoint, &east_asian_width) ==
            MJB_STATUS_OK;

        if(eaw_valid) {
            mjbsh_id_name("East Asian Width", east_asian_width,
                mjbsh_east_asian_width_name(east_asian_width), 1);
        } else {
            mjbsh_null("East Asian Width", 1);
        }

        mjb_emoji_properties emoji_properties;
        bool emoji_valid = mjb_codepoint_emoji_properties(character->codepoint,
                               &emoji_properties) == MJB_STATUS_OK;
        unsigned int ep_nl = has_properties ? 1 : 0;

        if(emoji_valid) {
            mjbsh_bool("Emoji", 1, emoji_properties.emoji);
            mjbsh_bool("Emoji Presentation", 1, emoji_properties.presentation);
            mjbsh_bool("Emoji Modifier", 1, emoji_properties.modifier);
            mjbsh_bool("Emoji Modifier Base", 1, emoji_properties.modifier_base);
            mjbsh_bool("Emoji Component", 1, emoji_properties.component);
            mjbsh_bool("Extended Pictographic", ep_nl, emoji_properties.extended_pictographic);
        } else {
            mjbsh_null("Emoji", 1);
            mjbsh_null("Emoji Presentation", 1);
            mjbsh_null("Emoji Modifier", 1);
            mjbsh_null("Emoji Modifier Base", 1);
            mjbsh_null("Emoji Component", 1);
            mjbsh_null("Extended Pictographic", ep_nl);
        }

        if(has_properties) {
            for(size_t i = 0; i < MJB_PR_COUNT; ++i) {
                if(mjbsh_property_is_bool((mjb_property)i)) {
                    if(properties[i]) {
                        mjbsh_bool(mjb_property_name((mjb_property)i), i == last_prop ? 0 : 1,
                            true);
                    }
                } else {
                    if(properties[i] != 0) {
                        mjbsh_numeric(mjb_property_name((mjb_property)i), i == last_prop ? 0 : 1,
                            properties[i]);
                    }
                }
            }
        }
    }

    if(is_json) {
        printf("%s}%s%s", mjbsh_ji(), (type & MJB_POSITION_LAST) ? "" : ",", mjbsh_jnl());

        if(type & MJB_POSITION_LAST) {
            printf("]%s", mjbsh_jnl());
        }
    }

    return true;
}

int mjbsh_character_command(int argc, char *const argv[], unsigned int flags) {
    mjb_status status = mjb_for_each_character(argv[0], strlen(argv[0]), MJB_ENC_UTF_8,
        mjbsh_output_next_character);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    return 0;
}

// ----------
// commands/codepoint.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include <errno.h>

// #include "../shell.h"

int mjbsh_codepoint_command(int argc, char *const argv[], unsigned int flags) {
    char buffer[5];
    char *endptr = NULL;

    errno = 0;
    unsigned long value = strtoul(argv[0], &endptr, 16);

    if(endptr == argv[0] || *endptr != '\0' || errno == ERANGE || value > MJB_CODEPOINT_MAX) {
        return mjbsh_error("Invalid codepoint: %s", argv[0]);
    }

    mjb_codepoint codepoint = (mjb_codepoint)value;
    unsigned int length = mjb_codepoint_encode(codepoint, buffer, 5, MJB_ENC_UTF_8);

    if(length == 0) {
        return mjbsh_error("Failed to encode codepoint: %s", argv[0]);
    }

    char *argv_buffer[] = { buffer };

    return mjbsh_character_command(1, argv_buffer, flags);
}

// ----------
// commands/emoji.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

static unsigned int mjbsh_emoji_json_character_count = 0;

static const char *mjbsh_emoji_sequence_type_name(mjb_emoji_sequence_type type) {
    switch(type) {
        case MJB_EMOJI_SEQUENCE_NONE:
            return "None";
        case MJB_EMOJI_SEQUENCE_BASIC:
            return "Basic";
        case MJB_EMOJI_SEQUENCE_KEYCAP:
            return "Keycap";
        case MJB_EMOJI_SEQUENCE_FLAG:
            return "Flag";
        case MJB_EMOJI_SEQUENCE_TAG:
            return "Tag";
        case MJB_EMOJI_SEQUENCE_MODIFIER:
            return "Modifier";
        case MJB_EMOJI_SEQUENCE_ZWJ:
            return "ZWJ";
        case MJB_EMOJI_SEQUENCE_TEXT_VARIATION:
            return "Text variation";
        case MJB_EMOJI_SEQUENCE_EMOJI_VARIATION:
            return "Emoji variation";
    }

    return "Unknown";
}

static const char *mjbsh_emoji_qualification_name(mjb_emoji_qualification qualification) {
    switch(qualification) {
        case MJB_EMOJI_QUALIFICATION_NONE:
            return "None";
        case MJB_EMOJI_QUALIFICATION_COMPONENT:
            return "Component";
        case MJB_EMOJI_QUALIFICATION_FULLY_QUALIFIED:
            return "Fully-qualified";
        case MJB_EMOJI_QUALIFICATION_MINIMALLY_QUALIFIED:
            return "Minimally-qualified";
        case MJB_EMOJI_QUALIFICATION_UNQUALIFIED:
            return "Unqualified";
    }

    return "Unknown";
}

static const char *mjbsh_emoji_json_space(void) {
    return cmd_json_indent == 0 ? "" : " ";
}

static void mjbsh_emoji_json_indent(unsigned int level) {
    for(unsigned int i = 0; i < level; ++i) {
        printf("%s", mjbsh_ji());
    }
}

static void mjbsh_emoji_json_string(const char *value) {
    if(value == NULL) {
        value = "";
    }

    putchar('"');

    for(const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        char c = (char)*p;

        if(!mjbsh_print_escaped_character(&c)) {
            putchar(c);
        }
    }

    putchar('"');
}

static void mjbsh_emoji_json_string_field(const char *name, const char *value, unsigned int level,
    bool comma) {
    mjbsh_emoji_json_indent(level);
    printf("\"%s\":%s", name, mjbsh_emoji_json_space());
    mjbsh_emoji_json_string(value);
    printf("%s%s", comma ? "," : "", mjbsh_jnl());
}

static void mjbsh_emoji_json_bool_field(const char *name, bool value, unsigned int level,
    bool comma) {
    mjbsh_emoji_json_indent(level);
    printf("\"%s\":%s%s%s", name, mjbsh_emoji_json_space(), value ? "true" : "false",
        comma ? "," : "");
    printf("%s", mjbsh_jnl());
}

static void mjbsh_emoji_json_size_field(const char *name, size_t value, unsigned int level,
    bool comma) {
    mjbsh_emoji_json_indent(level);
    printf("\"%s\":%s%zu%s", name, mjbsh_emoji_json_space(), value, comma ? "," : "");
    printf("%s", mjbsh_jnl());
}

static void mjbsh_emoji_json_id_field(const char *name, unsigned int code, const char *value,
    unsigned int level, bool comma) {
    mjbsh_emoji_json_indent(level);
    printf("\"%s\":%s{%s", name, mjbsh_emoji_json_space(), mjbsh_jnl());

    mjbsh_emoji_json_size_field("code", code, level + 1, true);
    mjbsh_emoji_json_string_field("value", value, level + 1, false);

    mjbsh_emoji_json_indent(level);
    printf("}%s%s", comma ? "," : "", mjbsh_jnl());
}

static bool mjbsh_emoji_next_character(mjb_character *character, mjb_character_position type) {
    char buffer_utf8[5];
    unsigned int utf8_length = mjb_codepoint_encode(character->codepoint, buffer_utf8,
        sizeof(buffer_utf8), MJB_ENC_UTF_8);
    mjb_emoji_properties emoji;
    bool emoji_valid = mjb_codepoint_emoji_properties(character->codepoint, &emoji) ==
        MJB_STATUS_OK;

    if(utf8_length == 0) {
        return false;
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        char codepoint[16];

        snprintf(codepoint, sizeof(codepoint), "U+%04X", (unsigned int)character->codepoint);
        if(type & MJB_POSITION_FIRST) {
            printf("%s", mjbsh_jnl());
        } else {
            printf(",%s", mjbsh_jnl());
        }
        mjbsh_emoji_json_indent(2);
        printf("{%s", mjbsh_jnl());

        mjbsh_emoji_json_string_field("codepoint", codepoint, 3, true);
        mjbsh_emoji_json_string_field("name", character->name, 3, true);
        mjbsh_emoji_json_string_field("character", buffer_utf8, 3, true);
        mjbsh_emoji_json_bool_field("emoji_data", emoji_valid, 3, true);
        mjbsh_emoji_json_bool_field("emoji", emoji_valid && emoji.emoji, 3, true);
        mjbsh_emoji_json_bool_field("emoji_presentation", emoji_valid && emoji.presentation, 3,
            true);
        mjbsh_emoji_json_bool_field("emoji_modifier", emoji_valid && emoji.modifier, 3, true);
        mjbsh_emoji_json_bool_field("emoji_modifier_base", emoji_valid && emoji.modifier_base, 3,
            true);
        mjbsh_emoji_json_bool_field("emoji_component", emoji_valid && emoji.component, 3, true);
        mjbsh_emoji_json_bool_field("extended_pictographic",
            emoji_valid && emoji.extended_pictographic, 3, false);

        mjbsh_emoji_json_indent(2);
        printf("}");

        if(type & MJB_POSITION_LAST) {
            printf("%s", mjbsh_jnl());
        }

        ++mjbsh_emoji_json_character_count;

        return true;
    }

    if(type & MJB_POSITION_FIRST) {
        puts("");
        puts("Characters:");
    } else {
        puts("");
    }

    mjbsh_value("Codepoint", 1, "U+%04X", (unsigned int)character->codepoint);
    mjbsh_value("Name", 1, "%s", character->name);
    mjbsh_value("Character", 1, "%s", buffer_utf8);
    mjbsh_bool("Emoji Data", 1, emoji_valid);
    mjbsh_bool("Emoji", 1, emoji_valid && emoji.emoji);
    mjbsh_bool("Emoji Presentation", 1, emoji_valid && emoji.presentation);
    mjbsh_bool("Emoji Modifier", 1, emoji_valid && emoji.modifier);
    mjbsh_bool("Emoji Modifier Base", 1, emoji_valid && emoji.modifier_base);
    mjbsh_bool("Emoji Component", 1, emoji_valid && emoji.component);
    mjbsh_bool("Extended Pictographic", 1, emoji_valid && emoji.extended_pictographic);

    return true;
}

static bool mjbsh_emoji_input_from_codepoints(int argc, char *const argv[], char **buffer,
    size_t *size) {
    size_t buffer_size = ((size_t)argc * 4) + 1;
    size_t index = 0;
    char *codepoints = (char *)malloc(buffer_size);

    if(codepoints == NULL) {
        return false;
    }

    for(int i = 0; i < argc; ++i) {
        mjb_codepoint codepoint = 0;

        if(!mjbsh_parse_codepoint(argv[i], &codepoint)) {
            free(codepoints);

            return false;
        }

        unsigned int written = mjb_codepoint_encode(codepoint, codepoints + index,
            buffer_size - index, MJB_ENC_UTF_8);

        if(written == 0) {
            free(codepoints);

            return false;
        }

        index += written;
    }

    codepoints[index] = '\0';
    *buffer = codepoints;
    *size = index;

    return true;
}

static void mjbsh_emoji_print_json(const char *buffer, size_t byte_length, bool is_emoji_sequence,
    bool is_rgi, const mjb_emoji_sequence *emoji) {
    printf("{%s", mjbsh_jnl());

    mjbsh_emoji_json_string_field("input", buffer, 1, true);
    mjbsh_emoji_json_bool_field("emoji_sequence", is_emoji_sequence, 1, true);
    mjbsh_emoji_json_bool_field("rgi_emoji", is_rgi, 1, true);
    mjbsh_emoji_json_id_field("sequence_type", (unsigned int)emoji->type,
        mjbsh_emoji_sequence_type_name(emoji->type), 1, true);
    mjbsh_emoji_json_id_field("qualification", (unsigned int)emoji->qualification,
        mjbsh_emoji_qualification_name(emoji->qualification), 1, true);
    mjbsh_emoji_json_size_field("sequence_codepoints", emoji->codepoint_count, 1, true);

    mjbsh_emoji_json_indent(1);
    printf("\"characters\":%s[", mjbsh_emoji_json_space());

    mjbsh_emoji_json_character_count = 0;

    if(mjb_for_each_character(buffer, byte_length, MJB_ENC_UTF_8, mjbsh_emoji_next_character) !=
        MJB_STATUS_OK) {
        printf("]%s", mjbsh_jnl());
        printf("}%s", mjbsh_jnl());

        return;
    }

    if(mjbsh_emoji_json_character_count > 0) {
        mjbsh_emoji_json_indent(1);
    }

    printf("]%s", mjbsh_jnl());
    printf("}%s", mjbsh_jnl());
}

static void mjbsh_emoji_print_plain(const char *buffer, size_t byte_length, bool is_emoji_sequence,
    bool is_rgi, const mjb_emoji_sequence *emoji) {
    mjbsh_value("Input", 1, "%s", buffer);
    mjbsh_bool("Emoji Sequence", 1, is_emoji_sequence);
    mjbsh_bool("RGI Emoji", 1, is_rgi);
    mjbsh_id_name("Sequence Type", (unsigned int)emoji->type,
        mjbsh_emoji_sequence_type_name(emoji->type), 1);
    mjbsh_id_name("Qualification", (unsigned int)emoji->qualification,
        mjbsh_emoji_qualification_name(emoji->qualification), 1);
    mjbsh_numeric("Sequence Codepoints", 1, (unsigned int)emoji->codepoint_count);

    if(mjb_for_each_character(buffer, byte_length, MJB_ENC_UTF_8, mjbsh_emoji_next_character) !=
        MJB_STATUS_OK) {
        return;
    }
}

int mjbsh_emoji_command(int argc, char *const argv[], unsigned int flags) {
    char *buffer = argv[0];
    size_t size = strlen(buffer);
    bool should_free = false;

    (void)flags;

    if(cmd_interpret_mode == INTERPRET_MODE_CODEPOINT) {
        if(!mjbsh_emoji_input_from_codepoints(argc, argv, &buffer, &size)) {
            return mjbsh_error("Invalid codepoint input");
        }

        should_free = true;
    }

    mjb_emoji_sequence emoji;
    memset(&emoji, 0, sizeof(emoji));

    bool is_emoji_sequence = mjb_is_emoji_sequence(buffer, size, MJB_ENC_UTF_8);
    bool has_sequence_metadata = mjb_classify_emoji_sequence(buffer, size, MJB_ENC_UTF_8, &emoji) ==
        MJB_STATUS_OK;
    bool is_rgi = mjb_is_rgi_emoji(buffer, size, MJB_ENC_UTF_8);

    if(!has_sequence_metadata) {
        emoji.type = MJB_EMOJI_SEQUENCE_NONE;
        emoji.qualification = MJB_EMOJI_QUALIFICATION_NONE;
        emoji.codepoint_count = 0;
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_emoji_print_json(buffer, size, is_emoji_sequence, is_rgi, &emoji);
    } else {
        mjbsh_emoji_print_plain(buffer, size, is_emoji_sequence, is_rgi, &emoji);
    }

    if(should_free) {
        free(buffer);
    }

    return 0;
}

// ----------
// commands/filter.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

static mjb_filter_flags filter_flags = MJB_FILTER_NONE;

static int mjbsh_print_filter_analysis(const char *input) {
    mjb_result result;
    size_t input_size = strlen(input);

    mjb_status status = mjb_filter(input, input_size, MJB_ENC_UTF_8, filter_flags, MJB_ENC_UTF_8,
        &result);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_json_result(result.output, result.output_size);
    } else {
        puts(result.output);
    }

    if(result.output != NULL && result.output != input) {
        mjb_free(result.output);
    }

    return 0;
}

static void mjbsh_display_filter_output(const char *input) {
    mjbsh_clear_screen();
    printf("Filter the input\n");
    printf("Ctrl+C to exit\n");

    if(input == NULL || strlen(input) == 0) {
        fflush(stdout);

        return;
    }

    puts(input);
    (void)mjbsh_print_filter_analysis(input);
    fflush(stdout);
}

int mjbsh_filter_command(int argc, char *const argv[], unsigned int flags) {
    filter_flags = (mjb_filter_flags)flags;

    if(argc != 0) {
        return mjbsh_print_filter_analysis(argv[0]);
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        return mjbsh_error("filter: JSON output requires an input");
    }

    mjbsh_screen_mode(mjbsh_display_filter_output, NULL);

    return 0;
}

// ----------
// commands/locale.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

static void mjbsh_locale_field(const char *label, unsigned int nl, const char *value) {
    if(value[0] == '\0') {
        mjbsh_null(label, nl);

        return;
    }

    mjbsh_value(label, nl, "%s", value);
}

int mjbsh_locale_command(int argc, char *const argv[], unsigned int flags) {
    const char *input = argv[0];
    mjb_locale_id locale;
    mjb_status status = mjb_locale_parse(input, strlen(input), MJB_ENC_UTF_8, &locale);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        printf("{%s", mjbsh_jnl());
    }

    mjbsh_value("Input", 1, "%s", input);
    mjbsh_locale_field("Language", 1, locale.language);
    mjbsh_locale_field("Extlang", 1, locale.extlang);
    mjbsh_locale_field("Script", 1, locale.script);
    mjbsh_locale_field("Region", 1, locale.region);
    mjbsh_locale_field("Variant", 1, locale.variant);
    mjbsh_locale_field("Extensions", 1, locale.extensions);
    mjbsh_locale_field("Private use", 1, locale.private_use);
    mjbsh_locale_field("Grandfathered", 0, locale.grandfathered);

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        printf("%s}", mjbsh_jnl());
    }

    return 0;
}

// ----------
// commands/normalize.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

int mjbsh_normalize_string_command(int argc, char *const argv[], unsigned int flags) {
    mjb_result result;
    bool ret = true;

    mjb_status status = mjb_normalize(argv[0], strlen(argv[0]), MJB_ENC_UTF_8,
        (mjb_normalization)flags, MJB_ENC_UTF_8, &result);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_json_result(result.output, result.output_size);
        goto cleanup;
    }

    printf("%s", mjbsh_green());

    if(result.output_size > 0 &&
        mjb_for_each_character(result.output, result.output_size, MJB_ENC_UTF_8,
            mjbsh_next_string_character) != MJB_STATUS_OK) {
        printf("%s", mjbsh_reset());
        puts("");
        ret = false;

        goto cleanup;
    }

    printf("%s", mjbsh_reset());
    puts("");

cleanup:
    if(result.output != NULL && result.output != argv[0]) {
        mjb_free(result.output);
    }

    return ret ? 0 : 1;
}

int mjbsh_normalize_command(int argc, char *const argv[], unsigned int flags) {
    if(cmd_interpret_mode == INTERPRET_MODE_CHARACTER) {
        return mjbsh_normalize_string_command(argc, argv, flags);
    }

    unsigned int index = 0;
    // 5 bytes per codepoint is more than enough.
    char *codepoints = (char *)malloc(argc * 5);

    for(int i = 0; i < argc; ++i) {
        mjb_codepoint codepoint = 0;

        if(!mjbsh_parse_codepoint(argv[i], &codepoint)) {
            free(codepoints);

            return mjbsh_error("Invalid codepoint input: %s", argv[i]);
        }

        index += mjb_codepoint_encode(codepoint, codepoints + index, (argc * 5) - index,
            MJB_ENC_UTF_8);
    }

    codepoints[index] = '\0';

    mjb_result result;
    bool ret = true;
    mjb_status status = mjb_normalize(codepoints, index, MJB_ENC_UTF_8, (mjb_normalization)flags,
        MJB_ENC_UTF_8, &result);

    if(status != MJB_STATUS_OK) {
        free(codepoints);

        return mjbsh_error("%s", mjb_status_message(status));
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_json_result(result.output, result.output_size);

        goto cleanup;
    }

    if(result.output_size > 0 &&
        mjb_for_each_character(result.output, result.output_size, MJB_ENC_UTF_8,
            mjbsh_next_character) != MJB_STATUS_OK) {
        puts("");
        ret = false;

        goto cleanup;
    }

    puts("");

cleanup:
    if(result.output != NULL && result.output != codepoints) {
        mjb_free(result.output);
    }

    free(codepoints);

    return ret ? 0 : 1;
}

// ----------
// commands/string.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "../shell.h"

int mjbsh_case_command(int argc, char *const argv[], unsigned int flags) {
    mjb_result result = { NULL, 0, false };
    mjb_status status = mjb_map_case(argv[0], strlen(argv[0]), MJB_ENC_UTF_8,
        (mjb_map_case_type)flags, MJB_ENC_UTF_8, &result);

    if(status != MJB_STATUS_OK) {
        return mjbsh_error("%s", mjb_status_message(status));
    }

    if(cmd_output_mode == OUTPUT_MODE_JSON) {
        mjbsh_print_json_result(result.output, result.output_size);
    } else {
        puts(result.output);
    }

    if(result.transformed) {
        mjb_free(result.output);
    }

    return 0;
}

// ----------
// main.c
// ----------

/**
 * The Mojibake shell
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// #include "shell.h"

static int mjbsh_show_version(void) {
    mjb_character character;
    bool valid = mjb_codepoint_info(MJB_VERSION_NUMBER, &character) == MJB_STATUS_OK;

    printf("Mojibake %sv%s [%s]%s\n", mjbsh_green(), mjb_version(),
        valid ? character.name : "UNKNOWN", mjbsh_reset());

    return 0;
}

static void mjbsh_show_help(struct option options[], const char *descriptions[],
    mjbsh_command commands[], const char *error) {
    FILE *stream = error ? stderr : stdout;

    fprintf(stream,
        "%s%sUsage: mojibake [options...] <command> [<args>]\n\nMojibake client [v%s]\n\n",
        error ? error : "", error ? "\n\n" : "", MJB_VERSION);
    fprintf(stream, "Options:\n");

    for(unsigned long i = 0; options[i].val != 0; ++i) {
        fprintf(stream, "  -%c%s, --%s%s\n\t%s\n", options[i].val,
            options[i].has_arg == no_argument ? "" : " <arg>", options[i].name,
            options[i].has_arg == no_argument ? "" : "=<arg>", descriptions[i]);
    }

    fprintf(stream, "\nCommands:\n");

    for(unsigned long i = 0; commands[i].name != NULL; ++i) {
        fprintf(stream, "  %s\n\t%s\n", commands[i].name, commands[i].description);
    }
}

/**
 * The Mojibake shell
 *
 * EX SIGNIS ORDO
 */
int main(int argc, char *const argv[]) {
    int option = 0;
    int option_index = 0;

#ifdef _WIN32
    UINT original_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
#endif

    // unsigned int columns = 80;

    struct option long_options[] = { { "codepoint", no_argument, NULL, 'c' },
        { "help", no_argument, NULL, 'h' }, { "json-indent", required_argument, NULL, 'j' },
        { "output", required_argument, NULL, 'o' },
        { "show-allowed-symbols", no_argument, NULL, 's' }, { "verbose", no_argument, NULL, 'v' },
        { "version", no_argument, NULL, 'V' }, { NULL, 0, NULL, 0 } };

    const char *descriptions[] = { "Interpret input as a list of codepoints", "Print help",
        "JSON indent level (0-10). Default: 0",
        "Output mode: plain, json. Default: plain\n"
        "\t\tplain: print the result in plain text\n"
        "\t\tjson: print the result in JSON format",
        "Show allowed symbols", "Verbose output", "Print version", "Width of output" };

    mjbsh_command commands[] = { { "bidi", "Resolve the bidirectional algorithm for the input",
                                     mjbsh_bidi_command, 0, 1, false },
        { "break", "Break the input into grapheme, word, line, and sentence breaks",
            mjbsh_break_command, 0, 2, false },
        { "char", "Print the characters for the given string", mjbsh_character_command, 0, 1,
            false },
        { "codepoint", "Print the character for the given codepoint", mjbsh_codepoint_command, 0, 1,
            false },
        { "emoji", "Print emoji sequence and codepoint information for the input",
            mjbsh_emoji_command, 0, 1, true },
        { "filter", "Filter the input", mjbsh_filter_command,
            MJB_FILTER_NORMALIZE | MJB_FILTER_SPACES | MJB_FILTER_COLLAPSE_SPACES |
                MJB_FILTER_CONTROLS | MJB_FILTER_NUMERIC | MJB_FILTER_LIMIT_COMBINING,
            1, false },
        { "locale", "Parse a BCP 47 language tag", mjbsh_locale_command, 0, 1, false },
        { "nfd", "Normalize the input to NFD", mjbsh_normalize_command, MJB_NORMALIZATION_NFD, 1,
            true },
        { "nfkd", "Normalize the input to NFKD", mjbsh_normalize_command, MJB_NORMALIZATION_NFKD, 1,
            true },
        { "nfc", "Normalize the input to NFC", mjbsh_normalize_command, MJB_NORMALIZATION_NFC, 1,
            true },
        { "nfkc", "Normalize the input to NFKC", mjbsh_normalize_command, MJB_NORMALIZATION_NFKC, 1,
            true },
        { "upper", "Convert the input to uppercase", mjbsh_case_command, MJB_CASE_UPPER, 1, false },
        { "lower", "Convert the input to lowercase", mjbsh_case_command, MJB_CASE_LOWER, 1, false },
        { "title", "Convert the input to titlecase", mjbsh_case_command, MJB_CASE_TITLE, 1, false },
        { "casefold", "Convert the input to case fold", mjbsh_case_command, MJB_CASE_CASEFOLD, 1,
            false },
        { "casefold-simple", "Convert the input to simple case fold", mjbsh_case_command,
            MJB_CASE_CASEFOLD_SIMPLE, 1, false },
        { NULL, NULL, NULL, 0, 0, false } };

    if(isatty(STDOUT_FILENO)) {
        /*struct winsize w;

        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

        if(w.ws_col > 0) {
            columns = w.ws_col;
        }*/

        const char *no_color = getenv("NO_COLOR");

#ifdef _WIN32
        // On Windows, TERM is usually not set, so enable colors by default if NO_COLOR is not set
        cmd_show_colors = no_color == NULL;
#else
        // On Unix, check TERM environment variable
        const char *term = getenv("TERM");
        cmd_show_colors = no_color == NULL && term != NULL && strcmp(term, "dumb") != 0;
#endif
    }

#ifdef _WIN32
    if(cmd_show_colors) {
        // On Windows, we need to enable ANSI escape codes for stdout and stderr
        HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode_out, mode_err;

        // Enable ANSI escape codes for stdout
        if(h_out != INVALID_HANDLE_VALUE && GetConsoleMode(h_out, &mode_out)) {
            mode_out |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

            if(!SetConsoleMode(h_out, mode_out)) {
                // If we can't enable ANSI codes, disable colors
                cmd_show_colors = 0;
            }
        } else {
            // If we can't get console mode, disable colors
            cmd_show_colors = 0;
        }

        // Enable ANSI escape codes for stderr
        if(cmd_show_colors && h_err != INVALID_HANDLE_VALUE && GetConsoleMode(h_err, &mode_err)) {
            mode_err |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_err, mode_err);
        }
    }
#endif

    while((option = getopt_long(argc, argv, "hj:co:vVsw:", long_options, &option_index)) != -1) {
        char *endptr = NULL;

        switch(option) {
            case 'h':
                mjbsh_show_help(long_options, descriptions, commands, NULL);
                return 0;
            case 'c':
                cmd_interpret_mode = INTERPRET_MODE_CODEPOINT;
                break;
            case 'j': {
                endptr = NULL;
                cmd_json_indent = strtoul(optarg, &endptr, 10);

                if(endptr == optarg || *endptr != '\0' || cmd_json_indent > 10) {
                    fprintf(stderr, "JSON indent level must be a number between 0 and 10.\n");
                    mjbsh_show_help(long_options, descriptions, commands, NULL);

                    return 1;
                }

                break;
            }
            case 'o':
                if(strcmp(optarg, "plain") == 0) {
                    cmd_output_mode = OUTPUT_MODE_PLAIN;
                } else if(strcmp(optarg, "json") == 0) {
                    cmd_output_mode = OUTPUT_MODE_JSON;
                } else {
                    fprintf(stderr, "Invalid output mode: %s\n", optarg);
                    mjbsh_show_help(long_options, descriptions, commands, NULL);

                    return 1;
                }
                break;
            case 's':
                cmd_show_allowed_symbols = true;
                break;
            case 'v':
                ++cmd_verbose;
                break;
            case 'V':
                return mjbsh_show_version();
            case '?':
                // getopt_long already printed an error message
                break;
            default:
                abort();
        }
    }

    // After global options, the next argument is the subcommand
    if(optind >= argc) {
        fprintf(stderr, "No command specified.\n");
        mjbsh_show_help(long_options, descriptions, commands, NULL);

        return 1;
    }

    if(argc - optind == 1) {
        // Break command has a realtime mode
        if(!(strcmp(argv[optind], "break") == 0 || strcmp(argv[optind], "filter") == 0)) {
            fprintf(stderr, "No command value specified.\n");
            mjbsh_show_help(long_options, descriptions, commands, NULL);

            return 1;
        }
    }

    int next_argc = argc - optind - 1;
    char *const *next_argv = argv + optind + 1;

    for(int i = 0; commands[i].name != NULL; ++i) {
        if(strcmp(argv[optind], commands[i].name) == 0) {
            bool accepts_arguments = next_argc <= commands[i].max_arguments ||
                (commands[i].accepts_codepoint_list &&
                    cmd_interpret_mode == INTERPRET_MODE_CODEPOINT);

            if(!accepts_arguments) {
                fprintf(stderr, "%s: expected at most %d argument%s, received %d\n",
                    commands[i].name, commands[i].max_arguments,
                    commands[i].max_arguments == 1 ? "" : "s", next_argc);

                return 1;
            }

            return commands[i].function(next_argc, next_argv, commands[i].flags);
        }
    }

    fprintf(stderr, "Unknown command: %s\n", argv[optind]);
    mjbsh_show_help(long_options, descriptions, commands, NULL);

#ifdef _WIN32
    SetConsoleOutputCP(original_cp);
#endif

    return 1;
}
