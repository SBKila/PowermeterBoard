11/08/2023 :
=> Adding setupDialog to setup MQTT serveur :  Done
=> Display powermeters in UI : Done
=> Add add PowerMeter Settings : Done
Adding dynamic crash on addComponent -> onHaConnect Done by delay the OnHaConnect in next loop using newComponent flag
12-13/08/23:
    Fix DIO in UI is it esp pin number or PM index done
    ===>> Fix issue when insert dynamic component Works now but not understand why  <<==
14/08/23
Next: Add Edit PowerMeter Settings

20/08/23 Updating HAAdapterDDS238 to merge with thingspeak release
21/08/23 Correctly manage Update cumulative VS Full AdapterDDS238
Think about moving adapter to PMBoard project => it is not HA lib


@todo
=> Understanb how to manage connection to MQTT server full connected or connect on demand ?? 
=> next debug MQTT login+password when nothing is set
for now isSetup flag is unsed as work around
=> 
