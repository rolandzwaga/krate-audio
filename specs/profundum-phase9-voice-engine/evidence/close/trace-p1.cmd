@echo off
rem Profundum Phase 9 T032: 100 s CPU trace of the full P-1 probe against the symbolised exe (run from an ELEVATED cmd;
rem any current directory). Output: evidence\run-log\close\prof\p1-full.etl (~2 GB) and p1-full-exe.log.
cd /d F:\projects\iterum
set OUT=specs\profundum-phase9-voice-engine\evidence\run-log\close\prof
wpr -cancel >nul 2>&1
wpr -start CPU -filemode || (echo wpr -start FAILED & exit /b 1)
echo recording... the probe runs in the background, its output goes to %OUT%\p1-full-exe.log
start /b "" build\windows-x64-prof\bin\Release\dsp_systems_tests.exe VoiceEngine_CpuProbe > %OUT%\p1-full-exe.log 2>&1
timeout /t 100 /nobreak >nul
wpr -stop %OUT%\p1-full.etl
if errorlevel 1 (echo wpr -stop FAILED & exit /b 1)
dir %OUT%\p1-full.etl | find "p1-full.etl"
echo TRACE DONE - the probe keeps running; you can close this window.
