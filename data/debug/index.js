let wslog;
$(document).ready(function () {
    var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/ws";
    var ws = new WebSocket(wsURI);
    ws.onopen = function (evt) { console.log("WS:Connection open ..."); };
    ws.onmessage = function (evt) {
        //console.log("WS:Received Message: (%s) %s", evt.type, evt.data);
        try {
            a = JSON.parse(evt.data);
            if (a.type && a.type == "mem") {
                $("#memUsed").text(a.avg);
                $("#memMin").text(a.min);
                $("#memFrag").text(a.frag);
                $("#memLeak").text(a.leak);
            }
            if (a.type && a.type == "dds") {
                handleDDS(a);
            }
            if (a.memUsed) {
                $("#memUsed").text(a.memUsed);
            }
            if (a.debug) {
                $("#lastlog").prepend("<p>" + a.debug + "</p>");
            }
        } catch (e) {
            $("#lastlog").prepend("<p>" + evt.data + "</p>");
        };
    };
    ws.onclose = function (evt) { console.log("WS:Connection closed."); };
    ws.onerror = function (evt) { console.log("WS:WebSocket error : " + evt.data) };

});

function formatDuration(ms) {
    if (typeof ms !== 'number' || !isFinite(ms) || ms < 0) return "N/A";
    let s = Math.floor(ms / 1000);
    let d = Math.floor(s / 86400);
    s %= 86400;
    let h = Math.floor(s / 3600);
    s %= 3600;
    let m = Math.floor(s / 60);
    let sec = s % 60;
    let parts = [];
    if (d > 0) parts.push(d + " day" + (d > 1 ? "s" : ""));
    if (h > 0) parts.push(h + " hour" + (h > 1 ? "s" : ""));
    if (m > 0) parts.push(m + " minute" + (m > 1 ? "s" : ""));
    if (sec > 0) parts.push(sec + " seconde" + (sec > 1 ? "s" : ""));
    return parts.length > 0 ? parts.join(' ') : "0 secondes";
}

// Function to handle PowerMeter (DDS) logic
function handleDDS(data) {
    let pin = data.pin;
    // Target the specific meter container using ID
    let $meter = $("#meter-" + pin);

    // 1. Create structure if it doesn't exist (Lazy Load)
    if ($meter.length === 0) {
        let html = `
        <div id="meter-${pin}" class="meter-card">
            <h4>PowerMeter (Pin ${pin})</h4>
            <p>Cumulative: <span class="ddspc">--</span></p>
            <p>Tick: <span class="ddspt">--</span></p>
            <p>Last int time: <span class="ddslint">--</span></p>
            <p>Nb pulse: <span class="ddspls">--</span></p>
            <p>Last pulse duration: <span class="ddslplsd">--</span></p>
            <p>Last Tick Delta: <span class="ddsltd">--</span></p>
            <p>Last Tick time: <span class="ddsltt">--</span></p>
        </div>`;

        $("#metersContainer").append(html);

        // Re-select the newly created element
        $meter = $("#meter-" + pin);
    }

    // 2. Update values using classes within the specific container
    $meter.find(".ddspc").text(data.pc);
    $meter.find(".ddspt").text(data.pt);
    $meter.find(".ddslint").text(formatDuration(data.lint));
    $meter.find(".ddspls").text(data.pls);
    $meter.find(".ddslplsd").text(data.lplsd);
    $meter.find(".ddsltd").text(data.ltd);
    $meter.find(".ddsltt").text(formatDuration(data.ltt));
}