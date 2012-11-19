/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <stdlib.h>
#include "v8-convert.hpp"
#include "DataTypes.h"
#include "Helpers.h"
#include "Session.h"
#include "Publication.h"

using namespace v8;

enum PublicationEvent
{
  EVT_MESSAGE = 0,
  EVT_STOP
};

Persistent<Function> Publication::constructor;


/*-----------------------------------------------------------------------------
 * Initialize the Publication module
 */
void Publication::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("Publication"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  t->PrototypeTemplate()->Set(String::NewSymbol("on"), FunctionTemplate::New(On)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("sendMessage"), FunctionTemplate::New(SendMessage)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("stop"), FunctionTemplate::New(Stop)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Publication object
 */
Handle<Value> Publication::New(const Arguments& args)
{
  HandleScope scope;
  Publication* pub = (Publication*)External::Unwrap(args[0]->ToObject());
  pub->Wrap(args.This());
  return args.This();
}

Handle<Value> Publication::NewInstance(Publication* publication)
{
  HandleScope scope;
  Handle<External> wrapper = External::New(publication);
  Handle<Value> argv[1] = { wrapper };
  Local<Object> instance = constructor->NewInstance(1, argv);

  instance->Set(String::NewSymbol("topic"), String::New(publication->GetTopic()), ReadOnly);
  switch (publication->GetQos())
  {
  case TVA_QOS_BEST_EFFORT:
    instance->Set(String::NewSymbol("qos"), String::New("BE"), ReadOnly);
    break;

  case TVA_QOS_GUARANTEED_CONNECTED:
    instance->Set(String::NewSymbol("qos"), String::New("GC"), ReadOnly);
    break;

  case TVA_QOS_GUARANTEED_DELIVERY:
    instance->Set(String::NewSymbol("qos"), String::New("GD"), ReadOnly);
    break;
  }

  return scope.Close(instance);
}

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Publication::Publication(Session* session)
{
  _session = session;
  _handle = TVA_INVALID_HANDLE;
  _topic = NULL;
  _qos = TVA_QOS_BEST_EFFORT;
  uv_mutex_init(&_sendLock);

  EventEmitterConfiguration events[] = 
  {
    { EVT_MESSAGE,  "send-message" },
    { EVT_STOP,     "stop"    },
  };
  SetValidEvents(2, events);
}

Publication::~Publication()
{
  if (_handle != TVA_INVALID_HANDLE)
  {
    tvaCancelPublication(_handle, TVA_INVALID_HANDLE);
  }
  if (_topic)
  {
    free(_topic);
  }
  uv_mutex_destroy(&_sendLock);
}


/*****     On     *****/

/*-----------------------------------------------------------------------------
  * Register for publication events
  *
  * publication.on(event, listener);
  *
  * Events / Listeners:
  *   'message-send'         - Message sent                            - function (message, err) { }
  *   'stop'                 - Publication stopped                     - function (err) { }
  */
Handle<Value> Publication::On(const Arguments& args)
{
  HandleScope scope;
  Publication* publication = ObjectWrap::Unwrap<Publication>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);          // event
  PARAM_REQ_FUNCTION(1, args);        // handler

  String::AsciiValue evt(args[0]->ToString());
  Local<Function> handler = Local<Function>::Cast(args[1]);

  if (publication->AddListener(*evt, Persistent<Function>::New(handler)) == false)
  {
    THROW_INVALID_EVENT_LISTENER("publication", *evt);
  }

  return scope.Close(args.This());
}


/*****     SendMessage     *****/

class SendMessageRequest
{
public:
  Publication* publication;
  char topic[256];
  std::list<MessageFieldData> fieldData;
  bool useSelfDescribing;
  bool invokeCallback;
  int messageType;
  TVA_STATUS result;
  Persistent<Function> complete;
  Persistent<Object> origMessage;

  SendMessageRequest(Publication* pub)
  {
    publication = pub;
    topic[0] = 0;
    messageType = 0;
    useSelfDescribing = false;
    invokeCallback = false;
  }
};

/*-----------------------------------------------------------------------------
 * Send a message
 *
 * publication.sendMessage(topic, message, [options], [callback]);
 *
 * // None of the members of the options object are required
 * options = {
 *    selfdescribe  : [ignore topic schema],                  (boolean, default: false)
 * });
 */
Handle<Value> Publication::SendMessage(const Arguments& args)
{
  HandleScope scope;
  Publication* pub = ObjectWrap::Unwrap<Publication>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);        // topic
  PARAM_REQ_OBJECT(1, args);        // message

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());
  Local<Object> message = Local<Object>::Cast(args[1]);
  Local<Object> options;
  Local<Function> complete;
  bool parseOptions = false;

  SendMessageRequest* request = new SendMessageRequest(pub);
  tva_strncpy(request->topic, *topic, sizeof(request->topic));

  // Check for additional optional arguments
  for (int i = 2; i < args.Length(); i++)
  {
    if (args[i]->IsFunction())
    {
      complete = Local<Function>::Cast(args[i]);
    }
    else if (args[i]->IsObject())
    {
      options = Local<Object>::Cast(args[i]);
      parseOptions = true;
    }
  }

  if (parseOptions)
  {
    std::vector<std::string> optionNames = cvv8::CastFromJS<std::vector<std::string> >(options->GetPropertyNames());
    for (size_t i = 0; i < optionNames.size(); i++)
    {
      char* optionName = (char*)(optionNames[i].c_str());
      Local<Value> optionValue = options->Get(String::NewSymbol(optionName));

      if (optionValue->IsUndefined())
      {
        continue;
      }

      if (tva_str_casecmp(optionName, "selfdescribe") == 0)
      {
        request->useSelfDescribing = optionValue->BooleanValue();
      }
#ifdef TVA_MSG_ISFROMJMS
      else if (tva_str_casecmp(optionName, "messageType") == 0)
      {
        String::AsciiValue messageType(optionValue->ToString());
        if (tva_str_casecmp(*messageType, "text") == 0)
        {
          request->messageType = TVA_JMS_MSG_TYPE_TEXT;
        }
        else
        {
          request->messageType = TVA_JMS_MSG_TYPE_MAP;
        }
      }
#endif
    }
  }

  if (!complete.IsEmpty())
  {
    request->complete = Persistent<Function>::New(complete);
  }

  // Get message data
  std::vector<std::string> fieldNames = cvv8::CastFromJS<std::vector<std::string> >(message->GetPropertyNames());
  for (size_t i = 0; i < fieldNames.size(); i++)
  {
    char* fieldName = (char*)(fieldNames[i].c_str());

    MessageFieldData field;
    tva_strncpy(field.name, fieldName, sizeof(field.name));

    Local<Value> fieldValue = message->Get(String::NewSymbol(fieldName));
    if (fieldValue->IsBoolean())
    {
      field.type = MessageFieldDataTypeBoolean;
      field.value.boolValue = fieldValue->BooleanValue();
    }
    else if (fieldValue->IsInt32())
    {
      field.type = MessageFieldDataTypeInt32;
      field.value.int32Value = fieldValue->Int32Value();
    }
    else if (fieldValue->IsNumber())
    {
      field.type = MessageFieldDataTypeNumber;
      field.value.numberValue = fieldValue->NumberValue();
    }
    else if (fieldValue->IsDate())
    {
      field.type = MessageFieldDataTypeDate;
      field.value.dateValue.timeInMicroSecs = (TVA_UINT64)(fieldValue->NumberValue() * 1000);
    }
    else if (fieldValue->IsString())
    {
      field.type = MessageFieldDataTypeString;
      String::AsciiValue strValue(fieldValue->ToString());
      field.value.stringValue = strdup(*strValue);
    }
    else
    {
      field.type = MessageFieldDataTypeNone;
    }

    request->fieldData.push_back(field);
  }

  request->origMessage = Persistent<Object>::New(message);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Publication::SendMessageWorker, Publication::SendMessageWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform send message
 */
void Publication::SendMessageWorker(uv_work_t* req)
{
  SendMessageRequest* request = (SendMessageRequest*)req->data;
  Publication* publication = request->publication;

  TVA_PUBLISH_MESSAGE_DATA_HANDLE messageData = TVA_INVALID_HANDLE;
  TVA_STATUS rc = TVA_ERROR;

  do
  {
    if (request->useSelfDescribing)
    {
      // Create a new self describing message
      rc = tvaSelfDescMsgTNew(publication->GetHandle(), request->topic, TVA_INVALID_HANDLE, (TVA_UINT32)request->fieldData.size(), &messageData);
      if (rc != TVA_OK) break;
    }
    else
    {
      // Create a new message
      rc = tvaCreateMessageForTopic(publication->GetHandle(), request->topic, &messageData);
      if (rc != TVA_OK) break;
    }

#ifdef TVA_MSG_ISFROMJMS
    if (request->messageType == TVA_JMS_MSG_TYPE_TEXT)
    {
      tvaPubMsgInfoSet(messageData, TVA_PUBMSGINFO_JMS_MSG_TYPE, &request->messageType, (TVA_INT32)sizeof(request->messageType));
    }
#endif

    // Loop through the fields in the message object from JavaScript and add fields to the Tervela message
    while (!request->fieldData.empty())
    {
      MessageFieldData field = request->fieldData.front();
      request->fieldData.pop_front();

      switch (field.type)
      {
      case MessageFieldDataTypeNumber:
        rc = tvaSetDoubleIntoMessageByFieldName(messageData, field.name, field.value.numberValue);
        break;

      case MessageFieldDataTypeInt32:
        rc = tvaSetIntIntoMessageByFieldName(messageData, field.name, field.value.int32Value);
        break;

      case MessageFieldDataTypeString:
        rc = tvaSetStringIntoMessageByFieldName(messageData, field.name, field.value.stringValue);
        free(field.value.stringValue);
        break;

      case MessageFieldDataTypeBoolean:
        rc = tvaSetBooleanIntoMessageByFieldName(messageData, field.name, field.value.boolValue);
        break;

      case MessageFieldDataTypeDate:
        rc = tvaSetDateTimeIntoMessageByFieldName(messageData, field.name, field.value.dateValue);
        break;

      default:
        // Unsupported field type, ignore
        rc = TVA_OK;
        break;
      }

      if (rc != TVA_OK)
      {
        request->fieldData.clear();
        break;
      }
    }

    if (rc == TVA_OK)
    {
      publication->Lock();

      // Send the Tervela message
      if (publication->GetQos() == TVA_QOS_GUARANTEED_DELIVERY)
      {
        rc = publication->GetSession()->SendGdMessage(publication, messageData, request->origMessage, request->complete);
        request->invokeCallback = false;
      }
      else
      {
        /* Tervela API versions prior to 5.1.0 don't include tvaSendMessageEx */
#ifdef TVA_PUB_FL_NOBLOCK
        rc = tvaSendMessageEx(messageData, TVA_PUB_FL_NOBLOCK);
#else
        rc = tvaSendMessage(messageData);
#endif
        request->invokeCallback = true;
      }

      publication->Unlock();
    }
  } while (0);

  if (messageData != TVA_INVALID_HANDLE)
  {
    tvaReleasePublishData(messageData);
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Send message complete
 */
void Publication::SendMessageWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  SendMessageRequest* request = (SendMessageRequest*)req->data;
  delete req;

  if ((request->invokeCallback) || (request->result != TVA_OK))
  {
    Handle<Value> argv[2];
    if (request->result == TVA_OK)
    {
      argv[0] = Undefined();
    }
    else
    {
      argv[0] = String::New(tvaErrToStr(request->result));
    }
    argv[1] = request->origMessage;

    TryCatch tryCatch;

    // Call complete callback if it was set
    if (!request->complete.IsEmpty())
    {
      request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
      request->complete.Dispose();
    }

    // Emit "message" event
    request->publication->Emit(EVT_MESSAGE, 2, argv);

    if (tryCatch.HasCaught())
    {
      node::FatalException(tryCatch);
    }

    request->origMessage.Dispose();
  }

  delete request;
}

/*-----------------------------------------------------------------------------
 * Send of message has completed.
 */
void Publication::SendMessageComplete(int argc, v8::Handle<v8::Value> argv[])
{
  Emit(EVT_MESSAGE, argc, argv);
}


/*****     Stop     *****/

struct StopPublicationRequest
{
  Publication* publication;
  TVA_STATUS result;
};

/*-----------------------------------------------------------------------------
  * Stop the publication
  *
  * publication.stop({
  *    complete      : [callback on create complete]          (function (err), required)
  * });
  */
Handle<Value> Publication::Stop(const v8::Arguments& args)
{
  HandleScope scope;
  Publication* publication = ObjectWrap::Unwrap<Publication>(args.This());

  if (args.Length() > 0)
  {
    // Read argument (complete callback)
    Local<Function> complete = Local<Function>::Cast(args[0]);
    publication->AddOnceListener(EVT_STOP, Persistent<Function>::New(complete));
  }

  // Send data to worker thread
  StopPublicationRequest* request = new StopPublicationRequest;
  request->publication = publication;

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Publication::StopWorker, Publication::StopWorkerComplete);

  return scope.Close(Undefined());
}

/*-----------------------------------------------------------------------------
 * Perform stop publication
 */
void Publication::StopWorker(uv_work_t* req)
{
  StopPublicationRequest* request = (StopPublicationRequest*)req->data;
  Publication* publication = request->publication;

  TVA_STATUS rc = tvaCancelPublication(publication->GetHandle(), TVA_INVALID_HANDLE);
  publication->SetHandle(TVA_INVALID_HANDLE);

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Stop publication complete
 */
void Publication::StopWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  StopPublicationRequest* request = (StopPublicationRequest*)req->data;
  delete req;

  Handle<Value> argv[1];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
  }

  TryCatch tryCatch;

  request->publication->Emit(EVT_STOP, 1, argv);

  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  delete request;
}
