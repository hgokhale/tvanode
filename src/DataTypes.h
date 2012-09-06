/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <vector>
#include "tvaClientAPIInterface.h"

/*-----------------------------------------------------------------------------
 * Message field type
 */
enum MessageFieldDataType
{
  MessageFieldDataTypeNone,
  MessageFieldDataTypeBoolean,
  MessageFieldDataTypeNumber,
  MessageFieldDataTypeDate,
  MessageFieldDataTypeString
};

/*-----------------------------------------------------------------------------
 * Message field data
 */
struct MessageFieldData
{
  char name[64];
  MessageFieldDataType type;
  union
  {
    double numberValue;
    char* stringValue;
    bool boolValue;
    TVA_DATE dateValue;
  } value;
};

/*-----------------------------------------------------------------------------
 * Message received event
 */
struct MessageEvent
{
  TVA_MESSAGE* tvaMessage;
  char topic[256];
  TVA_UINT64 generationTime;
  TVA_UINT64 receiveTime;
  std::vector<MessageFieldData> fieldData;
  bool isLastMessage;
};
