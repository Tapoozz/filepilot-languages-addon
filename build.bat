@echo off
rem Build FilePilotUniversal with MSYS2 UCRT64 gcc.
setlocal
set GCC=C:\msys64\ucrt64\bin\gcc.exe

echo [1/5] fphook.dll (bidi + Arabic shaping hook)
"%GCC%" -O2 -shared -o fphook.dll fphook.c fprtl.c || goto :err

echo [2/5] embed DLL -^> fphook_dll.h
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('fphook.dll'); $o=New-Object Text.StringBuilder; [void]$o.AppendLine('static const unsigned char FPH_DLL[] = {'); for($i=0;$i -lt $b.Length;$i++){[void]$o.Append(('0x{0:x2},' -f $b[$i])); if($i%%16 -eq 15){[void]$o.AppendLine()}}; [void]$o.AppendLine(''); [void]$o.AppendLine('};'); [void]$o.AppendLine('static const unsigned int FPH_DLL_LEN = '+$b.Length+';'); [IO.File]::WriteAllText('fphook_dll.h',$o.ToString())" || goto :err

echo [3/5] standalone tools (optional, for manual use)
"%GCC%" -O2 -municode -o fpglyphs.exe   fpglyphs.c   || goto :err
"%GCC%" -O2 -municode -o fpaddimport.exe fpaddimport.c || goto :err

echo [4/5] FilePilotUniversal.exe (the one-click patcher)
"%GCC%" -O2 -municode -o FilePilotUniversal.exe fppatch.c || goto :err

echo [5/5] transform self-test
"%GCC%" -O2 -DFPRTL_TEST -o fprtl_test.exe fprtl.c || goto :err

echo.
echo BUILD OK. Deliverable: FilePilotUniversal.exe (fphook.dll is embedded in it).
goto :eof
:err
echo.
echo BUILD FAILED
exit /b 1
