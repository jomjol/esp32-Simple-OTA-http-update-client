# esp32-simple-OTA-http-update-client

This is an easy HTTP web client to implement firmware update over the air (OTA):



<img src="https://raw.githubusercontent.com/jomjol/esp32-Simple-OTA-http-update-client/main/images/web_client_image.jpg">



It creates a very simple homepage, on which you can select a firmware `firmware.bin` you have just compiled locally. Then you push the "Upload and Reboot" button and it is transferred to the ESP32, flashed and the ESP32 reboots immediately.

This is very useful, if you do not have physical access to the ESP32 or if it is included in a housing, that make access to the USB-port or chip interface difficult.



The OTA code including the web client is capsuled in a single library: `jomjol_SimpleOTAviaHTTP`



There are only a view prerequisites to consider:

##### 1. Make sure you have implemented connection to the internet in your code 

You can use the include library `jomjol_connect_wlan` (and `jomjol_helper` for support)



##### 2. Implement the library in your project

Copy the library `jomjol_SimpleOTAviaHTTP` into `lib`



##### 3. Start the server in you main routine:

```#include "server_ota_http.h"
#include "server_ota_http.h"

...

extern "C" void app_main() 
{
    CheckOTAUpdate();
    server = start_webserver();   
    register_server_main_uri(server);
    
    ...
}
```



##### 4. Update the `platformio.ini`

Create a custom `partition.csv` in the root directory, that has the otadata, ota_1 and ota_2 partitions needed for the inline update:

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x4000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
ota_0,    app,  ota_0,   ,        1900k,
ota_1,    app,  ota_1,   ,        1900k,
```

Here a rather big firmware of up to 1900k is possible. You can adapt this to your need an available memory of your device.



##### 5. Update the `platformio.ini`

```
board_build.partitions = partition.csv

lib_deps = 
	# any other libraries you need plus: 
	jomjol_SimpleOTAviaHTTP 

board_build.embed_files  =
  lib/jomjol_SimpleOTAviaHTTP/upload_ota.html
```



Start the ESP32 and enjoy the update via the following link:

`http://IP-ESP32/ota`





### You find a very simple example here. This can be compiled with esp-idf in platformio environment.