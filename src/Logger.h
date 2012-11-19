/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"

class Logger: node::ObjectWrap
{
public:
  /*-----------------------------------------------------------------------------
   * Get the current logging levels
   *
   * var logLevels = logger.getLevels();
   */
  static v8::Handle<v8::Value> GetLevels(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Add the given level to the set of current levels
   *
   * logger.setLevel(logger.INFO);
   */
  static v8::Handle<v8::Value> SetLevel(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Remove the given level from the set of current levels
   *
   * logger.clearLevel(logger.INFO);
   */
  static v8::Handle<v8::Value> ClearLevel(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Write a message to the log
   *
   * logger.write("message");
   */
  static v8::Handle<v8::Value> Write(const v8::Arguments& args);


  /* Internal methods */
  Logger(char* filename, char* tagname);
  ~Logger();

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(char* filename, char* tagname);

private:
  static v8::Persistent<v8::Function> constructor;
};
