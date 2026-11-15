@echo off
setlocal EnableExtensions
title Roku Root
color 0A

rem ------------------------------------------------------------------
rem  Windows launcher: mitmweb + Linux servers + root shell (tvsh)
rem  Fill in the placeholders below. See windows/README.md.
rem ------------------------------------------------------------------
set "LINUXUSER=user"
set "LINUXIP=192.168.1.2"
set "TV=192.168.1.100"
set "WINIP=192.168.1.3"
set "KEY=%USERPROFILE%\.ssh\roku_root"
set "MITM=C:\Program Files\mitmproxy\bin\mitmweb.exe"
set "MITMDIR=%~dp0"
set "SSHOPT=-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=6 -o ServerAliveInterval=15"

echo ============================================================
echo   ROKU ROOT - servidores + shell root
echo ============================================================
echo.

rem ---------- 1) mitmweb ----------
tasklist /fi "imagename eq mitmweb.exe" 2>nul | find /i "mitmweb.exe" >nul
if not errorlevel 1 goto mitmok

echo [1/3] iniciando mitmweb...
pushd "%MITMDIR%"
start "mitmproxy" /min "%MITM%" --mode reverse:https://example.invalid@443 --mode reverse:http://example.invalid@80 --listen-host %WINIP% --set keep_host_header=true --set connection_strategy=lazy --set upstream_cert=false --set dominio_alvo=roku.com,globo.com --emudhra --set http2=false --no-web-open-browser -s proxy_subdominios.py -s ginga_serve.py
popd
echo       aguardando o proxy subir...
ping -n 5 127.0.0.1 >nul
goto servers

:mitmok
echo [1/3] mitmweb ja esta rodando

:servers
echo [2/3] preparando os servidores no Linux (%LINUXUSER%@%LINUXIP%)...
echo.
ssh %SSHOPT% -i "%KEY%" %LINUXUSER%@%LINUXIP% "ROKU_HOST=%TV% bash /tmp/opencode/start_servers.sh"
if errorlevel 1 goto noLinux
echo.
if /i "%~1"=="test" goto selftest

echo [3/3] abrindo o shell root na TV...
echo       Na TV: entrada Antena TV, pagina GINGA LAB, aperte OK no controle.
echo       Para sair do shell: Ctrl-] ou feche esta janela.
echo.
ssh -t %SSHOPT% -i "%KEY%" %LINUXUSER%@%LINUXIP% "ROKU_HOST=%TV% ROKU_WAIT=300 tvsh"
echo.
echo Sessao encerrada.
pause
exit /b 0

:selftest
echo [teste] servidores OK - shell nao aberto (modo test).
pause
exit /b 0

:noLinux
echo.
echo ERRO: nao consegui falar com o Linux %LINUXIP%.
echo Confira se o PC Linux esta ligado e na mesma rede.
echo.
pause
exit /b 1
