# Legal — uso, escopo e responsabilidade

## 1. Pesquisa no próprio aparelho

Este projeto foi desenvolvido e testado em uma TV **de propriedade do
pesquisador** (Roku H536X / Philco PTV24G5YR2CP). O objetivo é entender o
funcionamento do aparelho e documentar as falhas para que possam ser corrigidas.

O código é publicado para:

- revisão e reprodução por outros pesquisadores, **em hardware próprio**;
- defesa: entender o impacto real de CVE-2023-3079 (V8/Chromium 104) e
  CVE-2026-43499 (GhostLock, `rt_mutex`) em dispositivos embarcados;
- preservação da pesquisa, já que a cadeia depende de uma versão específica de
  firmware que deixa de existir com atualizações.

## 2. Autorização

Use **somente** em equipamento que você possui ou para o qual tem autorização
escrita do proprietário. Isso vale para a TV, para a rede e para qualquer
equipamento intermediário.

O projeto inclui um **MITM** que se passa por hosts de terceiros e usa uma
cadeia de certificados forjada. Apontar esse proxy para tráfego de outra pessoa,
de outra rede ou de outro aparelho é interceptação de comunicações e pode ser
crime, independentemente da intenção. A cadeia eMudhra/AAA é de terceiros e é
usada aqui apenas porque o firmware de teste a aceita.

## 3. Termos de uso e garantias

- O uso pode violar os **termos de serviço da Roku** (modificação de software,
  engenharia reversa, acesso a serviços). A avaliação desse risco é sua.
- Não há **nenhuma garantia**. O software é fornecido "como está" (licença MIT,
  `LICENSE`). Um endereço errado, uma escrita parcial em MTD, um update
  indesejado ou um reboot no meio do processo podem inutilizar a TV, apagar
  dados, derrubar a conta ou anular a garantia do fabricante.
- Você é responsável por backups (NVRAM, partições MTD) antes de experimentar.

## 4. O que este repositório não contém e não pode conter

- **Firmware** da Roku (imagens, boot, NAND, kernel ELF exato).
- **Código proprietário** da Roku, incluindo `roku_mfg_lib`, o app de
  fabricação (MFG), bibliotecas e binários assinados extraídos do aparelho.
- **Pacotes de canal** `.pkg` / `.jwt`, plugins descriptografados, conteúdo de
  terceiros (por exemplo, o screensaver).
- **Certificados, chaves, device keys**, material da cadeia eMudhra/AAA.
- **Serial, MAC, credenciais, tokens** ou qualquer identificador do aparelho e
  da rede do pesquisador. Os endereços nos exemplos são placeholders
  (`192.168.1.2`, `192.168.1.100`, `192.168.1.3`) e o usuário é `user`.

Se você encontrar qualquer item dessa lista no repositório, trate como erro e
avise os mantenedores.

## 5. Direito autoral e engenharia reversa

- O código original deste repositório é MIT. As **técnicas** documentadas vêm
  de pesquisa pública e da análise do próprio aparelho.
- Citações de trechos de terceiros são feitas para análise; não há
  redistribuição de obras completas.
- Em algumas jurisdições, contornar proteções técnicas pode ser tratado de
  forma específica (por exemplo, regras de anti-circumvention do DMCA nos EUA,
  ou equivalentes locais). Há exceções para pesquisa, interoperabilidade e
  segurança, mas elas variam. **Verifique a lei da sua jurisdição** antes de
  publicar, distribuir ou usar comercialmente.
- A licença MIT **não** concede direitos sobre firmware, marcas ou conteúdo de
  terceiros.

## 6. Divulgação responsável

- Falhas em produtos Roku devem ser reportadas pelo canal de divulgação
  responsável da Roku, não exploradas em aparelhos de terceiros.
- Se este repositório revelar algo que você acredita ser uma falha ainda não
  corrigida com impacto amplo, avise os mantenedores antes de divulgar
  detalhes que facilitem abuso em massa.
- O projeto não concede "safe harbor". Publicar código não autoriza usá-lo
  contra ninguém.

## 7. Segurança operacional

- As portas **2222** (shell PTY) e **2323** (comando/transferência) não têm
  autenticação. Mantenha a TV e o PC em rede confiável, sem exposição à
  internet, e desligue os agentes quando não estiver usando.
- O footprint do exploit (`randomize_va_space=0`, `modprobe_path` corrompido,
  futex presos) só é restaurado com reboot — e o reboot perde o root.
- Não aceite atualização de sistema durante a pesquisa. O Roku OS 16.0 remove
  a cadeia de certificados usada pelo foothold.

## 8. Contato e remoção

Pedidos de correção, remoção de conteúdo ou dúvidas legais devem ser abertos
como issue no repositório (sem anexar material proprietário) ou enviados em
privado aos mantenedores. Conteúdo de terceiros será removido quando
identificado.
