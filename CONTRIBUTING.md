# Contribuindo

O Miragem é pesquisa em hardware próprio. Contribuições úteis, na ordem:

1. **Port de kernel** (o que mais destrava gente): rode o GhostLock em outro
   modelo/build e envie os offsets. Guia pronto para agente de IA em
   [`docs/PORTING.md`](docs/PORTING.md); registre em `kernels.json` e abra o PR.
2. **Binário de payload por alvo** (`payload/<id>/`): veja
   [`payload/README.md`](payload/README.md) — compile com
   `HOST_IP="255.255.255.255"` para o site personalizar por IP.
3. **Site/instalador**: melhorar textos, acessibilidade do controle remoto,
   traduções (PT/EN/ES), o gerador de pacote.
4. **Documentação**: corrigir instruções que não bateram no seu aparelho.

## Antes do PR

```sh
npm run check     # build do gerador USB + build do site + checagens
node tools/test_installer.mjs dist/installer.html
```

O CI (`.github/workflows/check.yml`) roda o mesmo em cada PR.

## Regras do repositório

- **Nada de firmware da Roku** (`update.roku`, `.pkg`, `.jwt`, dumps, chaves).
  `.gitignore` bloqueia; não force.
- Sem IPs reais, seriais, MACs, tokens ou credenciais no código/docs. Use
  `192.168.1.2` (PC) e `192.168.1.100` (TV) como exemplo.
- Binários só os construídos a partir deste repositório (`payload/`,
  `tools/miragemd.arm`).
- Documente o que testou e o que **não** testou; “funciona” sem evidência não
  entra.

## Commit / PR

- Commits curtos e no imperativo (`Docs: ...`, `Installer: ...`).
- Descreva: modelo/OS/kernel, o que mudou, como testou (com log), o que ficou
  pendente.
- PR de kernel: use o template e preencha a tabela de offsets.

## Segurança

Vulnerabilidades ou achados sensíveis: veja [`SECURITY.md`](SECURITY.md).
Não abra issue pública com dados do seu aparelho.
