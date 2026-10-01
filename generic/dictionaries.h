/*
 * Most dictionaries copyright (C) 2016 and later: Unicode, Inc. and others. 
 * License & terms of use: http://www.unicode.org/copyright.html 
 * Copyright (c) 2015 International Business Machines Corporation 
 * and others. All Rights Reserved.
 */

#ifndef DICTIONARIES_H
#define DICTIONARIES_H

#include <stddef.h>

/* 
 * Auto-generated from ICU word lists. 
 * Each word is UTF-8 encoded. 
*/

typedef struct {
    const char* const* words;
    size_t count;
    const char* language;
    const char* code;
} LanguageDict;

/* Word arrays */
extern const char* const khmer_words[];
extern const size_t khmer_word_count;
extern const char* const lao_words[];
extern const size_t lao_word_count;
extern const char* const thai_words[];
extern const size_t thai_word_count;

/* Language dicts */
extern const LanguageDict khmer_dict;
extern const LanguageDict lao_dict;
extern const LanguageDict thai_dict;

/* All dicts - defined in dictionaries.c */
extern const LanguageDict* const all_dicts[];
extern const size_t all_dicts_count;



#endif /* DICTIONARIES_H */
