@echo off

echo Make spi_aes_key.h from spi_enc_key.bin

copy spi_enc_key.bin spi_aes.key
..\..\..\..\tools\scripts\xxd.exe -c 16 -i spi_aes.key > spi_aes_key.h

..\..\..\..\tools\scripts\cat.exe spi_aes.key > all.key
..\..\..\..\tools\scripts\crc32.exe spi_aes.key > crc32.txt
..\..\..\..\tools\scripts\sed.exe -i 's/([0-9]*)$/spi_aes.key/g' crc32.txt
..\..\..\..\tools\scripts\crc32.exe all.key >> crc32.txt
..\..\..\..\tools\scripts\sed.exe -i 's/([0-9]*)$/all.key/' crc32.txt

del spi_aes.key

copy spi_aes_key.h ..\..\..\..\bsp\examples_bare\test-efuse\spi_aes_key.h
