/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <list>
#include "tvaClientAPIInterface.h"

/*-----------------------------------------------------------------------------
 * Message field type
 */
enum MessageFieldDataType
{
  MessageFieldDataTypeNone,
  MessageFieldDataTypeBoolean,
  MessageFieldDataTypeInt32,
  MessageFieldDataTypeNumber,
  MessageFieldDataTypeDate,
  MessageFieldDataTypeString,
  MessageFieldDataTypeBooleanArray,
  MessageFieldDataTypeInt16Array,
  MessageFieldDataTypeInt32Array,
  MessageFieldDataTypeInt64Array,
  MessageFieldDataTypeFloatArray,
  MessageFieldDataTypeDoubleArray,
  MessageFieldDataTypeDateArray,
  MessageFieldDataTypeStringArray,
};

/*-----------------------------------------------------------------------------
 * Message field data
 */
struct MessageFieldData
{
  char name[64];
  MessageFieldDataType type;
  int count;
  union
  {
    double numberValue;
    int int32Value;
    char* stringValue;
    bool boolValue;
    TVA_DATE dateValue;
    void* arrayValue;
  } value;
};

/*-----------------------------------------------------------------------------
 * Message received event
 */
struct MessageEvent
{
  TVA_MESSAGE* tvaMessage;
  std::list<MessageFieldData> fieldData;
  int jmsMessageType;
  bool isLastMessage;
};
