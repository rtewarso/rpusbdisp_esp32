This is a a usb lcd touchscreen for the esp32-s3 based off the rpusbdisp produced by robospeak 

Much of it was written by claude, so it doesn't work that well

The pin connections are listed in the code but essentially the lcd shares the spi bus with touchscreen

it uses the following hardware:
* https://www.amazon.com/Hosyond-Display-320x240-Compatible-Development/dp/B09XHRKFMM/ref=sr_1_2?
* https://www.amazon.com/Hosyond-Development-Dual-Mode-Compatible-ESP32-S3-WROOM-1/dp/B0F5QCK6X5/ref=sr_1_4?

cd <esp-idf>
. ./export.sh

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM1 flash
idf.py -p /dev/ttyACM1 monitor


TODO:
Support this:
* https://www.amazon.com/dp/B0FFGZTGYN?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1
