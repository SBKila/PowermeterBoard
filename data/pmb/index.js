
$("#addPM").on("pageinit", function () {
  $("#createPMForm").validate({
    rules: {
      pm_ref: {
        required: true,
      },
      pm_name: {
        required: true,

      },
      pm_nbtick: {
        required: true,
        number: true,
        regex: "[0-9]*"
      },
      pm_voltage: {
        number: true,
      },
      pm_maxamp: {
        number: true,
      }
    },
    errorPlacement: function (error, element) {
      error.insertAfter(element.parent());
    },
    submitHandler: function (form) {
      const formData = new FormData(form);

      $.ajax({
        type: "POST",
        url: "./pm",
        data: JSON.stringify(Object.fromEntries(formData)),
        contentType: "application/json",
        dataType: "json",
        success: function (data, textStatus, jqXHR) {
          appendNewPowermeter(data);
          $("#powermeterslist").collapsibleset('refresh');
          $.mobile.changePage("#landing");
        },
        error: function (data, textStatus, jqXHR) {
          //process error msg
          $.mobile.changePage("#landing");
        }
      }
      );
    }
  }
  );
});

jQuery.validator.addMethod(
  "regex",
  function (value, element, regexp) {
    if (regexp.constructor != RegExp)
      regexp = new RegExp(regexp);
    else if (regexp.global)
      regexp.lastIndex = 0;
    return this.optional(element) || regexp.test(value);
  }, "erreur expression reguliere"
);

function appendNewPowermeter(element){
  var newItem = $('<div>')
          .attr({ 'data-role': 'collapsible', 'data-collapsed': 'true' })
          .html('<h3>' + element.name + '<span class="ui-li-count" id="cumulative'+element.dIO+'">'+element.cumulative+'</span></h3>')
        $("#powermeterslist").append(newItem);
}

$(document).ready(function () {

  $.ajax({
    type: "GET",
    url: "./pm",
    success: function (data, textStatus, jqXHR) {
      //process data
      console.log("get powermeters ok");
      data.forEach(element => appendNewPowermeter(element));
      $("#powermeterslist").collapsibleset('refresh');
    },
    error: function (data, textStatus, jqXHR) {
      //process error msg
      console.log("get powermeters KO");
    }
  });

  var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/pmb/ws";
  var ws = new WebSocket(wsURI);


  ws.onopen = function (evt) { console.log("Connection open ..."); };
  ws.onmessage = function (evt) { 
    console.log("Received Message: " + evt.data);
    JSON.parse(evt.data).forEach(function(element) {
        $("#cumulative"+element.dIO).html(element.cumulative);
    } ) 
  };
  ws.onclose = function (evt) { console.log("Connection closed."); };
  ws.onerror = function (evt) { console.log("WebSocket error : " + evt.data) };


});