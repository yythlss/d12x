@echo off

..\..\..\..\tools\scripts\spienctool.exe genauthkey --seedver 3 --chip d12x --spienckey spi_enc_key.bin --outseed sign.key --outverifykey verify.key

xxd.exe -c 16 -i sign.key > d12x_hwauth.h
xxd.exe -c 16 -i verify.key >> d12x_hwauth.h
del sign.key
del verify.key

