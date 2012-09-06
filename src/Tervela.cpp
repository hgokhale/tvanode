/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <stdlib.h>
#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "tvaGDAPI.h"
#include "v8-convert.hpp"
#include "Helpers.h"
#include "Session.h"
#include "Publication.h"
#include "Subscription.h"
#include "Replay.h"

using namespace v8;


/*-----------------------------------------------------------------------------
 * Function prototypes
 */
Handle<Value> Connect(const Arguments& args);
Handle<Value> ConnectSync(const Arguments& args);


/*-----------------------------------------------------------------------------
 * Initialize the module
 */
void Init(Handle<Object> target)
{
  target->Set(String::NewSymbol("connect"), FunctionTemplate::New(Connect)->GetFunction());
  target->Set(String::NewSymbol("connectSync"), FunctionTemplate::New(ConnectSync)->GetFunction());
}


/*****     Connect     *****/

struct ConnectRequest
{
  Session* session;
  char* username;
  char* password;
  char* primaryTmx;
  char* secondaryTmx;
  char* gdClientName;
  int gdMaxOut;
  int timeout;
  TVA_STATUS result;
  Persistent<Function> complete;

  ConnectRequest()
  {
    session = NULL;
    username = NULL;
    password = NULL;
    primaryTmx = NULL;
    secondaryTmx = NULL;
    gdClientName = NULL;
    gdMaxOut = 1000;
    timeout = 30000;
  }
};

bool ConnectParseOptions(Local<Object> options, ConnectRequest* request);
void ConnectWorker(uv_work_t* req);
void ConnectWorkerComplete(uv_work_t* req);

/*-----------------------------------------------------------------------------
 * Create a new session and login to the Tervela fabric
 *
 * tervela.connect({connectOptions}, function (err, session) {
 *     // Connect complete
 *     // If `err` is set an error occurred and the session was not created
 * });
 *
 * connectOptions = {
 *     username       : [API username],                         (string, required)
 *     password       : [API password],                         (string, required)
 *     primaryTmx     : [TMX name or address],                  (string, required)
 *     secondaryTmx   : [TMX name or address],                  (string, optional (default: [empty]))
 *     timeout        : [login timeout in seconds],             (integer, optional (default: 30))
 *     name           : [client name for GD operations],        (string, only required when using GD)
 *     gdMaxOut       : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
 * };
 */
Handle<Value> Connect(const Arguments& args)
{
  HandleScope scope;

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_OBJECT(0, args);        // options
  PARAM_REQ_FUNCTION(1, args);      // 'complete' callback

  // Ready arguments
  Local<Object> options = Local<Object>::Cast(args[0]);
  Local<Function> complete = Local<Function>::Cast(args[1]);

  ConnectRequest* request = new ConnectRequest();
  request->complete = Persistent<Function>::New(complete);

  // Make sure required arguments were specified
  if (!ConnectParseOptions(options, request))
  {
    ThrowException(Exception::TypeError(String::New("Incomplete options")));
    return scope.Close(Undefined());
  }

  // Post work request
  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, ConnectWorker, ConnectWorkerComplete);

  return scope.Close(Undefined());
}

/*-----------------------------------------------------------------------------
 * Create a new session and login to the Tervela fabric
 *
 * var session = tervela.connect({connectOptions});
 *
 * connectOptions = {
 *     username       : [API username],                         (string, required)
 *     password       : [API password],                         (string, required)
 *     primaryTmx     : [TMX name or address],                  (string, required)
 *     secondaryTmx   : [TMX name or address],                  (string, optional (default: [empty]))
 *     timeout        : [login timeout in seconds],             (integer, optional (default: 30))
 *     name           : [client name for GD operations],        (string, only required when using GD)
 *     gdMaxOut       : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
 * };
 */
Handle<Value> ConnectSync(const Arguments& args)
{
  HandleScope scope;

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_OBJECT(0, args);        // options

  // Ready arguments
  Local<Object> options = Local<Object>::Cast(args[0]);

  ConnectRequest request;

  // Make sure required arguments were specified
  if (!ConnectParseOptions(options, &request))
  {
    ThrowException(Exception::TypeError(String::New("Incomplete options")));
    return scope.Close(Undefined());
  }

  // Call ConnectWorker synchronously
  uv_work_t req;
  req.data = &request;
  ConnectWorker(&req);

  free(request.username);
  free(request.password);
  free(request.primaryTmx);
  if (request.secondaryTmx) free(request.secondaryTmx);
  if (request.gdClientName) free(request.gdClientName);

  // Return result - Session object if successful, else error string
  Handle<Value> result;
  if (request.result == TVA_OK)
  {
    result = Local<Value>::New(Session::NewInstance(request.session));
    request.session->MarkInUse(true);
  }
  else
  {
    result = String::New(tvaErrToStr(request.result));
  }

  return scope.Close(result);
}

/*-----------------------------------------------------------------------------
 * Parse options
 */
bool ConnectParseOptions(Local<Object> options, ConnectRequest* request)
{
  // Get options data
  std::vector<std::string> optionNames = cvv8::CastFromJS<std::vector<std::string> >(options->GetPropertyNames());
  for (size_t i = 0; i < optionNames.size(); i++)
  {
    char* optionName = (char*)(optionNames[i].c_str());
    Local<Value> optionValue = options->Get(String::NewSymbol(optionName));

    if (optionValue->IsUndefined())
    {
      continue;
    }

    if (tva_str_casecmp(optionName, "username") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      request->username = strdup(*val);
    }
    else if (tva_str_casecmp(optionName, "password") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      request->password = strdup(*val);
    }
    else if (tva_str_casecmp(optionName, "tmx") == 0)
    {
      if (optionValue->IsString())
      {
        String::AsciiValue val(optionValue->ToString());
        request->primaryTmx = strdup(*val);
      }
      else if (optionValue->IsArray())
      {
        Handle<Array> tmxs = Handle<Array>::Cast(optionValue);
        char** p_tmx = &request->primaryTmx;
        Local<Value> element;
        
        element = tmxs->Get(0);
        if (!element->IsUndefined())
        {
          String::AsciiValue tmx(element->ToString());
          *p_tmx = strdup(*tmx);
          p_tmx = &request->secondaryTmx;
        }

        if (tmxs->Length() > 1)
        {
          element = tmxs->Get(1);
          if (!element->IsUndefined())
          {
            String::AsciiValue tmx(element->ToString());
            *p_tmx = strdup(*tmx);
          }
        }
      }
    }
    else if (tva_str_casecmp(optionName, "name") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      request->gdClientName = strdup(*val);
    }
    else if (tva_str_casecmp(optionName, "timeout") == 0)
    {
      request->timeout = optionValue->Int32Value() * 1000;
    }
    else if (tva_str_casecmp(optionName, "gdMaxOut") == 0)
    {
      request->gdMaxOut = optionValue->Int32Value();
    }
  }

  // Make sure required arguments were specified
  if (!request->username || !request->password || !request->primaryTmx)
  {
    return false;
  }

  return true;
}

/*-----------------------------------------------------------------------------
 * Perform login
 */
void ConnectWorker(uv_work_t* req)
{
  ConnectRequest* request = (ConnectRequest*)req->data;
  Session* session = new Session();
  TVA_STATUS rc;

  do
  {
    rc = tvaAppInitialize(NULL);
    if (rc != TVA_OK) break;

    TVA_SESSION_HANDLE sessionHandle;
    rc = tvaSessionNew(Session::SessionNotificationCallback, session, &sessionHandle);
    if (rc != TVA_OK) break;

    rc = tvaSessionLogin(sessionHandle, request->username, request->password, request->primaryTmx, request->secondaryTmx, request->timeout);
    if (rc != TVA_OK) break;

    tvaSrvcInitPE(sessionHandle, Replay::ReplayNotificationEvent);

    if (request->gdClientName)
    {
      TVAGD_CONTEXT_HANDLE gdContextHandle;
      rc = tvagdContextNew(sessionHandle, request->gdClientName, Session::SessionNotificationCallback, session, &gdContextHandle);
      if (rc != TVA_OK)
      {
        tvaSessionTerm(sessionHandle);
        break;
      }

      rc = tvagdContextCfgSet(gdContextHandle, TVA_GDCFG_PUB_MSG_MAX_OUTSTAND, &request->gdMaxOut, (TVA_INT32)sizeof(request->gdMaxOut));
      if (rc != TVA_OK)
      {
        tvagdContextTerm(gdContextHandle);
        tvaSessionTerm(sessionHandle);
        break;
      }

      rc = tvagdContextInit(gdContextHandle);
      if (rc != TVA_OK)
      {
        tvagdContextTerm(gdContextHandle);
        tvaSessionTerm(sessionHandle);
        break;
      }

      session->SetGdHandle(gdContextHandle);
      session->SetGdMaxOut(request->gdMaxOut);
    }

    session->SetHandle(sessionHandle);
    request->session = session;
  } while(0);

  if ((rc != TVA_OK) && (session))
  {
    delete session;
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Login complete
 */
void ConnectWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  ConnectRequest* request = (ConnectRequest*)req->data;
  delete req;

  Handle<Value> argv[2];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
    argv[1] = Local<Value>::New(Session::NewInstance(request->session));
    request->session->MarkInUse(true);
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
    argv[1] = Undefined();
  }

  TryCatch tryCatch;

  request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
  request->complete.Dispose();
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  free(request->username);
  free(request->password);
  free(request->primaryTmx);
  if (request->secondaryTmx) free(request->secondaryTmx);
  if (request->gdClientName) free(request->gdClientName);
  delete request;
}

/*-----------------------------------------------------------------------------
 * Node.js init entry point
 */
extern "C" {
  void NODE_EXTERN init (Handle<Object> target)
  {
    Init(target);
    Session::Init(target);
    Publication::Init(target);
    Subscription::Init(target);
    Replay::Init(target);
  }

  NODE_MODULE(tervela, init);
};
