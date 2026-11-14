/*
 * utility.cpp - Implements utility functions.
 *
 * Copyright (c) 2024-2025 Jimmy Cassis
 */

#include "pch.h"
#include "utility.h"

unsigned int parse_time(const char* input)
{
    unsigned int value = 0;
    unsigned int multiplier = 1000;
    const char* ptr = input;
    unsigned int colon_count = 0;

    while (*ptr && ((*ptr >= '0' && *ptr <= '9') || *ptr == ':'))
    {
        colon_count += *ptr == ':';
        ++ptr;
    }
    if (colon_count > 2)
        return BORK_TIME;
    if (*ptr && *ptr != '.' && *ptr != ',')
        return BORK_TIME;
    if (*ptr)
        ++ptr;
    while (*ptr && *ptr >= '0' && *ptr <= '9')
        ++ptr;
    if (*ptr)
        return BORK_TIME;

    ptr = strrchr(input, ':');
    if (!ptr)
        ptr = input;
    for (;;)
    {
        char* end;
        if (ptr != input)
            ++ptr;
        if (multiplier == 1000)
        {
            double temp = pfc::string_to_float(ptr);
            if ((colon_count && temp >= 60.0) || temp * 1000.0 >= static_cast<double>(BORK_TIME))
                return BORK_TIME;
            value = static_cast<unsigned int>(temp * 1000.0f);
        }
        else
        {
            unsigned int temp = strtoul(ptr, &end, 10);
            if (temp >= 60 && multiplier < 3600000)
                return BORK_TIME;
            value += temp * multiplier;
        }
        if (ptr == input)
            break;
        ptr -= 2;
        while (ptr > input && *ptr != ':')
            --ptr;
        multiplier *= 60;
    }

    return value;
}

// PSFlib joins repeated tags with '\n'; only the first value is used.
unsigned int parse_first_time(const char* input)
{
    pfc::string first;
    first.set_string(input, strcspn(input, "\n"));
    return parse_time(first);
}

void print_time(unsigned int ms, char* out)
{
    char frac[8];
    unsigned int h, m, s;
    if (ms % 1000)
    {
        sprintf_s(frac, ".%3.3d", ms % 1000);
        for (int i = 3; i > 0; --i)
            if (frac[i] == '0')
                frac[i] = '\0';
        if (!frac[1])
            frac[0] = '\0';
    }
    else
    {
        frac[0] = '\0';
    }
    h = ms / (60 * 60 * 1000);
    m = (ms % (60 * 60 * 1000)) / (60 * 1000);
    s = (ms % (60 * 1000)) / 1000;
    if (h)
        sprintf_s(out, 16, "%d:%2.2d:%2.2d%s", h, m, s, frac);
    else if (m)
        sprintf_s(out, 16, "%d:%2.2d%s", m, s, frac);
    else
        sprintf_s(out, 16, "%d%s", s, frac);
}

bool is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool parse_tag_bool(const char* value)
{
    if (!value)
        return false;

    while (*value && is_ascii_space(*value))
        ++value;

    if (!*value)
        return false;

    char* end = nullptr;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end != value)
    {
        while (*end && is_ascii_space(*end))
            ++end;
        if (!*end)
            return parsed != 0;
    }

    return true;
}