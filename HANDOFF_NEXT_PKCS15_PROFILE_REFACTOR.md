# Handoff: refatoração do profile PKCS#15 estático (próxima sessão, Debian)

> Escrito ao final de uma sessão de testes no Windows (08/08/2026). A tarefa
> descrita aqui é para ser feita **no Debian/Linux**, não no Windows — é
> puramente sobre a estrutura interna do driver (`src/libopensc/`), não tem
> nada de específico de plataforma. O ambiente Windows usado hoje foi só
> onde o trabalho aconteceu por acaso; o Linux é onde você tem o setup de
> desenvolvimento principal e mais confortável para isso.

---

## 1. O que já foi feito e já está no PR (não precisa refazer)

No dia de hoje, além de validar o driver StarSign inteiro no Windows
(build MSVC, PKCS#11, minidriver, autenticação real em TRT3/TJMG-PJe/e-Proc,
assinatura de PDF validada pelo ITI — tudo documentado em
`HANDOFF_WINDOWS_TESTING.md`), também respondemos a um review novo que o
**frankmorgner** (mantenedor do OpenSC) deixou no PR
[#3764](https://github.com/OpenSC/OpenSC/pull/3764). Desse review, **já
foram corrigidos e enviados** (commit `f0cd700e9` no branch
`DiegoRibeirodeSouza/OpenSC@feature/starsign-cut-s-driver`):

1. Cabeçalhos de licença LGPL 2.1 faltando em `card-starsign.c` e
   `pkcs15-starsign.c`.
2. `starsign_select_ef_child()` agora usa `iso_ops->process_fci()` (parser
   padrão de FCP do OpenSC) em vez de um parser manual que só lia o tamanho
   do arquivo. **Verificado contra hardware físico: saída do
   `pkcs15-tool -D` idêntica, byte a byte, à versão anterior**, para todos
   os certificados/chaves/PINs do token.
3. `starsign_set_security_env()` agora lê `key_ref`/`algorithm_ref` de
   `env` quando o chamador fornece essa informação (em vez de sempre usar
   os bytes fixos `01`/`02`), com fallback pros valores hardcoded
   (reverse-engineered) quando não fornecido. Verificado: nesse cartão, o
   profile atual nunca fornece essa informação, então o payload enviado ao
   cartão continua sendo exatamente `84 01 01 80 01 02` — comportamento
   idêntico ao original, só que agora "correto" estruturalmente.

Todas as 5 threads de comentário inline do review foram respondidas
individualmente no PR.

## 2. O que falta: a reestruturação maior (ainda não feita)

### 2.1. De onde vem esse pedido, exatamente

Comentário do **frankmorgner**, na thread de `pkcs15-starsign.c` linha 68
(citação literal, em inglês, do jeito que ele escreveu):

> "your code for path SELECTion looks very convoluted and I have the
> impression that this is due to the PKCS#15 profile supplying confusing
> information. One option would be to fix this path information in the
> profile so that special cases are avoided.
>
> But if the card's profile is so broken, it may be simpler as second
> option to just supply the card's static profile as OpenSC internal data.
> See `src/libopensc/pkcs15-esteid2025.c` for such an example. Metadata may
> be filled with what you have already gathered from the existing profile;
> paths should be chosen so that they can directly be selected, e.g. with
> the iso driver (note that you have the option to supply both, and AID
> *and* a (relative) path with sc_path_t. both are handled in e.g.
> `iso7816.c`)
>
> My main concern here is that the selection logic doesn't look like it
> can be maintained in the long run."

Ou seja: não é um bug, é uma preocupação de **manutenibilidade a longo
prazo** do código, vinda diretamente do mantenedor do projeto que vai
revisar (e eventualmente mergear) o PR.

### 2.2. Isso é necessário?

**Não é necessário para o driver funcionar.** Já validamos exaustivamente
hoje (Windows + hardware físico + TRT3/TJMG-PJe/e-Proc + PDF aprovado pelo
ITI) que o driver, do jeito que está agora, funciona corretamente de ponta
a ponta. Essa mudança é sobre **legibilidade/manutenção do código**, não
sobre corrigir um defeito funcional.

Dito isso, **é relevante para o PR ser aceito**: o frankmorgner deixou
claro que essa é a principal preocupação dele com o código no estado atual
("My main concern here..."). Ignorar completamente esse ponto é uma opção
válida (você pode argumentar que a lógica atual, embora tenha vários casos
especiais, é correta e testada), mas é bem provável que ele peça de novo
antes de aprovar o merge. Vale a pena fazer, mas não é bloqueante para
usar o driver no dia a dia — só para o PR upstream avançar mais rápido.

### 2.3. O que a mudança envolve, tecnicamente

Estudei o exemplo que ele indicou (`src/libopensc/pkcs15-esteid2025.c`,
264 linhas) até o fim. O padrão é: em vez de fazer
`sc_pkcs15_parse_df()` (que lê e decodifica o PrKDF/AODF/CDF *reais* do
cartão) e depois "consertar" os campos que vêm errados/faltando (que é o
que `starsign_parse_df()` faz hoje em `pkcs15-starsign.c`), você monta os
objetos PKCS#15 (certificados, PINs, chaves) **diretamente como dados
estáticos no código C**, chamando:

- `sc_pkcs15emu_add_x509_cert(p15card, &cert_obj, &cert_info)` — para cada
  certificado, com `cert_info.path` já setado pro path exato conhecido.
- `sc_pkcs15emu_add_pin_obj(p15card, &pin_obj, &pin_info)` — pro(s) PIN(s).
- Para chaves RSA (o exemplo usa `sc_pkcs15emu_add_ec_prkey` porque o
  EstEID 2025 é EC; **procurar o equivalente RSA**, provavelmente
  `sc_pkcs15emu_add_rsa_prkey` — conferir a assinatura exata em
  `pkcs15.h`/`pkcs15-syn.c` antes de usar).

**Importante — o que essa mudança NÃO resolve:** a lógica de seleção de
arquivo em `card-starsign.c` (`starsign_select_file`, o tratamento do
placeholder `0x3FFF`, o cache de `mid_fid`/`df5031_selected`) **continua
sendo necessária**. Isso é uma característica de como o *hardware do
cartão* endereça arquivos (confirmado por engenharia reversa contra uma
captura genuína do driver SafeSign proprietário) — não é um artefato de
como a gente lê os metadados PKCS#15. Trocar a fonte dos metadados (parse
dinâmico → estático) não muda como o SELECT FILE físico precisa ser feito
quando alguém efetivamente for ler um desses certificados/chaves.

Ou seja: essa mudança troca a parte de **descoberta/parsing** de objetos
(hoje em `pkcs15-starsign.c::starsign_parse_df`), não a parte de
**navegação/seleção física** (hoje em `card-starsign.c::starsign_select_file`).
As duas coisas são independentes.

### 2.4. Dados de referência já coletados (não precisa redescobrir)

Estes são os valores reais, confirmados contra o token físico
(`pkcs15-tool -D`, sessão de hoje) — usar exatamente estes ao montar o
profile estático:

**Token:**
- Serial: `001f00310028e510`
- Manufacturer ID: `A.E.T. Europe B.V.`

**PIN [User Pin]** (o único PIN que importa pra uso normal — "SO Pin" é
separado, `Reference: 1`):
- Auth ID: `01` (mas o `Reference` real da APDU VERIFY é `02` — ver nota
  abaixo)
- ID: `02`
- Reference: `2` (`0x02`)
- Type: ascii-numeric, min_len 4, max_len 15, stored_len 15, pad_char `0x00`
- Path: `3f00`

**Chave privada ATUAL (a que importa, a que tem certificado válido):**
- Label no cartão: vazio (`Private RSA Key []`)
- ID: `444945474f205249424549524f20444520534f555a4120323032342d31302d30392032303a32323a3235`
  (é a string ASCII `"DIEGO RIBEIRO DE SOUZA 2024-10-09 20:22:25"` em hex —
  esquema de ID não-padrão desse cartão/fabricante, mas é o valor real)
- Key ref: `1` (`0x01`)
- ModLength: 2048
- Usage: decrypt, sign, signRecover, unwrap (`0x2E`)
- Auth ID: `02` (liga a chave ao PIN acima)
- Native: yes

**Chave privada ANTIGA (órfã, sem certificado — 2021~2024, provavelmente
pode ser ignorada/omitida no profile estático já que não tem uso real):**
- Label: `DIEGO RIBEIRO DE SOUZA (18/10/2021 ~ 17/10/2024)`
- ID: `2cda5770ba19033f`
- Key ref: `0` (`0x00`) — **atenção:** isso aparece como `0x00` mesmo na
  baseline original (constatamos hoje que o patch
  `if (key_reference == 0x00) key_reference = 0x01` em
  `starsign_parse_df()` **não** está sendo aplicado a essa chave
  especificamente, só à chave atual. Não investigamos a fundo o motivo —
  como essa chave não tem certificado nem é usada, não bloqueou nada, mas
  vale entender por que antes de decidir se ela entra no profile estático
  ou se é seguro simplesmente omiti-la.

**Chave pública:**
- Label: `19602704`
- ID: `2cda5770ba19033f` (mesmo ID da chave privada antiga — não da atual)
- Key ref: `0` (`0x00`)
- Usage: encrypt, wrap, verify, verifyRecover (`0xD1`)

**Os 4 certificados** (paths no formato "virtual" com o placeholder
`0x3FFF` que o driver já trata):

| Label | Path | ID (hex) |
|---|---|---|
| DIEGO RIBEIRO DE SOUZA 2024-10-09 20:22:25 (o seu, o que importa) | `3f003fff43020114` | `444945474f205249424549524f20444520534f555a4120323032342d31302d30392032303a32323a3235` |
| AC OAB G3 emitido por AC Certisign G7 | `3f003fff430205a0` | `6a3bcf3abf73e203ae24cf9f02cd30f12ffe7e66` |
| AC Certisign G7 emitido por Autoridade Certificadora Raiz Brasileira v5 | `3f003fff430213ae` | `eb1046ea1980e7e0b2e95b072928085d17ad8e28` |
| Autoridade Certificadora Raiz Brasileira v5 (raiz, autoassinado) | `3f003fff43021371` | `4acadab14b74bf4fba7bace64b91801c44b8cc66` |

(dump completo salvo nesta sessão, caso precise de mais algum campo, mas os
acima cobrem o que o `pkcs15-esteid2025.c` usa como template)

### 2.5. Passo a passo sugerido

1. Ler `src/libopensc/pkcs15.h` e `src/libopensc/pkcs15-syn.c` pra
   confirmar a assinatura exata de `sc_pkcs15emu_add_rsa_prkey` (ou nome
   equivalente) — o exemplo do EstEID usa a variante EC, StarSign é RSA.
2. Escrever uma nova versão de `sc_pkcs15emu_starsign_init` em
   `pkcs15-starsign.c` que:
   - Seta `p15card->tokeninfo->label`, `manufacturer_id`, `serial_number`
     (pode ler do cartão como o exemplo faz, ou hardcode — decidir).
   - Adiciona o PIN via `sc_pkcs15emu_add_pin_obj`.
   - Adiciona os 4 certificados via `sc_pkcs15emu_add_x509_cert`, com os
     paths exatos da tabela acima.
   - Adiciona a chave privada atual via `sc_pkcs15emu_add_rsa_prkey`
     (ou equivalente), com `key_reference = 1`, `auth_id = 02`.
   - Decidir o que fazer com a chave órfã antiga e a chave pública (omitir?
     incluir do jeito que está?).
   - **Não** chamar `sc_pkcs15_bind_internal`/`sc_pkcs15_parse_df` — esse é
     justamente o ponto do padrão estático, pular o parsing dinâmico.
3. **Manter `starsign_select_file()` em `card-starsign.c` exatamente como
   está** (com a correção do `process_fci` já aplicada hoje) — ela ainda
   vai ser chamada quando o OpenSC precisar de fato ler o conteúdo de um
   desses arquivos (certificado, etc.), só que agora vai receber o path já
   pronto do profile estático em vez de um path descoberto dinamicamente.
4. Compilar (no Debian: `./bootstrap && ./configure && make`, processo bem
   mais simples que o Windows/MSVC de hoje).
5. **Testar contra o token físico, com muito cuidado — ver seção 3
   abaixo.**

## 3. Metodologia de teste (lição aprendida hoje)

Durante a sessão de hoje, tivemos um alarme falso: uma mudança pareceu ter
quebrado a leitura de uma chave, gastamos um tempo revertendo e
investigando, e no fim descobrimos que o comportamento "estranho" já
existia no código original — não era regressão nenhuma. A lição prática:

- **Sempre tire um dump `pkcs15-tool -D` completo ANTES de mexer em
  qualquer coisa**, e salve em arquivo. É a sua baseline de verdade.
- Depois de cada mudança, tire o dump de novo e **compare com `diff`**
  (ou `Compare-Object` no PowerShell) contra a baseline — não confie na
  memória do que "parecia" antes.
- Só depois de confirmar que a leitura completa bate 100% com a baseline,
  parta pro teste de assinatura real (`pkcs11-tool --sign --id <hex> ...`,
  isolado, não o `--test` completo que mistura múltiplas chaves).
- Só depois de leitura + assinatura confirmadas, considere seguro fazer
  commit/push.

## 4. Consequências de não fazer essa mudança

Se você decidir **não** fazer essa reestruturação:
- O driver continua funcionando perfeitamente (já provado extensivamente).
- O PR provavelmente fica em `CHANGES_REQUESTED` até esse ponto ser
  resolvido de alguma forma — seja com essa refatoração, seja convencendo
  o frankmorgner de que a abordagem atual é aceitável.
- Você pode optar por continuar usando o driver localmente (compilado da
  sua branch) mesmo sem o PR ser mergeado — não há urgência técnica, só
  urgência de "fechar o PR direito".

## 5. Links de referência

- PR: https://github.com/OpenSC/OpenSC/pull/3764
- Thread do comentário específico: procurar por "very convoluted" nos
  comentários de review de `pkcs15-starsign.c`
- Exemplo a seguir: `src/libopensc/pkcs15-esteid2025.c` (já lido/entendido
  nesta sessão, resumo na seção 2.3 acima)
- Commit que já foi enviado hoje com as correções menores:
  `f0cd700e9` no branch `feature/starsign-cut-s-driver`
