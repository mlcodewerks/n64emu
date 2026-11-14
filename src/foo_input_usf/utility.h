/*
 * utility.h - Utility functions header file.
 *
 * Copyright (c) 2023-2025 Jimmy Cassis
 */

#pragma once

#define BORK_TIME 0xC0CAC01A

unsigned int parse_time(const char* input);
unsigned int parse_first_time(const char* input);
void print_time(unsigned int ms, char* out);
bool is_ascii_space(char c);
bool parse_tag_bool(const char* value);