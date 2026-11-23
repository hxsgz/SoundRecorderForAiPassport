@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0..\.buildtools\host-build" mkdir "%~dp0..\.buildtools\host-build"
cd /d "%~dp0..\.buildtools\host-build"
set vad=..\..\vendor\webrtcvad\cbits
set spl=%vad%\webrtc\common_audio\signal_processing
set core=%vad%\webrtc\common_audio\vad
cl /nologo /std:c11 "..\..\tests\check_clip_buffer.c" /Fe:check_clip_buffer.exe
if errorlevel 1 exit /b 1
check_clip_buffer.exe
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /I"%vad%" "..\..\firmware\main\gate.c" "..\..\tests\gate_host.c" "%core%\vad_core.c" "%core%\vad_filterbank.c" "%core%\vad_gmm.c" "%core%\vad_sp.c" "%spl%\division_operations.c" "%spl%\energy.c" "%spl%\get_scaling_square.c" "%spl%\resample_48khz.c" "%spl%\resample_fractional.c" "%spl%\resample_by_2_internal.c" /link /export:gate_frame /export:test_gate_size /export:test_reset_votes /export:test_continuing /out:gate.dll


