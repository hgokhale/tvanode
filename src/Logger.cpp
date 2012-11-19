/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include "v8-convert.hpp"
#include "Helpers.h"
#include "Logger.h"

using namespace v8;

Persistent<Function> Logger::constructor;

/*-----------------------------------------------------------------------------
 * Initialize the Logger module
 */
void Logger::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("Logger"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  t->PrototypeTemplate()->Set(String::NewSymbol("getLevels"), FunctionTemplate::New(GetLevels)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("setLevel"), FunctionTemplate::New(SetLevel)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("clearLevel"), FunctionTemplate::New(ClearLevel)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("write"), FunctionTemplate::New(Write)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Logger object
 */
Handle<Value> Logger::New(const Arguments& args)
{
  HandleScope scope;
  char* filename = (char*)External::Unwrap(args[0]->ToObject());
  char* tagname = (char*)External::Unwrap(args[1]->ToObject());
  Logger* logger = new Logger(filename, tagname);
  logger->Wrap(args.This());
  return args.This();
}

Handle<Value> Logger::NewInstance(char* filename, char* tagname)
{
  HandleScope scope;
  Handle<External> fileWrapper = External::New(filename);
  Handle<External> tagWrapper = External::New(tagname);
  Handle<Value> argv[2] = { fileWrapper, tagWrapper };
  Local<Object> instance = constructor->NewInstance(2, argv);

#ifdef TVA_LOGLEVEL_ERROR
  Local<Object> logLevel = Object::New();
  logLevel->Set(String::NewSymbol("ERROR"), cvv8::CastToJS(TVA_LOGLEVEL_ERROR), ReadOnly);
  logLevel->Set(String::NewSymbol("WARN"), cvv8::CastToJS(TVA_LOGLEVEL_WARN), ReadOnly);
  logLevel->Set(String::NewSymbol("INFO"), cvv8::CastToJS(TVA_LOGLEVEL_INFO), ReadOnly);
  logLevel->Set(String::NewSymbol("DATA"), cvv8::CastToJS(TVA_LOGLEVEL_DATA), ReadOnly);
  logLevel->Set(String::NewSymbol("STATE"), cvv8::CastToJS(TVA_LOGLEVEL_STATE), ReadOnly);
  logLevel->Set(String::NewSymbol("VSTATE"), cvv8::CastToJS(TVA_LOGLEVEL_VSTATE), ReadOnly);
  logLevel->Set(String::NewSymbol("STATS"), cvv8::CastToJS(TVA_LOGLEVEL_STATS), ReadOnly);
  logLevel->Set(String::NewSymbol("QSTATS"), cvv8::CastToJS(TVA_LOGLEVEL_QSTATS), ReadOnly);
  logLevel->Set(String::NewSymbol("RSTATS"), cvv8::CastToJS(TVA_LOGLEVEL_RSTATS), ReadOnly);
  logLevel->Set(String::NewSymbol("LSTATS"), cvv8::CastToJS(TVA_LOGLEVEL_LSTATS), ReadOnly);
  logLevel->Set(String::NewSymbol("VSTATS"), cvv8::CastToJS(TVA_LOGLEVEL_VSTATS), ReadOnly);
  logLevel->Set(String::NewSymbol("DIAG"), cvv8::CastToJS(TVA_LOGLEVEL_DIAG), ReadOnly);
  logLevel->Set(String::NewSymbol("VDIAG"), cvv8::CastToJS(TVA_LOGLEVEL_VDIAG), ReadOnly);

  instance->Set(String::NewSymbol("Level"), logLevel, ReadOnly);
#endif

  return scope.Close(instance);
}

#ifdef TVA_LOGLEVEL_ERROR

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Logger::Logger(char* filename, char* tagname)
{
  tvaLogOpen(filename, tagname);
}

Logger::~Logger()
{
}

/*-----------------------------------------------------------------------------
  * Get the current logging levels
  *
  * var logLevels = logger.getLevels();
  */
Handle<Value> Logger::GetLevels(const Arguments& args)
{
  HandleScope scope;
  Local<Integer> currentLevels = Integer::NewFromUnsigned(tvaLogLevelsGet());
  return scope.Close(currentLevels);
}

/*-----------------------------------------------------------------------------
  * Add the given level to the set of current levels
  *
  * logger.setLevel(logger.INFO);
  */
Handle<Value> Logger::SetLevel(const Arguments& args)
{
  HandleScope scope;

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_NUMBER(0, args);          // level

  int level = Local<Number>::Cast(args[0])->Int32Value();
  tvaLogLevelSet(level);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
  * Remove the given level from the set of current levels
  *
  * logger.clearLevel(logger.INFO);
  */
Handle<Value> Logger::ClearLevel(const Arguments& args)
{
  HandleScope scope;

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_NUMBER(0, args);          // level

  int level = Local<Number>::Cast(args[0])->Int32Value();
  tvaLogLevelClear(level);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
  * Write a message to the log
  *
  * logger.write("message");
  */
Handle<Value> Logger::Write(const Arguments& args)
{
  HandleScope scope;

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_NUMBER(0, args);          // level
  PARAM_REQ_STRING(1, args);          // message

  int level = Local<Number>::Cast(args[0])->Int32Value();
  String::AsciiValue message(args[1]->ToString());

  tvaLogWrite(level, *message);

  return scope.Close(args.This());
}

#else   // TVA_LOGLEVEL_ERROR not defined, Logger won't be able to do anything

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Logger::Logger(char* filename, char* tagname)
{
}

Logger::~Logger()
{
}

/*-----------------------------------------------------------------------------
  * Get the current logging levels
  *
  * var logLevels = logger.getLevels();
  */
Handle<Value> Logger::GetLevels(const Arguments& args)
{
  HandleScope scope;
  Local<Integer> currentLevels = Integer::New(0);
  return scope.Close(currentLevels);
}

/*-----------------------------------------------------------------------------
  * Add the given level to the set of current levels
  *
  * logger.setLevel(logger.INFO);
  */
Handle<Value> Logger::SetLevel(const Arguments& args)
{
  HandleScope scope;
  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
  * Remove the given level from the set of current levels
  *
  * logger.clearLevel(logger.INFO);
  */
Handle<Value> Logger::ClearLevel(const Arguments& args)
{
  HandleScope scope;
  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
  * Write a message to the log
  *
  * logger.write("message");
  */
Handle<Value> Logger::Write(const Arguments& args)
{
  HandleScope scope;
  return scope.Close(args.This());
}

#endif
