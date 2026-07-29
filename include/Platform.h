#pragma once

#ifdef _WIN32
    #define PALSCHEMA_API __declspec(dllexport)
#else
    #define PALSCHEMA_API __attribute__((visibility("default")))
#endif
