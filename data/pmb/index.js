var isSettingsDirty = false;
var pmSettings = [];

$("#setMQTT").on('pagebeforeshow', function() {
  $("#mqtt_url").val($("#mqtturl").val());
  $("#mqtt_port").val($("#mqtturl").val().substring($("#mqtturl").val().lastIndexOf(':')));
  $("#mqtt_login").val($("#mqttlogin").val());
  $("#mqtt_pwd").val($("#mqttpwd").val());
})

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
      $("#mqtturl").val("mqtt://"+formData.mqtt_url+":"+formData.mqtt_port);
      $("#mqttlogin").val(formData.mqtt_login);
      $("#mqttpwd").val(formData.mqtt_pwd);
      

      
      // $.ajax({
      //   type: "POST",
      //   url: "./mqtt",
      //   data: JSON.stringify(Object.fromEntries(formData)),
      //   contentType: "application/json",
      //   dataType: "json",
      //   success: function (data, textStatus, jqXHR) {
          
      //   },
      //   error: function (data, textStatus, jqXHR) {
      //     //process error msg
      //     $.mobile.changePage("#landing");
      //   }
      // });
    }
  });
});
let selectPmSelectInitialized=false;
$("#addPM").on("pageinit", function (event) {
  selectPmSelectInitialized=true;
})
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
        $("#createPMForm").trigger("reset")
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
      data.dIO = data.dIO.substring(2);
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
  $("#editpm_ticks").val($("#ticks" + pm).val());
  $("#editpm_nbtick").val($("#nbticks" + pm).val());
  $("#editpm_voltage")[0].selectedIndex = $("#editpm_voltage option[value=" + $("#voltage" + pm).val() + "]").index();
  $("#editpm_maxamp").val($("#maxamp" + pm).val());
  $("#editpm_value").val($("#cumulative" + pm).text());

  $("body").pagecontainer("change", "#editPM");
  $("select#editpm_voltage").selectmenu("refresh", true);

});
$('body').on('click', 'a[pmdeletetarget]', function () {
  var target = $(this).attr("pmdeletetarget");
  console.log("delete " + target);
  _removePMSettings(target);
});
$("#wifiexpbtn").on("click",()=>{
  $('#wifiexpbtn').buttonMarkup({ icon: "carat-"+($('#wifipanel').is(":visible")?"d":"u") });
  $('#wifipanel').toggle();
});
function _uploadSettings() {
  $("#btn-upload").button('disable');
}
function _updatePMSettings(data) {
  pmSettings = pmSettings.filter(element => (element.dIO === data.dIO) ? data : element);
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
  pmSettings = pmSettings.filter(element => element.dIO != dIO);
  isSettingsDirty = true;
  $("#btn-upload").button('enable');
  _removePowermeterDisplay(dIO);
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
  return '<h3> PM' + dioToPmReferenceIndex[element.dIO] + ': <span id="name' + element.dIO + '">' + element.name + '</span><span class="ui-li-count" id="cumulative' + element.dIO + '">' + element.cumulative + '</span></h3>' +
    '<form>' +
    '<div class="ui-grid-a">' +
    '<div class="ui-block-a">'+
    '<div class="ui-field-contain">'+
		'<label>tick:</label>'+
		'<input type="text" id="ticks' + element.dIO + '" value="' + element.ticks + '" disabled="disabled" data-mini="true" >'+
		'</div>'+
    '</div>'+
    '<div class="ui-block-b">'+
    '<div class="ui-field-contain">'+
		'<label>nbTicksByKw:</label>'+
		'<input type="text" id="nbticks' + element.dIO + '" value="' + element.nbTickByKW + '" disabled="disabled" data-mini="true" >'+
		'</div>'+
    '</div>'+
    '</div>' +
    '<div class="ui-grid-a">' +
    '<div class="ui-block-a">'+
    '<div class="ui-field-contain">'+
		'<label>voltage:</label>'+
		'<input type="text" id="voltage' + element.dIO + '" value="' + element.voltage + '" disabled="disabled" data-mini="true" >'+
		'</div>'+
    '</div>'+
    '<div class="ui-block-b">'+
    '<div class="ui-field-contain">'+
		'<label>maxAmp</label>'+
		'<input type="text" id="maxamp' + element.dIO + '" value="' + element.maxAmp + '" disabled="disabled" data-mini="true" >'+
		'</div>'+
    '</div>'+
    '</div>' +
    '<div>' +
    '<a href="#" pmedittarget="' + element.dIO + '" class="ui-btn ui-shadow ui-corner-all ui-icon-edit ui-btn-icon-notext ui-btn-inline">Edit</a>' +
    '<a href="#" pmdeletetarget="' + element.dIO + '" class="ui-btn ui-shadow ui-corner-all ui-icon-delete ui-btn-icon-notext ui-btn-inline">Delete</a>' +
    '</div>'+
    '</form>';
}

function _removePowermeterDisplay(dIO) {
  $("div[dio=" + dIO + "]").remove();
  $("#powermeterslist").collapsibleset('refresh', true);
  $("#createPMForm_dIO option[value='" + dIO + "']").removeAttr("disabled", false);
  if(selectPmSelectInitialized) 
  {
    $("select#createPMForm_dIO").selectmenu("refresh",true);
  }
}
function _replacePowermeterDisplay(element, index) {
  var item = $("div[dio=" + element.dIO + "]");
  var newItem = $('<div>')
    .attr({ 'data-role': 'collapsible', 'data-collapsed': 'true', 'dio': element.dIO })
    .html(_buildPowermeterDisplay(element));
  //$("div[dio=" + element.dIO + "]").html(_buildPowermeterDisplay(element));
  item.after(newItem);
  item.remove();
  $("#powermeterslist").collapsibleset('refresh');
}
function _appendNewPowermeterDisplay(element, index) {
  var newItem = $('<div>')
    .attr({ 'data-role': 'collapsible', 'data-collapsed': 'true', 'dio': element.dIO })
    .html(_buildPowermeterDisplay(element));

  // Sort by PM index
  var findNextItem = $("#powermeterslist").children("div[dio]").filter(
    function () {
      return dioToPmReferenceIndex[$(this).attr("dio")] > dioToPmReferenceIndex[element.dIO];
    });
  if (findNextItem.length != 0)
    $(findNextItem[0]).before(newItem);
  else
    $("#powermeterslist").append(newItem);

  // remove PM from option
  $("#createPMForm_dIO option[value='" + element.dIO + "']").prop("disabled", true);
  if(selectPmSelectInitialized) $("select#createPMForm_dIO").selectmenu("refresh",true);
  $("#powermeterslist").collapsibleset('refresh');
}
const dioToPmReferenceIndex = [1, 9, 10, 8, 2, 255, 255, 255, 255, 255, 255, 255, 5, 6, 4, 7, 3];
const pmReferenceIndexToDio = [255, /*PM1 D3*/0, /*PM2 D2*/4, /*PM3 D1*/16, /*PM4 D5*/14, /*PM5 D6*/12, /*PM6 D7*/13, /*PM7 D8*/15, /*PM8 D9*/3, /*PM9 D10*/1, /*PM10 D4*/2];


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
};
$("#landing").on("pageinit", function (event) {
  _downloadSettings();
})

$(document).ready(function () {
  var wsURI = ((window.location.protocol === "https:") ? "wss://" : "ws://") + window.location.host + "/pmb/ws";
  var ws = new WebSocket(wsURI);
  ws.onopen = function (evt) { console.log("WS:Connection open ..."); };
  ws.onmessage = function (evt) {
    var pmdEvent = JSON.parse(evt.data);
    console.log("WS:Received Message: " + evt.data);
    if (pmdEvent.type === "pdu") {
      pmdEvent.datas.forEach(function (element) {
        $("#cumulative" + element.dIO).html(element.cumulative);
        $("#ticks" + element.dIO).html(element.ticks);
      })
    }
    // MQTT connection status
    if (pmdEvent.type === "msc") {
      $("#mqttstatus").val(pmdEvent.datas ? "connected" : "disconnected");
    }
    if (pmdEvent.type === "wsc") {
      if(pmdEvent.datas.ssid) $("#ssid").val(pmdEvent.datas.ssid);
      if(pmdEvent.datas.ip) $("#ip").val(pmdEvent.datas.ip);
      if(pmdEvent.datas.rssi) $("#rssi").val(pmdEvent.datas.rssi);
      if(pmdEvent.datas.host) $("#host").val(pmdEvent.datas.host);
      if(pmdEvent.datas.bssid) $("#bssid").val(pmdEvent.datas.bssid);
      if(pmdEvent.datas.channel) $("#channel").val(pmdEvent.datas.channel);
      if(pmdEvent.datas.status) $("#status").val(wifiStatusToString[pmdEvent.datas.status]);
    }
  };
  const wifiStatusToString=["NO BOARD","IDLE","NO SSID AVAIL","SCAN COMPLETED","CONNECTED","CONNECT FAILED","CONNECTION LOST","WRONG PASSWORD","DISCONNECTED"];

  ws.onclose = function (evt) { console.log("WS:Connection closed."); };
  ws.onerror = function (evt) { console.log("WS:WebSocket error : " + evt.data) };
});