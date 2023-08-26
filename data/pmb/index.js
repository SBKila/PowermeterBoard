var isSettingsDirty = false;
var pmSettings = [];

$("#setMQTT").on("pagecreate", function () {
  $("#setMQTTForm").validate({
    rules: {
      mqtt_url: {
        required: true,
      },
      mqtt_port: {
        required: true,
      }
    },
    errorPlacement: function (error, element) {
      error.insertAfter(element.parent());
    },
    submitHandler: function (form) {
      const formData = new FormData(form);

      $.ajax({
        type: "POST",
        url: "./mqtt",
        data: JSON.stringify(Object.fromEntries(formData)),
        contentType: "application/json",
        dataType: "json",
        success: function (data, textStatus, jqXHR) {
          _appendNewPowermeterDisplay(data);
          $("#powermeterslist").collapsibleset('refresh');
          $.mobile.changePage("#landing");
        },
        error: function (data, textStatus, jqXHR) {
          //process error msg
          $.mobile.changePage("#landing");
        }
      });
    }
  });
});

$("#addPM").on("pagecreate", function () {
  $("#createPMForm").validate({
    rules: {
      dIO: {
        required: true,
      },
      name: {
        required: true,
      },
      nbTickByKW: {
        required: true,
        number: true,
        regex: "[0-9]*"
      },
      voltage: {
        number: true,
      },
      maxAmp: {
        number: true,
      },
      cumulative: {
        number: true,
      }
    },
    errorPlacement: function (error, element) {
      error.insertAfter(element.parent());
    },
    submitHandler: function (form) {
      if ($("#createPMForm").valid()) {
        const formData = new FormData(form);
        var data = {};
        for (var pair of formData.entries()) {
          data[pair[0]] = pair[1];
        }
        _addPMSettings(data);
        $.mobile.changePage("#landing");
      }

      // $.ajax({
      //   type: "PUT",
      //   url: "./pm",
      //   data: JSON.stringify(Object.fromEntries(formData)),
      //   contentType: "application/json",
      //   dataType: "json",
      //   success: function (data, textStatus, jqXHR) {
      //     appendNewPowermeter(data);
      //     $("#powermeterslist").collapsibleset('refresh');
      //     $.mobile.changePage("#landing");
      //   },
      //   error: function (data, textStatus, jqXHR) {
      //     //process error msg
      //     $.mobile.changePage("#landing");
      //   }
      // }
      // );
    }
  }
  );
});
$("#editPM").on("pagecreate", function () {
  $("#editPMForm").validate({
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
      },
      pm_value: {
        number: true,
      }
    },
    errorPlacement: function (error, element) {
      error.insertAfter(element.parent());
    },
    submitHandler: function (form) {
      const formData = new FormData(form);
      var data = {};
      for (var pair of formData.entries()) {
        data[pair[0]] = pair[1];
      }
      data.dIO=data.dIO.substring(2);
      _updatePMSettings(data);
      $.mobile.changePage("#landing");
      // $.ajax({
      //   type: "POST",
      //   url: "./pm",
      //   data: JSON.stringify(Object.fromEntries(formData)),
      //   contentType: "application/json",
      //   dataType: "json",
      //   success: function (data, textStatus, jqXHR) {
      //     updatePowermeter(data);
      //     $("#powermeterslist").collapsibleset('refresh');
      //     $.mobile.changePage("#landing");
      //   },
      //   error: function (data, textStatus, jqXHR) {
      //     //process error msg
      //     $.mobile.changePage("#landing");
      //   }
      // }
      // );
    }
  }
  );
});

$("#btn-reload").on("click", () => {
  _downloadSettings();
});
$("#btn-upload").on("click", () => {
  _uploadSettings();
});

$('body').on('click', 'a[pmedittarget]', function () {
  var pm = $(this).attr("pmedittarget");
  $("#editpm_ref").val("PM" + pm);
  $("#editpm_name").val($("#name" + pm).text());
  $("#editpm_ticks").val($("#ticks" + pm).text());
  $("#editpm_nbtick").val($("#nbticks" + pm).text());
  $("#editpm_voltage")[0].selectedIndex = $("#editpm_voltage option[value=" + $("#voltage" + pm).text() + "]").index();
  $("#editpm_maxamp").val($("#maxamp" + pm).text());
  $("#editpm_cumulative").val($("#cumulative" + pm).text());

  $("body").pagecontainer("change", "#editPM");
  $("#editpm_voltage").selectmenu();
  $("#editpm_voltage").selectmenu("refresh", true);

});
$('body').on('click', 'a[pmdeletetarget]', function () {
  var target = $(this).attr("pmdeletetarget");
  console.log("delete " + target);
  _removePMSettings(target);
});
function _uploadSettings(){
  $("#btn-upload").button('disable');
}
function _updatePMSettings(data) {
  pmSettings = pmSettings.filter(element => (element.dIO===data.dIO)?data:element);
  isSettingsDirty = true;
  $("#btn-upload").button('enable');
  _replacePowermeterDisplay(data);
}
function _addPMSettings(data) {
  pmSettings.push(data);
  $("#btn-upload").button('enable');  
  isSettingsDirty = true;
  _appendNewPowermeterDisplay(data);

}
function _removePMSettings(dIO) {
  pmSettings = pmSettings.filter(element => element.dIO!=dIO);
  isSettingsDirty = true;
  $("#btn-upload").button('enable');  
  $("div[dio="+dIO+"]").remove();
  $("#powermeterslist").collapsibleset('refresh');
}

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
function _buildPowermeterDisplay(element, index) {
  return '<h3> PM' + dioToPmReference[element.dIO] + ': <span id="name' + element.dIO + '">' + element.name + '</span><span class="ui-li-count" id="cumulative' + element.dIO + '">' + element.cumulative + '</span></h3>' +
      '<div class="ui-grid-a">' +
      '<div class="ui-block-a">tick:<span id="ticks' + element.dIO + '">' + element.ticks + '</span></div>' +
      '<div class="ui-block-b">nbTicksByKw:<span id="nbticks' + element.dIO + '">' + element.nbTickByKW + '</div>' +
      '</div>' +
      '<div class="ui-grid-a">' +
      '<div class="ui-block-a">voltage:<span id="voltage' + element.dIO + '">' + element.voltage + '</div>' +
      '<div class="ui-block-b">maxAmp:<span id="maxamp' + element.dIO + '">' + element.maxAmp + '</div>' +
      '</div>' +
      '<div>' +
      '<a href="#" pmedittarget="' + element.dIO + '" class="ui-btn ui-shadow ui-corner-all ui-icon-edit ui-btn-icon-notext ui-btn-inline">Edit</a>' +
      '<a href="#" pmdeletetarget="' + element.dIO + '" class="ui-btn ui-shadow ui-corner-all ui-icon-delete ui-btn-icon-notext ui-btn-inline">Delete</a>' +
      '</div>';
}

function _replacePowermeterDisplay(element, index) {
  $("div[dio="+element.dIO+"]").html(_buildPowermeterDisplay(element));
    $("#powermeterslist").collapsibleset('refresh',true);
}
function _appendNewPowermeterDisplay(element, index) {
  var newItem = $('<div>')
  .attr({ 'data-role': 'collapsible', 'data-collapsed': 'true', 'dio':element.dIO})
  .html(_buildPowermeterDisplay(element));
  $("#powermeterslist").append(newItem);
  $("#createPMForm_dIO option[value='" + dioToPmReference[element.dIO] + "']").prop("disabled", true);
    $("#powermeterslist").collapsibleset('refresh');
}
var dioToPmReference = [1, 9, 10, 8, 2, 3, 255, 255, 255, 255, 255, 255, 5, 6, 4, 7];

function _downloadSettings() {
  var data = [
    { "dIO": "0", "name": "PAC", "nbTickByKW": "2000", "voltage": "230", "maxAmp": "16", "cumulative": "553", "ticks": "1" },
    { "dIO": "2", "name": "Garage", "nbTickByKW": "1000", "voltage": "240", "maxAmp": "16", "cumulative": "7542", "ticks": "120" }
  ];
  console.log("get powermeters ok");
  
  $("#btn-upload").button('disable');  
  
  isSettingsDirty = false;
  pmSettings = data;
  $("#powermeterslist").empty();
  data.forEach(_appendNewPowermeterDisplay);
  $("#powermeterslist").collapsibleset('refresh');
  // $.ajax({
  //   type: "GET",
  //   url: "./pm",
  //   success: function (data, textStatus, jqXHR) {
  //     //process data
         // isSettingsDirty = false;
         // pmSettings = data;
         // $("#powermeterslist").empty();
         // data.forEach(appendNewPowermeter);
         // $("#powermeterslist").collapsibleset('refresh');
  //   },
  //   error: function (data, textStatus, jqXHR) {
  //     //process error msg
  //     console.log("get powermeters KO");
  //   }
  // });
}
jQuery( "#landing" ).on( "pageinit", function( event ) { 
  _downloadSettings();
} )
$(document).ready(function () {
  var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/pmb/ws";
  var ws = new WebSocket(wsURI);
  ws.onopen = function (evt) { console.log("WS:Connection open ..."); };
  ws.onmessage = function (evt) {
    //var pmdEvent = JSON.parse(evt.data);
    var pmdEvent = evt;
    console.log("WS:Received Message: " + pmdEvent.type);
    if (pmdEvent.type === "pdu") {
      pmdEvent.datas.forEach(function (element) {
        $("#cumulative" + element.dIO).html(element.cumulative);
        $("#ticks" + element.dIO).html(element.ticks);
      })
    }
    // MQTT connection status
    if (pmdEvent.type === "msc") {
      $("#HA_status").html(pmdEvent.datas ? "connected" : "disconnected");

    }
  };
  ws.onclose = function (evt) { console.log("WS:Connection closed."); };
  ws.onerror = function (evt) { console.log("WS:WebSocket error : " + evt.data) };
});