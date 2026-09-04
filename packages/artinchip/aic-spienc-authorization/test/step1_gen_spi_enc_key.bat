@echo off

powershell -Command "[IO.File]::WriteAllBytes('spi_enc_key.bin', (1..16 | ForEach-Object { Get-Random -Minimum 0 -Maximum 256 }) -as [byte[]])"
:: powershell -Command "[IO.File]::WriteAllBytes('spi_enc_key.bin', (New-Object byte[] 16))"

