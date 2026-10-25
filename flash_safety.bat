@echo off
rem Dublu-click: trimite Safety\Debug\Safety.elf prin bootloader pe USART2.
rem Argumentele se transmit mai departe (ex: flash_safety.bat --port COM3).
setlocal
cd /d "%~dp0"

rem Primul Python care are pyserial (Python 3.15 de pe acest PC are alt pachet "serial")
for %%P in ("py -3.12" "py -3" "python") do (
    %%~P -c "import serial.tools.list_ports" >nul 2>&1 && (
        %%~P flash_safety.py %*
        goto :done
    )
)
echo EROARE: nu gasesc un Python cu pyserial. Instaleaza-l cu:
echo     py -3.12 -m pip install pyserial

:done
echo.
pause
