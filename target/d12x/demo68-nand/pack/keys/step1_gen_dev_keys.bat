@echo off

powershell -Command "[IO.File]::WriteAllBytes('spi_enc_key.bin', (1..16 | ForEach-Object { Get-Random -Minimum 0 -Maximum 256 }) -as [byte[]])"
:: powershell -Command "[IO.File]::WriteAllBytes('spi_enc_key.bin', (New-Object byte[] 16))"
powershell -Command "[IO.File]::WriteAllBytes('spl_chacha20_key.bin', (1..32 | ForEach-Object { Get-Random -Minimum 0 -Maximum 256 }) -as [byte[]])"
powershell -Command "[IO.File]::WriteAllBytes('app_chacha20_key.bin', (1..32 | ForEach-Object { Get-Random -Minimum 0 -Maximum 256 }) -as [byte[]])"

