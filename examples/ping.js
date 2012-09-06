/**
 * ping.js
 *
 * This application send and receives messages on a PING topic, timing the total
 * round-trip latency for each message.
 *
 * Usage:
 *   node ping.js --tmx=tmx[:tmx] [options]
 *      --user=username             (optional, default: tervela)
 *      --pass=password             (optional, default: tva123ma1)
 *      --topic=topic               (optional, default: PING)
 *      --tmx=tmx[:tmx]             (required)
 *      --count=message_count       (optional, default: 100)
 *      --delay=inter_msg_gap_ms    (optional, default: 10)
 */

// Set NODE_PATH environment variable to include location of tervela.node file
var tervela = require("tervela");

process.on('uncaughtException', function (err) {
    console.error(err.stack);
});

process.on('exit', function () {
    console.log("Done.");
});

var username = "tervela";
var password = "tva123ma1";
var topic = "PING";
var primaryTmx = "";
var secondaryTmx = "";
var name = "";
var count = 100;
var delay = 10;
var verbose = false;
var exit = false;

// Process command line arguments
var args = process.argv.splice(2);
args.forEach(function (val, index, array) {
    var keyval = val.split("=");
    if (keyval.length == 2) {
        if (keyval[0] == "--user") {
            username = keyval[1];
        }
        else if (keyval[0] == "--pass") {
            password = keyval[1];
        }
        else if (keyval[0] == "--topic") {
            topic = keyval[1];
        }
        else if (keyval[0] == "--count") {
            count = keyval[1];
        }
        else if (keyval[0] == "--delay") {
            delay = keyval[1];
        }
        else if (keyval[0] == "--name") {
            name = keyval[1];
        }
        else if (keyval[0] == "--tmx") {
            var ps = keyval[1].split(":");
            primaryTmx = ps[0];
            if (ps.length > 1) {
                secondaryTmx = ps[1];
            }
        }
    }
    else if (keyval.length == 1) {
        if (keyval[0] == "--verbose") {
            verbose = true;
        }
        else if (keyval[0] == "--help") {
            exit = true;
        }
    }
});

if ((!exit) && (primaryTmx.length == 0)) {
    console.log("Missing TMX");
    exit = true;
}

if (exit) {
    printUsage();
}
else {
    console.log("Running with options:");
    console.log("  username     : " + username);
    console.log("  password     : " + password);
    console.log("  primaryTmx   : " + primaryTmx);
    console.log("  secondaryTmx : " + secondaryTmx);
    console.log("  topic        : " + topic);
    console.log("  count        : " + count);
    console.log("  delay        : " + delay);
    console.log("  verbose      : " + verbose);
    console.log("");

    var latencies = new Array();
    var txcount = 0;
    var rxcount = 0;
    var testingComplete = false;
    var startTime, endTime;

    // Login to the TMX/TMXs
    console.log("Connecting to %s...", (secondaryTmx) ? "TMXs" : "TMX");
    tervela.connect({
        username: username,
        password: password,
        tmx: [ primaryTmx, secondaryTmx ],
        name: name
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
            });

        console.log("Connected");

        var subscription, publication;

        // Create subscription and publication
        subscription = session.createSubscriptionSync(topic, { qos: 'BE' });
        if (typeof subscription === "string") {
            console.log("Error creating subscription to " + topic + ": " + subscription);
            setTimeout(function () { shutdown(session); }, 10);
            return;
        }
        console.log("Start subscription complete");

        publication = session.createPublicationSync(topic);
        if (typeof publication === "string") {
            console.log("Error creating publication to " + topic + ": " + publication);
            setTimeout(function () { shutdown(session); }, 10);
            return;
        }
        console.log("Start publication complete");

        // Set up event listener for message events
        subscription.on('message', function (msg) {
            try {
                var lat = msg.fields["time"];
                var now = new Date();
                var diff = now.getTime() - lat.getTime();
                latencies.push(diff);

                if (verbose) {
                    console.log("RX: %d : %d %d (%d)", rxcount, now, lat, diff);
                }

                rxcount++;
                if (rxcount == count) {
                    testComplete(session);
                }
            }
            catch (e) {
                console.log("messageEvent: Caught exception: " + e.message);
                console.log(e.stack);
            }
        });

        // Start
        startTime = new Date();
        setTimeout(function () { sendMessage(publication); }, delay);
    });
}

function sendMessage(publication) {
    try {
        var message = {
            time: new Date()
        };

        if (verbose) {
            console.log("TX: %d : %d", txcount, message["time"].getTime());
        }

        publication.sendMessage(topic, message, { selfdescribe: true}, function (err) {
            if (err) {
                console.log("-- Error sending message: " + err);
            }
        });

        txcount++;
        if (txcount < count) {
            setTimeout(function () { sendMessage(publication); }, delay);
        }
        else {
            console.log("Done sending messages");
        }
    }
    catch (e) {
        console.log("sendMessage: Caught exception: " + e.message);
        console.log(e.stack);
    }
}

function testComplete(session) {
    endTime = new Date();
    var duration = endTime.getTime() - startTime.getTime();
    
    var total = 0;
    latencies.sort(function (a,b) { return (a - b); });    
    latencies.forEach(function (val, index, array) {
        total += val;
    });
    
    var min = latencies[0];
    var max = latencies[latencies.length - 1];
    var mean = total / latencies.length;
    
    console.log("\n*** Latency results (ms):");
    console.log("* Count:%d  min:%d  max:%d  mean:%d  runtime:%d", latencies.length, min, max, mean, duration);
    console.log(" ");

    shutdown(session);
    testingComplete = true;
}

function shutdown(session) {
    // Closing the session also closes the publication and subscription
    session.close(function (err) { console.log("Session close complete"); });
}

function printUsage() {
    console.log("Usage: node ping.js --tmx=tmx[:tmx] [options]");
    console.log("  --user=username                  (optional, default: tervela)");
    console.log("  --pass=password                  (optional, default: tva123ma1)");
    console.log("  --topic=topic                    (optional, default: PING)");
    console.log("  --count=message_count            (optional, default: 100)");
    console.log("  --delay=inter_msg_gap_ms         (optional, default: 10)");
    console.log("  --verbose                        (optional, default: false)");
}
