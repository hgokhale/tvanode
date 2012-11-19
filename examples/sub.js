/**
 * sub.js
 *
 * This application creates one or more subscriptions and receives messages for the configured duration.
 *
 * Usage:
 *   node sub.js --tmx=tmx[:tmx] [options]
 *      --user=username                  (optional, default: tervela)
 *      --pass=password                  (optional, default: tva123ma1)
 *      --tmx=tmx[:tmx]                  (required)
 *      --gdname=gd_client_name          (required for GD)
 *      --topic=topic[:topic]            (optional, default: TEST.BULK.*)
 *      --tcount=wc_topic_count          (optional, default: 1)
 *      --tstart=wc_topic_start          (optional, default: 0)
 *      --duration=test_duration_s       (optional, default: 30)
 *      --qos=( "BE" | "GC" | "GD" )     (optional, default: "BE")
 *      --subname=subscription_name      (required for GD)
 *      --ackmode=( "auto" | "manual" )  (optional for GD, default: "auto")
 *
 * If a given topic is a wildcard topic the wildcard is expanded into "tcount" discrete topics, using the pattern:
 *   T[(tstart)...(tstart+tcount)]
 * So if the topic is TEST.BULK.*, and tstart=10 and tcount=20, the application will subscribe to
 *   TEST.BULK.T10 through TEST.BULK.T29
 * If a tcount of 0 is specified the application will subscribe on the wildcard topic directly.
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
var _testDuration = 30;
var _qos = "BE";
var _subName;
var _ackMode = "auto";
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
        else if (keyval[0] == "--qos") {
            _qos = keyval[1];
        }
        else if (keyval[0] == "--subname") {
            _subName = keyval[1];
        }
        else if (keyval[0] == "--ackmode") {
            _ackMode = keyval[1];
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
    console.log("  duration     : " + _testDuration);
    console.log("  qos          : " + _qos);
    console.log("  name         : " + _subName);
    console.log("  ackMode      : " + _ackMode);
    console.log("  verbose      : " + _verbose);
    console.log("");

    // Login to the TMX/TMXs
    console.log("Connecting to %s...", (_secondaryTmx) ? "TMXs" : "TMX");
    tervela.connect({
        username: _username,
        password: _password,
        tmx: [ _primaryTmx, _secondaryTmx ],
        name: _gdName
    }, function (err, session) {
        if (err) {
            console.log("Connect failed: " + err);
            return;
        }

        // Set up session event listeners
        session
            .on('connection-info', function (activeTmx, standbyTmx) {
                var info = "* Session connected to active TMX " + activeTmx;
                if (standbyTmx) {
                    info += ", standby TMX " + standbyTmx;
                }
                console.log(info);
            })
            .on('connection-lost', function () {
                console.log("* Lost session connection, all operations affected");
            })
            .on('connection-restored', function () {
                console.log("* Session connection restored, all operations will continue");
            })
            .on('gds-lost', function () {
                console.log("* Lost communications with the GDS, GD operations affected");
            })
            .on('gds-restored', function () {
                console.log("* Communications with the GDS have been restored, GD operations will continue");
            });

        console.log("Connected");

        // Determine total number of subscriptions
        var totalSubCount = 0;
        _topicList.forEach(function (topic, index, array) {
            if ((topic.indexOf("*") != -1) && (_wcTopicCount > 0)) {
                totalSubCount += _wcTopicCount;
            }
            else {
                totalSubCount++;
            }
        });

        // Create subscriptions
        var subscriptions = new Array();
        var topics = new Array();
        _topicList.forEach(function (val, index, array) {
            if ((val.indexOf("*") != -1) && (_wcTopicCount > 0)) {
                // Wildcard, expand to multiple subscriptions
                for (var i = 0; i < _wcTopicCount; i++) {
                    var idx = i + parseInt(_wcTopicStart);
                    var topic = val.replace("*", "T" + idx);
                    startSubscription(session, topic, subscriptions, topics, totalSubCount);
                }
            }
            else {
                // Discrete or single wildcard subscription
                startSubscription(session, val, subscriptions, topics, totalSubCount);
            }
        });
    });
}

var _totalRecvMessages = 0;
var _firstRecvDate;
var _lastRecvDate;

var _subCreateCount = 0;
function startSubscription(session, topic, subscriptions, topics, maxCount) {
    session.createSubscription(topic, { qos: _qos, name: _subName, ackMode: _ackMode }, function (err, sub) {
        if (err) {
            console.log("Error creating subscription on " + topic + ": " + err);
            subscriptions.push(undefined);
            topics.push(undefined);
        }
        else {
            sub.on('message', function (msg) {
                _totalRecvMessages++;
                if (!_firstRecvDate) {
                    _firstRecvDate = new Date();
                }
                _lastRecvDate = new Date();
                if (_verbose) {
                    console.log("  Processing message %s", msg.topic);
                    for (var fieldName in msg.fields) {
                        var fieldData = msg.fields[fieldName];
                        var fieldType = "???";

                        if (typeof fieldData === "number") {
                            fieldType = "NUMBER ";
                        }
                        else if (typeof fieldData === "string") {
                            fieldType = "STRING ";
                        }
                        else if (typeof fieldData === "boolean") {
                            fieldType = "BOOLEAN";
                        }
                        else if (typeof fieldData === "object") {
                            if (fieldData.constructor == (new Date).constructor) {
                                fieldType = "DATE   ";
                            }
                            else if (fieldData.constructor == (new Array).constructor) {
                                fieldType = "ARRAY  ";
                            }
                        }
                        console.log("  => %s : %s = " + fieldData, fieldType, fieldName);
                    }
                }

                if (_ackMode == "manual") {
                    sub.ackMessage(msg, function (err) {
                        if (_verbose) {
                            if (err) {
                                console.log("   -- Message ACK error: " + err);
                            }
                            else {
                                console.log("   -- Message ACK complete");
                            }
                        }
                    });
                }
            });

            console.log("Created %s subscription on %s", sub.qos, sub.topic);
            subscriptions.push(sub);
            topics.push(topic);

            if (++_subCreateCount == maxCount) {
                setTimeout(function () { mainLoop(session, subscriptions, topics); }, 10);
            }
        }
    });
}

function mainLoop(session, subscriptions, topics) {
    // Convert test duration to milliseconds
    _testDuration *= 1000;

    var startDate = new Date();
    var endDate = new Date(startDate.getTime() + _testDuration);

    console.log("Starting test run for %d seconds, will end at " + endDate.toLocaleTimeString(), _testDuration / 1000);
    setTimeout(function () { testComplete(session, subscriptions, topics); }, _testDuration);
}

function testComplete(session, subscriptions, topics) {
    var timeDiff = 0;
    if (_firstRecvDate && _lastRecvDate) {
        timeDiff = _lastRecvDate.getTime() - _firstRecvDate.getTime();
    }

    console.log("Test completed, shutting down...");
    _testingComplete = true;

    var subCount = 0;
    for (var i = 0; i < subscriptions.length; i++) {
        var sub = subscriptions[i];
        var topic = topics[i];
        if (sub) {
            console.log("Deleting subscription on " + topic);
            sub.stop(function (err) {
                subCount++;
                if (subCount == subscriptions.length) {
                    shutdownSession(session, timeDiff);
                }
            });
        }
        else {
            subCount++;
            if (subCount == subscriptions.length) {
                shutdownSession(session, timeDiff);
            }
        }
    }
}

function shutdownSession(session, timeDiff) {
    session.close(function (err) {
        var mps = (_totalRecvMessages / timeDiff) * 1000;
        console.log("Logout complete");
        console.log("Done.  Received a total of %d messages (%d MPS)", _totalRecvMessages, mps.toFixed(3));
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
    console.log("  --gdname=gd_client_name          (required for GD)");
    console.log("  --topic=topic[:topic]            (optional, default: TEST.BULK.*)");
    console.log("  --tcount=wc_topic_count          (optional, default: 1)");
    console.log("  --tstart=wc_topic_start          (optional, default: 0)");
    console.log("  --duration=test_duration_s       (optional, default: 30)");
    console.log("  --qos=( \"BE\" | \"GC\" | \"GD\" )     (optional, default: \"BE\")");
    console.log("  --subname=subscription_name      (required for GD)");
    console.log("  --ackmode=( \"auto\" | \"manual\" )  (optional for GD, default: \"auto\")");
    console.log("  --verbose                        (optional, default: false)");
}
