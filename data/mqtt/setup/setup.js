
$(document).ready(function () {
    $("#btn-save-settings").on("click", function () {
        const formData = new FormData($("#form-mqtt")[0]);
      
        $.ajax({
            type: "POST",
            url: "/mqtt",
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