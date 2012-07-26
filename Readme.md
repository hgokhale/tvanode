# Tervela-node

Tervela-node allows access to the Tervela Messaging Framework from within a Node.js application.

## How to Install

Download the source code from GitHub and build it.

Building Tervela-node requires the Tervela Client API to be installed on the build machine.  Running Tervela-node also requires the Tervela Client API to be installed on the machine.

## How to Use

First, require `tervela`:

    var tervela = require('tervela');

Next, create a session and login to the TMX:

    var session = new tervela.Session();
    
    session.login({
        username: API-username,
        password: API-password,
        primaryTmx: primary-tmx,
        secondaryTmx: secondary-tmx,
        statusEvent: function (code, msg) { 
            console.log("* Session notify (" + code + "): " + msg);
        },
        timeout: 30,
        complete: function (err) {
            if (err != undefined) {
                console.log("Error logging in: " + err);
            }
            else {
                .....
            }
        }
    });


Once logged in, the `Session` object is used to create publications and subscriptions.  Publications and subscriptions are created empty, and they need to be started before they can be used.

    var publication = session.createPublication();
    publication.start({
        topic: topic-string,
        complete: function (err) {
            if (err != undefined) {
                console.log("Error starting publication: " + err);
            }
            else {
                // Once a publication is started, messages can be sent
                var myMessage = {
                    intField: 42,
                    stringField: "Hello",
                    dateField: new Date(),
                    boolField: true
                };
                
                publication.sendMessage({
                    topic: topic-string,
                    message: myMessage,
                    selfdescribe: true,
                    complete: function (err) {
                        if (err != undefined) {
                            console.log("Error sending message: " + err);
                        }
                    }
                });
            }
        }
    });
    
    var subscription = session.createSubscription();
    subscription.start({
        topic: topic-string,
        messageEvent: function (msg) {
            // Once a subscription is started messages can be received
            console.log("Message received!");
            console.log("- Topic: " + msg.topic);
            console.log("- GenerationTime: " + msg.generationTime);
            console.log("- ReceiveTime: " + msg.receiveTime);
            
            for (var field in msg.fields) {
                var fieldContents = msg.fields[field];
                console.log("--- " + field + " : " + fieldContents);
            }
        },
        complete: function (err) {
            if (err != undefined) {
                console.log("Error starting subscription: " + err);
            }
        }
    });

When publications and subscriptions are no longer needed, they should be stopped:

    publication.stop({
        complete: function (err) { 
            if (err != undefined) {
                console.log("Error stopping publication: " + err);
            }
        }
    });
    
    subscription.stop({
        complete: function (err) { 
            if (err != undefined) {
                console.log("Error stopping subscription: " + err);
            }
        }
    });

When a session is no longer needed, it should be terminated by calling logout:

    session.logout({
        complete: function (err) { 
            if (err != undefined) {
                console.log("Error in logout: " + err);
            }
        }
    };

See the `test` directory for samples.

## Full API documentation

##### About the API

* The API is comprised of three objects, `Session`, `Publication`, and `Subscription`.  All applications will create a session, and from there, any number of publications and subscriptions.
* All object methods take a single parameter, an object that describes how the method should be invoked.  Properties in the object specify different parameters, and the values of those properties are the values of the parameters.
* The last (or sometimes only) parameter to all methods is the `complete` property - the function that is invoked when the method completes.  All `complete` functions behave the same, they all have a single `err` parameter.  If `err` is `undefined`, no error occurred and the method completed successfully.  Otherwise `err` will be a string describing the error that occurred.

### Session

#### Session.login - Login to the Tervela fabric

    session.login({
        username      : [API username],                   (string, required)
        password      : [API password],                   (string, required)
        primaryTmx    : [TMX name or address],            (string, required)
        secondaryTmx  : [TMX name or address],            (string, optional (default: [empty]))
        timeout       : [login timeout in ms],            (integer, optional (default: 30000))
        statusEvent   : [callback for session events],    (function (code, msg), optional)
        complete      : [callback on login complete]      (function (err), required)
    });

* `secondaryTmx` is used when logging into a TMX FT pair
* A `timeout` of 0 means login will never timeout, and will internally retry until successful
* `statusEvent` is used to be notified of Tervela session events. See the Tervela Client Application Programming Interface (API) C Language Reference for more information and a list of event codes

#### Session.logout - Logout of the Tervela fabric

    session.logout({
        complete      : [callback on logout complete]     (function (err), required)
    });

#### Session.createPublication - Create a new publication object

    var pub = session.createPublication();

#### Session.createSubscription - Create a new subscription object

    var sub = session.createSubscription();

### Publication

#### Publication.start - Start the publication, get ready to send messages

    pub.start({
        topic         : [topic name],                     (string, required)
        complete      : [callback on start complete]      (function (err), required)
    });

* `topic` can be either a discrete or wildcard topic

#### Publication.sendMessage - Send a message

    pub.sendMessage({
        topic         : [topic name],                     (string, required)
        message       : [message to send],                (object, required)
        selfdescribe  : [ignore topic schema],            (boolean, optional (default: false))
        complete      : [callback on send complete],      (function (err), required)
    });

* The `topic` used when sending a message must match the `topic` used to create the publication, either directly or within a wildcard subset.  
For example, if the publication topic is a discrete topic such as A.1, the message must be sent on topic A.1.  However if the publication topic is a wildcard topic such as A.* then the message can be sent on any topic that falls under A.* (A.A, A.1, A.2, etc.).
* The `message` is a JavaScript object.  The property names become the field names in the Tervela message, and the property values become those field values.
* `selfdescribe` makes the message "self-describing" or not.  When `selfdescribe` is set to `false` (the default), property names must match schema field names; when `selfdescribe` is set to `true`, the schema field names are ignored and the property names become the field names.

#### Publication.stop - Stop the publication

    pub.stop({
        complete      : [callback on stop complete]      (function (err), required)
    });

### Subscription

#### Subscription.start - Start the subscription, get ready to receive messages

    sub.start({
        topic         : [topic name],                     (string, required)
        messageEvent  : [callback on message recv],       (function (message), required)
        complete      : [callback on start complete],     (function (err), required)
    });

* `topic` can be either a discrete or wildcard topic
* The `messageEvent` function is invoked for every message received.  The passed message is a JavaScript object with the given format:
    {
        topic,                 (string : message topic)
        generationTime,        (Date : when the message was sent by the publisher)
        receiveTime,           (Date : when the message was received)
        fields,                (Array : message fields list ([name]=value))
    }

#### Subscription.stop - Stop the subscription

    sub.stop({
        complete      : [callback on stop complete],      (function (err), required)
    });
