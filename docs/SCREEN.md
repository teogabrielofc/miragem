# Tela no PC (mirror, tipo scrcpy)

`tools/screen.sh` mostra a **UI da TV** (`/dev/fb1`) numa janela do PC, sem
áudio. É o mesmo princípio do scrcpy, sem encoder de vídeo: a TV envia apenas
os tiles 64×64 que mudaram e o PC monta o frame localmente.

Testado no **H536X / PTC24G5** com **15.0.4.5537** (fb1 1280×720, 32 bpp,
stride 5120, double buffer de 1440 linhas). Serve para qualquer Roku com root e
framebuffer compatível; o `fbserve` avisa se o formato não for 16/32 bpp.

## Uso

```sh
# janela ao vivo (Ctrl-C encerra)
ROKU_HOST=192.168.1.100 bash tools/screen.sh

# screenshot (PNG) sem abrir janela
ROKU_HOST=192.168.1.100 bash tools/screen.sh shot tela.png

# forçar Tk (sem ffplay) e ampliar 2x
ROKU_HOST=192.168.1.100 bash tools/screen.sh start --tk --scale 2

# encerrar o servidor na TV (caso algo fique rodando)
ROKU_HOST=192.168.1.100 bash tools/screen.sh stop
```

Requisitos:

- **shell root** na TV (porta 2222) e o agente de upload (2323) — os mesmos do
  instalador/gerenciador (`tools/install_manager.sh` sobe os dois);
- no PC, `ffplay` (pacote `ffmpeg`) **ou** `python3-tk` (Tk). Com PIL a janela
  fica mais rápida; sem PIL o Tk usa PPM.
- o binário vai para `/tmp/fbserve` a cada uso (some no reboot). A porta padrão
  é **2331** (`SCREEN_PORT`), a taxa **10 fps** (`SCREEN_FPS`).

## Build do binário ARM

```sh
TC=~/tc/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf
$TC-gcc -O2 -static -no-pie -s -o tools/fbserve.arm tools/fbserve.c
```

O `tools/fbserve.arm` já vai versionado no repo; o build só é necessário se
mexer no `fbserve.c`.

## Como funciona

1. `screen.sh` envia `fbserve.arm` e o inicia na TV:
   `fbserve -d /dev/fb1 -p 2331 -f 10`.
2. `fbserve` abre `/dev/fb1`, faz `mmap` e lê só a área visível
   (`yoffset` do double buffer do compositor).
3. Compara com o frame anterior e manda **somente os tiles 64×64 alterados**
   (cabeçalho `FBS2`, little-endian; detalhes no topo de `tools/fbserve.c`).
4. `tools/tvscreen.py` recebe, aplica os tiles num frame local e mostra via
   `ffplay` (rawvideo BGRA/RGB565) ou Tk; `--shot` salva PNG.
5. Sem mudança na tela, **não trafega nada** (0 B/s parado).

Medição no H536X: tela parada = 0 B/s; navegando no Home = ~0,7–2,3 MB por
frame alterado e ~6 fps efetivos na Wi-Fi. É suficiente para acompanhar o
controle remoto; não é vídeo em 30/60 fps nem transmissão de vídeo.

## Limites conhecidos

- **Só a UI (fb1).** O plano de vídeo (`/dev/fb0`) não é capturado: conteúdo de
  vídeo (apps de streaming, HDMI, canais) aparece preto/estático no espelho.
  Para conteúdo DRM isso é intencional.
- **Sem áudio** (por escolha; o protocolo é só de imagem).
- **Sem controle remoto pelo PC** — para teclas use ECP:
  `curl -X POST http://PC:8092/keypress/Home` (relay do gerenciador) ou
  `POST http://TV:8060/keypress/Home` de outro host.
- **Root necessário** só para subir o binário e ler o framebuffer; o app em si
  roda como root em `/tmp/fbserve`.
- A porta 2331 **não tem autenticação**: qualquer um na LAN que conecte vê a
  tela. Use `screen.sh stop` quando terminar ou restrinja no firewall.
- Resolução/formato: 16 ou 32 bpp; se o fb tiver outra profundidade o
  `fbserve` recusa e mostra o motivo no `/tmp/fbserve.log`.

## Problemas comuns

| sintoma | causa/providência |
|---|---|
| `shell root fechado em ...:2222` | rode o instalador/root (a TV perdeu o root no boot) |
| `upload (2323) indisponivel` | `tools/install_manager.sh` ou reabra o instalador (agente `ra6`) |
| `ffplay nao abriu a janela (sem DISPLAY?)` | rode no desktop ou use `--tk` |
| `bind: Address in use` | outro `fbserve` vivo: `screen.sh stop` e tente de novo |
| janela preta | tela realmente preta (HDMI/entrada de vídeo) ou fb0; abra o Home |
| cores trocadas | fb não é BGRX; confira `fbserve.log` (bpp/stride) |
