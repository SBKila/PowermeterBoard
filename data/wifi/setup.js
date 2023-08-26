
$(document).ready(function () {
    $.getJSON("/wifi/scan")
        .done(function (data) {
            var items = [];
            items.push("<option value=''>select WIFI</option>");
            $.each(data, function (key, val) {
                if(val){
                    const values = val.split(" ",1)
                    items.push("<option value='" + values[0] + "'>" + val + "</option>");
                }
            });
            $("#lstSSID").html(
                $("<select/>", {
                    html: items.join("")
                })
            );
            $("#lstSSID > select").on("change", function(){
                const ssid = $(this).children("option:selected").attr("value");
                if('' !== ssid) {
                    $("#ssid-name").val(ssid);
                }
            });
        })
        .fail(function () {
            alert("error");
        });
    $("#btn-save-settings").on("click", function () {
        const formData = new FormData($("#form-wifi")[0]);
      
        $.ajax({
            type: "POST",
            url: "/wifi",
            data:  JSON.stringify(Object.fromEntries(formData)),
            contentType: "application/json",
            dataType: "json",
            success: function(data, textStatus, jqXHR) {
               //process data
               //$.mobile.changePage( "#landing");
            },
            error: function(data, textStatus, jqXHR) {
               //process error msg
               //$.mobile.changePage( "#landing");
            }
          }
        );        
        
    });
});