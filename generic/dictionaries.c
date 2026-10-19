/*
 * dictionaries.c -- definitions for the combined dictionary table
 *
 * Copyright (C) 2016 and later: Unicode, Inc. and others.
 * License & terms of use: http://www.unicode.org/copyright.html
 * Copyright (c) 2015 International Business Machines Corporation
 * and others. All Rights Reserved.
 */

#include "dictionaries.h"

const LanguageDict* const all_dicts[] = {
    &khmer_dict,
    &lao_dict,
    &thai_dict,
};

const size_t all_dicts_count = 3;