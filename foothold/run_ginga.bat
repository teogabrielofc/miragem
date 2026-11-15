@echo off
cd /d "%~dp0"
mitmweb --mode reverse:https://example.invalid@443 --mode reverse:http://example.invalid@80 --listen-host 192.168.1.2 --set keep_host_header=true --set connection_strategy=lazy --set upstream_cert=false --set dominio_alvo=roku.com,globo.com --emudhra --set http2=false -s .\proxy_subdominios.py -s .\ginga_serve.py
pause
