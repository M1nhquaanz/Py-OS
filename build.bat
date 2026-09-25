@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set PATH=C:\mingw32\bin;C:\qemu;%PATH%   

echo ========================================================
echo    PyOS Baremetal MicroPython GUI - Auto Build Tool
echo ========================================================

echo [0/6] Cleaning workspace and generating header files...
del /q *.o *.exe *.bin *.elf objs.txt 2>nul
if exist py (
    for %%f in (py\*.o) do del /q "%%f" 2>nul
)

python gen_root_pointers.py
if errorlevel 1 ( echo [ERROR] Failed to generate root pointers! & pause & exit /b 1 )

python gen_qstr.py
if errorlevel 1 ( echo [ERROR] Failed to generate QSTR! & pause & exit /b 1 )

echo [1/6] Compiling Bootloader...
i686-w64-mingw32-gcc -c boot.s -o boot.o -m32
if not exist boot.o ( echo [ERROR] Failed to compile boot.s & pause & exit /b 1 )

set MP_FLAGS=-I. -I./genhdr -I./py -std=gnu99 -ffreestanding -O2 -mno-sse -mno-mmx -m32 -fno-asynchronous-unwind-tables -fno-pie -fno-stack-protector -Wno-overflow -DNDEBUG -include mpconfigport.h -DMICROPY_CONFIG_FILE=\"mpconfigport.h\"

echo [2/6] Compiling MicroPython Core Engine...
if not exist py (
    echo [ERROR] Directory py\ does not exist!
    pause & exit /b 1
)

set PY_COUNT=0
for %%f in (py\*.c) do (
    set "CFILE=%%f"
    set "FNAME=%%~nxf"
    set "OFILE=py\%%~nf.o"

    if /i not "!FNAME!"=="scheduler.c" if /i not "!FNAME!"=="moductypes.c" (
        echo      Compiling !CFILE!...
        i686-w64-mingw32-gcc -c "!CFILE!" -o "!OFILE!" %MP_FLAGS%
        if errorlevel 1 (
            echo [ERROR] Compilation failed: !CFILE!
            pause & exit /b 1
        )
        set /a PY_COUNT+=1
    )
)
echo      [OK] Successfully compiled !PY_COUNT! C files in py\

if exist py\nlrx86.s (
    echo      Compiling py\nlrx86.s...
    i686-w64-mingw32-gcc -c py\nlrx86.s -o py\nlrx86.o -m32 %MP_FLAGS%
) else if exist py\nlrx86.S (
    echo      Compiling py\nlrx86.S...
    i686-w64-mingw32-gcc -c py\nlrx86.S -o py\nlrx86.o -m32 %MP_FLAGS%
)

echo [3/6] Compiling Kernel main and GUI Interface...
i686-w64-mingw32-gcc -c kernel.c -o kernel.o %MP_FLAGS%
if not exist kernel.o ( echo [ERROR] Failed to compile kernel.c & pause & exit /b 1 )

echo [4/6] Creating object files list (objs.txt)...
> objs.txt echo boot.o
>> objs.txt echo kernel.o
if exist py\nlrx86.o >> objs.txt echo py/nlrx86.o

for %%f in (py\*.o) do (
    if /i not "%%~nxf"=="nlrx86.o" (
        >> objs.txt echo py/%%~nxf
    )
)
echo [5/6] Linking to ELF/PE Image...
i686-w64-mingw32-gcc -T linker.ld -o myos.elf -m32 -ffreestanding -O2 -nostdlib @objs.txt -lgcc -Wl,--image-base,0x0 -Wl,--file-alignment,0x1000 -Wl,--section-alignment,0x1000

if not exist myos.elf ( echo [ERROR] Link error! & pause & exit /b 1 )

echo [5.5/6] Converting to Flat Binary...
objcopy -O binary -S myos.elf myos.bin
if not exist myos.bin ( echo [ERROR] Binary conversion failed! & pause & exit /b 1 )

echo [6/6] Launching QEMU Emulator...
qemu-system-i386 -kernel myos.bin -m 256

endlocal
pause