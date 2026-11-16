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
```

O `apply`:

1. descompacta `/plugins/2213.zip` em `/tmp/rmp_dlna`;
2. troca `usbonly=1` → `0`;
3. faz `mount --bind /tmp/rmp_dlna/manifest` sobre `pkg:/manifest` do 2213;
4. reinicia o `SdkLauncher` (cold start) e relança o 2213.

O passo 4 é obrigatório: com `sdk_instant_resume=1`, o app volta de
instant-resume sem reler o manifesto.

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
- O caminho “limpo” continua sendo a variante de loja com DLNA (20255/account).
- Uso só na **sua** TV; não redistribua o pacote da Roku.
