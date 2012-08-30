/**
* pub.js
*
* Publisher test application
*
* Usage:
*   node pub.js --tmx=tmx[:tmx] [options]
*      --user=username                  (optional, default: tervela)
*      --pass=password                  (optional, default: tva123ma1)
*      --tmx=tmx[:tmx]                  (required)
*      --gdname=gd_client_name          (required for GD)
*      --topic=topic[:topic]            (optional, default: TEST.BULK.*)
*      --tcount=wc_topic_count          (optional, default: 1)
*      --tstart=wc_topic_start          (optional, default: 0)
*      --burst=msg_burst_count          (optional, default: 10)
*      --delay=inter_burst_ms           (optional, default: 10)
*      --duration=test_duration_s       (optional, default: 30)
*/

// Set NODE_PATH environment variable to include location of tervela.node file
var tervela = require("tervela");

process.on('uncaughtException', function (err) {
    console.log(err.stack);
});

var _username = "tervela";
var _password = "tva123ma1";
var _primaryTmx = "";
var _secondaryTmx = "";
var _gdName;
var _topicList = new Array();
var _wcTopicCount = 1;
var _wcTopicStart = 0;
var _burst = 10;
var _delay = 10;
var _testDuration = 30;
var _verbose = false;
var _exit = false;
var _testingComplete = false;

var args = process.argv.splice(2);
args.forEach(function (val, index, array) {
    var keyval = val.split("=");
    if (keyval.length == 2) {
        if (keyval[0] == "--user") {
            _username = keyval[1];
        }
        else if (keyval[0] == "--pass") {
            _password = keyval[1];
        }
        else if (keyval[0] == "--topic") {
            _topicList = keyval[1].split(":");
        }
        else if (keyval[0] == "--tcount") {
            _wcTopicCount = keyval[1];
        }
        else if (keyval[0] == "--tstart") {
            _wcTopicStart = keyval[1];
        }
        else if (keyval[0] == "--burst") {
            _burst = keyval[1];
        }
        else if (keyval[0] == "--delay") {
            _delay = keyval[1];
        }
        else if (keyval[0] == "--duration") {
            _testDuration = keyval[1];
        }
        else if (keyval[0] == "--tmx") {
            var ps = keyval[1].split(":");
            _primaryTmx = ps[0];
            if (ps.length > 1) {
                _secondaryTmx = ps[1];
            }
        }
        else if (keyval[0] == "--gdname") {
            _gdName = keyval[1];
        }
    }
    else if (keyval.length == 1) {
        if (keyval[0] == "--verbose") {
            _verbose = true;
        }
        else if (keyval[0] == "--help") {
            _exit = true;
        }
    }
});

if ((!_exit) && (_primaryTmx.length == 0)) {
    console.log("Missing TMX");
    _exit = true;
}

if (_exit) {
    printUsage();
}
else {
    if (_topicList.length == 0) {
        _topicList.push("TEST.BULK.*");
    }

    console.log("Running with options:");
    console.log("  username     : " + _username);
    console.log("  password     : " + _password);
    console.log("  primaryTmx   : " + _primaryTmx);
    console.log("  secondaryTmx : " + _secondaryTmx);
    console.log("  gdName       : " + _gdName);
    console.log("  topics       : " + printList(_topicList));
    console.log("  wcTopicCount : " + _wcTopicCount);
    console.log("  wcTopicStart : " + _wcTopicStart);
    console.log("  burst        : " + _burst);
    console.log("  delay        : " + _delay);
    console.log("  duration     : " + _testDuration);
    console.log("  verbose      : " + _verbose);
    console.log("");

    // Login to the TMX/TMXs
    tervela.connect({
        username: _username,
        password: _password,
        primaryTmx: _primaryTmx,
        secondaryTmx: _secondaryTmx,
        name: _gdName
    }, function (err, session) {
        if (err) {
            console.log("Login failed: " + err);
            return;
        }

        session
            .on('connect-info', function (activeTmx, standbyTmx) {
                var info = "* Session connected to active TMX " + activeTmx;
                if (standbyTmx) {
                    info += ", standby TMX " + standbyTmx;
                }
                console.log(info);
            })
            .on('gds-lost', function () {
                console.log("* Lost communications with the GDS, GD operations affected");
            })
            .on('gds-restored', function () {
                console.log("* Communications with the GDS have been restored, GD operations will continue");
            });

        console.log("Login complete");

        var publications = new Array();
        _topicList.forEach(function (topic, index, array) {
            createPublication(session, topic, publications, _topicList.length);
        });
    });
}

var _pubCreateCount = 0;
function createPublication(session, topic, publications, maxCount) {
    session.createPublication(topic, function (err, pub) {
        if (!err) {
            console.log("Created publication on " + topic);
            publications.push(pub);
        }
        else {
            console.log("Error creating publication on " + topic + ": " + err);
            publications.push(undefined);
        }

        if (++_pubCreateCount == maxCount) {
            setTimeout(function () { mainPublishingLoop(session, publications); }, 10);
        }
    });
}

var _messageThreads = 0;
var _messageCount = 0;
var _totalSentMessages = 0;
var _totalSendErrors = 0;
var _messagesOutstanding = 0;

function mainPublishingLoop(session, publications) {
    // Convert test duration to milliseconds
    _testDuration *= 1000;

    var startDate = new Date();
    var endDate = new Date(startDate.getTime() + _testDuration);

    console.log("Starting test run for %d seconds, will end at " + endDate.toLocaleTimeString(), _testDuration / 1000);
    setTimeout(function () { testComplete(session, publications, startDate); }, _testDuration);

    for (var i = 0; i < publications.length; i++) {
        if (publications[i]) {
            _messageThreads++;
            messageThreadStart(publications[i], _topicList[i]);
        }
    }
}

function messageThreadStart(pub, baseTopic) {
    setTimeout(function () { messageThread(pub, baseTopic); }, 0);
}

function messageThread(pub, baseTopic) {
    if (_testingComplete) {
        if (_messagesOutstanding > 0) {
            // Messages have been sent but have not completed yet, continue waiting
            setTimeout(function () { messageThread(pub, baseTopic); }, 100);
        }
        else {
            // No messages outstanding, mark as complete
            _messageThreads--;
        }
    }
    else {
        var sent = 0;
        var topic = baseTopic;
        var topicLeaf = 0;
        for (var i = 0; i < _burst; i++) {
            topic = baseTopic.replace("*", "T" + (topicLeaf + _wcTopicStart));
            if (++topicLeaf >= _wcTopicCount) {
                topicLeaf = 0;
            }

            _messageCount++;
            var message = {
                publicationTopic: baseTopic,
                messageCount: _messageCount,
                sendDate: new Date()
            };

            _messagesOutstanding++;
            pub.sendMessage(topic, message, { selfdescribe: true }, function (err) {
                _messagesOutstanding--;
                if (err) {
                    _totalSendErrors++;
                    if (_verbose) {
                        console.log("Error sending on %s: %s", topic, err);
                    }
                }
                else {
                    _totalSentMessages++;
                    if (_verbose) {
                        console.log("Sent message %d", _totalSentMessages);
                    }
                }
            });

            if (++sent == _burst) {
                setTimeout(function () { messageThread(pub, baseTopic); }, _delay);
            }
        }
    }
}

function testComplete(session, publications, startDate) {
    var endDate = new Date();
    var timeDiff = endDate.getTime() - startDate.getTime();

    console.log("Test completed, shutting down...");
    _testingComplete = true;

    setTimeout(function () { waitingForMessageThreads(session, publications, timeDiff); }, 100);
}

function waitingForMessageThreads(session, publications, timeDiff) {
    if (_messageThreads > 0) {
        setTimeout(function () { waitingForMessageThreads(session, publications, timeDiff); }, 100);
    }
    else {
        shutdown(session, publications, timeDiff);
    }
}

function shutdown(session, publications, timeDiff) {
    var pubCount = 0;
    for (var i = 0; i < publications.length; i++) {
        var pub = publications[i];
        if (pub) {
            console.log("Deleting publication on " + _topicList[i]);
            pub.stop(function (err) {
                pubCount++;
                if (pubCount == publications.length) {
                    shutdownSession(session, timeDiff);
                }
            });
        }
        else {
            pubCount++;
            if (pubCount == publications.length) {
                shutdownSession(session, timeDiff);
            }
        }
    }
}

function shutdownSession(session, timeDiff) {
    session.close(function (err) {
        var mps = (_totalSentMessages / timeDiff) * 1000;
        console.log("Logout complete");
        console.log("Done.  Sent a total of %d messages, %d errors (%d MPS)", _totalSentMessages, _totalSendErrors, mps.toFixed(3));
    });
}

function printList(list) {
    var result = "";
    list.forEach(function (val, index, array) {
        result += val + " ";
    });

    return result;
}

function printUsage() {
    console.log("Usage: node pub.js --tmx=tmx[:tmx] [options]");
    console.log("  --user=username                  (optional, default: tervela)");
    console.log("  --pass=password                  (optional, default: tva123ma1)");
    console.log("  --topic=topic[:topic]            (optional, default: TEST.BULK.*)");
    console.log("  --tcount=wc_topic_count          (optional, default: 1)");
    console.log("  --tstart=wc_topic_start          (optional, default: 0)");
    console.log("  --burst=msg_burst_count          (optional, default: 10)");
    console.log("  --delay=inter_burst_ms           (optional, default: 10)");
    console.log("  --verbose                        (optional, default: false)");
}
