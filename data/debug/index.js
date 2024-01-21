let wslog;
$(document).ready(function () {
    var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/ws";
    var ws = new WebSocket(wsURI);
    ws.onopen = function (evt) { console.log("WS:Connection open ..."); };
    ws.onmessage = function (evt) {
        //console.log("WS:Received Message: (%s) %s", evt.type, evt.data);
        try {
            a = JSON.parse(evt.data);
            if(a.memUsed){
                $("#memUsed").text(a.memUsed);
            }
            if(a.debug){
                $("#lastlog").prepend( "<p>"+a.debug+"</p>" );
            }
        } catch (e) {
            $("#lastlog").prepend( "<p>"+evt.data+"</p>" );
        };
    };
    ws.onclose = function (evt) { console.log("WS:Connection closed."); };
    ws.onerror = function (evt) { console.log("WS:WebSocket error : " + evt.data) };

});