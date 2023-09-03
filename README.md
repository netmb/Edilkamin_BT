# Edilkamin_BT

This Arduino-ESP32 project allows to connect to Edilkamin pellet stoves via Bluetooth and automatically integrate it with Home Assistant. The cloud dependency is thus eliminated. 

I was able to reverse-engineer the Bluetooth protocol for the essential functions. This was not easy at the beginning, because the communication from cell phone to oven was encrypted via AES. Fortunately the necessary info was found in the source code of the app ;-)

I wrote the code for my Slide2 7-UP pellet stove, but actually all stoves from Edilkamin should work, which are controlled via "The mind".

If you want to try the code, you should include the code as Visual-Studio Code Platform-IO-Project via the Github URL. 

Afterwards you have to adjust the WLAN settings and the MQTT connection in the main.cpp. Don't forget to change the platform.io file and edit your ESP32 board here.

Once you have compiled the code and put it on your ESP32 board, the rest should work automatically. The ESP will search for the pellet stove via the corresponding Bluetooth characteristics and automatically add it to Home-Assistant via the MQTT discovery function.

## Limitations:

Currently only fan 1 can be changed, because my Slide2 does not have more fans. Some special functions like Chrono-timer and Pelletsensor are missing, cause i cant find the necessary codes for this.

I have no idea if this works on your pellet stoves right away, because I could only try it on my stove here. I am curious about the feedback...
