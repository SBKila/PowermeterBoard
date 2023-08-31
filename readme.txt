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
27/08/23 Change UI for save all and reboot (PM part).
28/08/23 Fix UI add remove PM issues ==> disable item in menu list PM reference
30/08/23 add wifi info in UI


@todo
=> Understanb how to manage connection to MQTT server full connected or connect on demand ?? 
=> next debug MQTT login+password when nothing is set
for now isSetup flag is unsed as work around
=> Think about moving adapter to PMBoard project => it is not HA lib
=> Add action warning in UI
=> Change UI for save all and reboot (MQTT part)
