# DLNA no USB Media Player (canal 2213) — por sessão

O app **USB Media Player (2213)** tem o stack DLNA/SSDP/UPnP inteiro no pacote
(`components/Services/DLNA/*`), mas ele é distribuído numa variante **USB-only**:
o manifesto traz `usbonly=1` e o
`components/CustomTasks/ConfigReader/ConfigReader.brs` desliga:

```brightscript
if m.manifest["usbonly"] = "1"
    configObj.AddSet("enableHTTPFS", false)
    configObj.AddSet("enableDLNA", false)
    configObj.AddSet("romOnly", true)
endif
```

Com isso `useDLNA()`/`isUSBOnly()` viram false/true e a UI nunca mostra
servidores de mídia. Não é uma configuração de usuário nem um registry: é o
manifesto do pacote.

> O token do 2213 lista também o id **20255** — provavelmente a variante
> "Roku Media Player" com DLNA. A loja escolhe a variante por aparelho/conta;
> TVs costumam receber a USB-only.

## Ligar (root ativo, porta 2222)

```sh
ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh apply
ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh status
ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh restore

# ou pelo helper/gerenciador (mesma operacao, kill seletivo):
ROKU_HOST=192.168.1.100 bash tools/zipapp.sh dlna apply
curl -s -X POST http://127.0.0.1:8090/api/dlna/apply      # na TV
```

O `apply`:

1. descompacta `/plugins/2213.zip` em `/tmp/rmp_dlna` (ou
   `/tmp/miragem/dlna` no helper do gerenciador);
2. troca `usbonly=1` → `0`;
3. faz `mount --bind` do manifesto sobre `pkg:/manifest` do 2213;
4. faz **cold start só do app**: `SIGKILL` no `SdkLauncher` com cgroup
   `memory:/plugins/<pid>` (o launcher spare de boot tem `memory:/` e fica
   vivo) e relança o 2213 pelo ECP.

O passo 4 é obrigatório: com `sdk_instant_resume=1`, o app volta de
instant-resume sem reler o manifesto. **Nunca** mate todos os `SdkLauncher`:
o watchdog da TV reinicia o aparelho.

## Gerenciador

Em *Ajustes → Superroot → Sessão (runtime)*, a linha **DLNA (USB Media
Player)** mostra o estado (`usbonly`, bind e app ativo) e tem **Ligar** /
**Desligar**. As rotas são `GET /api/dlna`, `POST /api/dlna/apply` e
`POST /api/dlna/restore`; o gerenciador chama
`/tmp/miragem/dlna_apply.sh` (instalado por `tools/install_manager.sh`).
O `tools/zipapp.sh dlna status|apply|restore` faz o mesmo pelo PC.
Como o ECP recusa loopback neste build, o cold start usa o relay do PC
(`tools/ecp_relay.py`, `<ip-do-pc>:8092` em `/tmp/miragem/ecp_relay`).

## Assinatura — por que isso funciona

O manifesto **está dentro** do RPAK e é coberto pela assinatura
(Merkle/RSA). O truque **não** burla a assinatura:

- o pacote em `/plugins/2213.zip` continua **intocado e assinado**;
- o SO monta/valida o pacote como sempre;
- depois disso, um **bind mount** troca só a visão de `pkg:/manifest` para o app;
- o app lê o arquivo no cold start e não revalida arquivo por arquivo.

Ou seja: é um **override de VFS em runtime**, não uma quebra criptográfica.
Se você repackar o `.zip` e instalar, a assinatura falha. É por isso que
`restore`/reboot desfazem: nada foi gravado no pacote.

## Limites

- **Vale por boot/sessão.** Reboot, remount do plugin ou restart do
  `SdkLauncher` podem reverter; rode `apply` de novo.
- Precisa de **root** (o bind e o kill do `SdkLauncher`).
- O `apply` fecha e relança o app (cold start); o `restore` volta a USB-only.
- Se o **ZIP app** estiver aplicado, o bind do manifesto fica sob o `pkg:`
  do ZIP; aplicar/restaurar o ZIP remove esse bind (o `umount` do `pkg:`
  falharia com `EBUSY`). Reaplique o DLNA depois, se necessário.
- O kill é **seletivo**: só o `SdkLauncher` do app (`memory:/plugins/<pid>`);
  matar todos derruba a TV por watchdog.
- O caminho “limpo” continua sendo a variante de loja com DLNA (20255/account).
- Uso só na **sua** TV; não redistribua o pacote da Roku.
