
$(document).ready(function () {
    var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/wifi";
    var ws = new WebSocket(wsURI);
    ws.onopen = function (evt) { console.log("WS:Connection open ..."); };
    ws.onmessage = function (evt) {
        //console.log("WS:Received Message: (%s) %s", evt.type, evt.data);
        try {

            var message = JSON.parse(evt.data);

            switch (message.evt) {
                case 0:
                    window.location = 'http://'+message.data;
                    break;
                case 1:
                    var items = [];
                    items.push("<option value=''>select WIFI</option>");
                    $.each(message.data, function (key, val) {
                        if (val) {
                            const values = val.split(" ", 1)
                            items.push("<option value='" + values[0] + "'>" + val + "</option>");
                        }
                    });
                    $("#lstSSID").html(
                        $("<select/>", {
                            html: items.join("")
                        })
                    );
                    $("#lstSSID > select").on("change", function () {
                        const ssid = $(this).children("option:selected").attr("value");
                        if ('' !== ssid) {
                            $("#ssid-name").val(ssid);
                        }
                    });
                    break;
            }
        } catch (e) {
            $("#lastlog").prepend("<p>" + evt.data + "</p>");
        };
    };
    ws.onclose = function (evt) { console.log("WS:Connection closed."); };
    ws.onerror = function (evt) { console.log("WS:WebSocket error : " + evt.data) };
    $("#lstSSID > input").on("click", function () {
        $.get("/wifi/scan")
            .done(function (data) {
                $("#lstSSID").html("Scanning...");
            })
            .fail(function () {
                alert("error");
            });
    });

    $("#btn-save-settings").on("click", function () {
        const formData = new FormData($("#form-wifi")[0]);
        
        $.ajax({
            type: "POST",
            url: "/wifi",
            data: JSON.stringify(Object.fromEntries(formData)),
            contentType: "application/json",
            dataType: "json",
            success: function (data, textStatus, jqXHR) {
                $("#btn-save-settings").disable();
            },
            error: function (data, textStatus, jqXHR) {
              alert(textStatus);
            }
        }
        );

    });
});