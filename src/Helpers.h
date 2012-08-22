#pragma once

#include <string.h>
#include <stdio.h>

#if defined(WIN32)
#define tva_strncpy(d, s, n)    strncpy_s(d, n, s, (n) - 1)
#define tva_str_casecmp         _stricmp
#define strdup                  _strdup
#define snprintf                sprintf_s

#else

#include <strings.h>

#define tva_strncpy(d, s, n)    do { \
    char *p = (d); \
    strncpy(p, s, (n) - 1); \
    p[(n)-1] = '\0'; \
  } while(0)
#define tva_str_casecmp         strcasecmp
#endif

/*-----------------------------------------------------------------------------
 * Parameter checking helpers
 */
#define PARAM_REQ_NUM(n, c)                                                     \
  if (c < n) {                                                                  \
    char msg[80];                                                               \
    snprintf(msg, sizeof(msg),                                                  \
        "Wrong number of arguments - got %d, expecting %d", c, n);              \
    v8::ThrowException(v8::Exception::TypeError(String::New(msg)));             \
    return scope.Close(v8::Undefined());                                        \
  }

#define PARAM_REQ_STRING(idx, args)                                             \
  if (!args[idx]->IsString()) {                                                 \
    char msg[80];                                                               \
    snprintf(msg, sizeof(msg),                                                  \
        "Incorrect arguments format - arg %d should be of type String", idx);   \
    v8::ThrowException(v8::Exception::TypeError(String::New(msg)));             \
    return scope.Close(v8::Undefined());                                        \
  }

#define PARAM_REQ_NUMBER(idx, args)                                             \
  if (!args[idx]->IsNumber()) {                                                 \
    char msg[80];                                                               \
    snprintf(msg, sizeof(msg),                                                  \
        "Incorrect arguments format - arg %d should be of type Number", idx);   \
    v8::ThrowException(v8::Exception::TypeError(String::New(msg)));             \
    return scope.Close(v8::Undefined());                                        \
  }

#define PARAM_REQ_FUNCTION(idx, args)                                           \
  if (!args[idx]->IsFunction()) {                                               \
    char msg[80];                                                               \
    snprintf(msg, sizeof(msg),                                                  \
        "Incorrect arguments format - arg %d should be of type Function", idx); \
    v8::ThrowException(v8::Exception::TypeError(String::New(msg)));             \
    return scope.Close(v8::Undefined());                                        \
  }

#define PARAM_REQ_OBJECT(idx, args)                                             \
  if (!args[idx]->IsObject()) {                                                 \
    char msg[80];                                                               \
    snprintf(msg, sizeof(msg),                                                  \
        "Incorrect arguments format - arg %d should be of type Object", idx);   \
    v8::ThrowException(v8::Exception::TypeError(String::New(msg)));             \
    return scope.Close(v8::Undefined());                                        \
  }
