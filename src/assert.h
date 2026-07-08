#pragma once

#include <stacktrace>
#include <iostream>

#ifndef NDEBUG
    #define traced_assert(x) \
        if (!(x)) { \
            std::cout << std::endl << std::endl << std::stacktrace::current() << std::endl << "Assertion failed: " << #x << ", file " << __FILE__ << ", line " << __LINE__ << std::endl; \
            std::terminate(); \
        }
#else
    #define traced_assert(x) ;
#endif