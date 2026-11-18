# HANDOFF — SharkInfo

Documento de passagem para quem continuar o desenvolvimento (pessoa ou sessão do Claude Code). Leia inteiro antes de mexer: ele explica **o que existe, por que foi feito assim, o que já foi provado no hardware real e o que ainda não foi**.

---

## 1. O que é

O SharkInfo é um app de bandeja do Windows que mostra a bateria de mouses sem fio **Attack Shark** (receptor USB 2.4G) e avisa antes de o mouse descarregar no meio do jogo. Ele foi inspirado no post do krisgfx (Wireless Device Battery Monitor) e ganhou funções extras.

- **Linguagem:** C++17 puro com a API do Windows: Win32, HID, GDI+, WinHTTP e BCrypt. **Nenhuma biblioteca externa.**
- **Tamanho:** o `.exe` tem ~470 KB, é estático e só usa DLLs do sistema.
- **Público:** o próprio dono (Pedro, GitHub `Pedr030`), com um Attack Shark **X6/R1**. Os amigos podem usar também.
- **Repositório:** `github.com/Pedr030/sharkinfo` (público, porque o atualizador lê as releases sem login).

### Histórico das decisões

- Começou como protótipo em **Python** (hidapi + pystray + matplotlib). O protótipo foi abandonado e **não está no repositório**.
- Foi refeito em **C++** pelo tamanho (KB em vez de dezenas de MB), pelo uso de memória e pelo acesso direto ao Windows.
- Nomes que passaram: SharkBateria → Barbatana → **SharkInfo** (o atual, escolhido pelo dono).
- O ícone da bandeja já foi um "quadrado colorido com número". O dono preferiu **um mouse em contorno com um selo colorido no canto** (estilo badge de notificação). O modo número ficou como opção.
- A % escrita na barra de tarefas (widget) é **o padrão**, por escolha do dono.

---

## 2. Estado atual (18/11/2026)

### Linha do tempo das versões (todas publicadas em 18/11/2026)

| Versão | O que entrou |
|---|---|
| v1.0.0 | Primeira release. Tudo o que está nas seções 4 e 5 abaixo. |
| v1.0.1 | Tirou o chute inicial do tempo restante (dava "48 h"); a descarga passou a ir de um cabo ao próximo e buracos (mouse dormindo, PC desligado) não zeram a conta. Serviu também para provar o ciclo do atualizador. |
| v1.0.2 | **Hub** (clique esquerdo), **polling rate medido** e o piso "dura pelo menos X". |
| v1.0.3 | Tempo restante mostra "calculando…" com o piso pequeno ("Garantido: ≥ X"); textos do hub encurtados; escuta de polling mais sensível e com contador de avisos no cartão. |

### Provado no PC do dono

- O receptor Xenta foi detectado, com o mouse identificado como **"Attack Shark X6 / R1"** (código de modelo `0x10`, que manda o nível **em dezenas**, 0..10).
- O ícone com selo verde aparece na barra, a dica (tooltip) mostra "Attack Shark X6 / R1: 80%" e a janela do histórico abre e desenha.
- O widget aparece na barra de tarefas, à esquerda da área de notificação.
- **O hub abre e desenha** (print do dono em 18/11/2026): barra lateral, mouse grande com selo, bateria 80%, cartões de tempo restante, polling, carga e avisos. O dono achou o visual "bem interessante". Defeitos vistos no print e corrigidos na v1.0.3: subtítulos cortados com "…" e pílulas do polling coladas no texto.
- **O atualizador chegou na v1.0.2 no PC do dono** (o `SharkInfo.exe` em `Downloads` estava na versão 1.0.2 sem ele baixar de novo). Falta só confirmar com ele que foi pelo atualizador e não por download manual.
- O mouse estava na divisa 80 ↔ 90 (≈ 85%) quando o histórico foi lido; ficou em 80 de 19:27 até a hora do print (mais de 1 h sem cair para 70). É por isso que o tempo restante demora: o X6/R1 só informa de 10 em 10%.

### Problemas vistos no hardware real e já corrigidos (falta confirmar no PC)

- **A % pulava 80 → 90 → 80.** A tensão da bateria se recupera em repouso e, com degraus de 10%, o valor fica oscilando na divisa entre dois degraus. Foi resolvido com o `suavizador.h` (seção 5.3).
- **"Tempo restante: calculando…" para sempre.** A regra antiga exigia 20 min e 2% de queda, o que num mouse de 10 em 10% demora horas. Foi trocada pelos "marcos de queda", com aprendizado das descargas anteriores (seção 5.4). Na v1.0.1 o chute inicial (`horas_bateria_padrao`, que dava "48 h") foi removido: sem dados, mostra "calculando…".

### Ainda NÃO verificado no Windows real

Ninguém rodou estas partes num PC de verdade. Teste antes de considerar prontas:

1. **Texto "80%" do widget.** No único print, a dica cobria o widget. Confirmar que o texto aparece e fica alinhado.
2. **Atualizador.** Ver "Provado" acima: provável, falta a confirmação do dono.
3. **Faixa por cima do jogo, notificações com ícone (`NIIF_USER`), envio pro ntfy, estimativa de carga e receptor Rongyuan.** Nada disso foi exercitado no hardware.
4. **Build com MSVC/Visual Studio pelo `CMakeLists.txt`.** Nunca foi compilado. O CI usa MinGW.

### Pendências abertas (fim da sessão de 18/11/2026)

1. **Polling rate NÃO funcionou no PC do dono (v1.0.2).** O cartão ficou em "—" mesmo mexendo o mouse. A conta passa nos testes (`TestarPolling`, tempos simulados), então o problema está na escuta do Windows (`polling.cpp`) ou nos limiares. Na v1.0.3 a janela de escuta virou uma janela comum oculta (antes era "só de mensagens"), os limiares ficaram mais baixos (25 avisos / 100 ms por trecho, 2 trechos) e o cartão mostra **"Medindo… (N avisos)"**. **Primeiro passo na próxima sessão:** pedir o print do cartão. Se aparecer "Mexa o mouse pra medir" mesmo mexendo, o Windows não está entregando `WM_INPUT` (problema de escuta); se aparecer "Medindo… (N avisos)" com N subindo e o Hz nunca fecha, o problema são os limiares ou o formato dos avisos. Um teste isolado em PowerShell rodou sem eventos, mas a sessão do Claude Code não tem desktop interativo, então não vale como prova.
   - O dono lembrou que no software oficial da Attack Shark **o polling é uma opção que ele escolhe**, então a informação existe no mouse e dá para ler direto. Ver "Pesquisa do polling rate" na seção 5.1. O protocolo não é público; falta descobri-lo.
   - Ideia ainda não feita: separar o histórico de descarga por polling rate, porque taxa mais alta gasta mais bateria.
2. **Toggle "Número no ícone da bandeja" não funciona, segundo o dono.** Não consegui reproduzir. O código parece certo: `ExecutarComando(CMD_NUMERO)` inverte `gCfg.iconeNumero`, grava o `config.ini` e chama `AtualizarIcone`, que passa o flag para `DesenharIcone`. O `config.ini` do dono estava com `icone_numero=0` na hora. Hipóteses a testar, em ordem: (a) o dono olha o **widget** da barra (que não muda) e não o ícone da bandeja, que no Windows 11 fica escondido no "^"; (b) o ícone em modo número é desenhado mas fica ilegível ou transparente em 16 a 20 px (só o modo mouse foi visto em produção; os testes só conferem que o ícone não é nulo); (c) o clique do hub não chega (ver se os outros toggles do hub funcionam). **Perguntar ao dono o que ele vê** (menu ou hub, ícone na bandeja ou widget) antes de mexer.
3. **Tempo restante:** o dono estranhou o "≥ 7 h 21 min" que só subia. É correto (um piso que ganha confiança), mas parecia uma contagem ao contrário, então na v1.0.3 virou "calculando…" com "Garantido: ≥ X" pequeno. Continua sem existir tempo restante de verdade até a primeira queda de 10%.
4. **Hub:** o visual passou na primeira olhada do dono. Falta ver as outras abas (Histórico, Avisos, Ajustes, Sobre) com print.

### Versões antigas

Os `.exe` mandados antes do GitHub (Python, "Barbatana" e "SharkInfo" sem versão) são `0.0.0-dev` ou anteriores e **não se atualizam sozinhos**. O dono precisa baixar a v1.0.0 manualmente uma vez. A partir dela, o atualizador assume.

---

## 3. Como compilar, testar e publicar

```
./build-mingw.sh                 # versão 0.0.0-dev (atualizador desligado)
VERSAO=1.2.0 ./build-mingw.sh    # versão de release (o CI chama assim)
```

- O script gera `build/SharkInfo.exe` e `build/testes.exe`. Precisa de `g++-mingw-w64-x86-64`. No Ubuntu: `apt install g++-mingw-w64-x86-64`.
- **Atenção:** com `VERSAO`, o script **reescreve `src/versao.h`**. Não faça commit desse arquivo alterado: no repositório ele fica sempre `0.0.0-dev`.
- Os testes (`tests/testes.cpp`) precisam de Windows. Numa sessão Linux dá para compilá-los, mas não rodar: o Wine não funcionou no ambiente onde o app foi feito. Quem roda os testes é o CI, num `windows-latest` de verdade.
- Para testar só a lógica no Linux, dá para compilar `historico.cpp` e `suavizador.h` nativamente com um "shim" do `windows.h`. Foi assim que a lógica foi validada durante o desenvolvimento.

### Workflows

- **`.github/workflows/ci.yml`** (push na `main` e PRs): compila no Ubuntu com MinGW e roda `testes.exe` no Windows.
- **`.github/workflows/release.yml`** (tag `v*`): compila com a versão da tag, gera o `SharkInfo.exe.sha256`, roda os testes no Windows e publica a release com as notas tiradas do `CHANGELOG.md`.

### Publicar uma versão (fluxo usado da v1.0.1 à v1.0.3, igual ao do repositório sinal-app)

O repositório tem duas branches fixas: **`development`** e **`main`**. Nada vai direto para a `main` (só a primeira carga do repositório foi assim).

1. Crie uma branch de trabalho a partir de `origin/development` (`feat/...`, `fix/...` ou `release/vX.Y.Z`).
2. Adicione a seção `## vX.Y.Z` no topo do `CHANGELOG.md`, com as mudanças em português. Esse texto vira as notas da release e aparece na janela de atualização do app.
3. Commit em Conventional Commits em português, **sem linha de coautoria/IA**, e `git push -u origin <branch>`.
4. Abra um PR da branch para `development` (`gh pr create --base development ...`) e espere o CI passar (`gh pr checks <n> --watch`). Merge com **merge commit** (`gh pr merge <n> --merge`), como no sinal-app.
5. Abra um PR de `development` para `main` ("release: vX.Y.Z"), espere o CI e faça o merge.
6. Atualize a `main` local (`git switch main && git pull --ff-only`) e rode `git tag vX.Y.Z && git push origin vX.Y.Z`. A tag dispara o workflow **Release**, que publica a release com `SharkInfo.exe` e `SharkInfo.exe.sha256`.
7. Confira com `gh release view vX.Y.Z` que os dois anexos estão lá.

**Não renomeie os anexos.** O atualizador procura exatamente `SharkInfo.exe` e `SharkInfo.exe.sha256` e espera a tag com prefixo `v`.

Dicas que custaram tempo:

- No PowerShell, `gh pr create --body "..."` com aspas e acentos quebra os argumentos. Use `--body-file arquivo.md`. Mensagem de commit longa: `git commit -F arquivo.txt` (o here-string `@'...'@` com `-F -` não funciona no PowerShell 5.1).
- O workflow Release **travou uma vez** (v1.0.1) no passo "Instalar MinGW-w64" (o `apt`), por mais de 14 minutos; o normal é cerca de 1,5 minuto. Solução: `gh run cancel <id>` e depois `gh run rerun <id>`. Se a release não aparecer em `gh release list`, olhe `gh run list --workflow Release` antes de achar que a tag falhou.
- **Não existe compilador na máquina do dono** (o Visual Studio 18 instalado não tem o componente C++ e não há MinGW). Quem compila é o CI; um erro de compilação só aparece depois do push. O CI leva cerca de 1 minuto e compila com `-Wall`; confira o log (`gh run view <id> --log`) procurando "warning".
- O `core.autocrlf` do Windows tira a permissão de execução do `build-mingw.sh` e põe CRLF nele. Já existe `.gitattributes` com `*.sh text eol=lf`, e o modo executável foi restaurado com `git update-index --chmod=+x`.

---

## 4. Arquitetura

### Threads (a "cozinha")

- **Thread principal:** roda o laço de mensagens do Windows. É a única que mexe em janela, ícone, menu e widget.
- **Thread de leitura** (`LacoLeitura` no `main.cpp`): a cada `intervalo_segundos` (10 s), lê o leitor, passa pelo `Suavizador`, registra no `Historico`, calcula as estimativas, guarda num `Estado` protegido por trava e avisa a janela com `PostMessage(WM_APP_LEITURA)`.
- **Thread do LeitorXenta:** fica escutando o receptor, com leitura overlapped.
- **Threads avulsas:** diagnóstico, envio pro ntfy e procura/download de atualização. Elas respondem com `PostMessage`.

**Regra:** a thread principal precisa ser rápida. O widget é **filho de uma janela do Explorer** (a barra de tarefas), e uma thread principal travada pode travar a barra.

### Mensagens próprias (`comum.h`)

| Mensagem | Quando chega |
|---|---|
| `WM_APP_BANDEJA` | Cliques no ícone da bandeja (`NOTIFYICON_VERSION_4`) |
| `WM_APP_LEITURA` | Dados novos da thread de leitura |
| `WM_APP_DIAG_OK` | O diagnóstico terminou |
| `WM_APP_WIDGET` | Clique no widget (wParam 1 = esquerdo, 2 = direito) |
| `WM_APP_ATUALIZACAO` | O atualizador mudou de estado (wParam = `atualizador::Estado`, lParam = 1 se foi pedido manual) |

### Mapa dos arquivos

| Arquivo | Papel |
|---|---|
| `src/main.cpp` | O maestro: `wWinMain`, ícone da bandeja, menu, timers, thread de leitura e reação a todas as mensagens |
| `src/leitores.cpp/.h` | Lista os HID (SetupDi + HidD/HidP) e tem os leitores Xenta, Rongyuan e Demo, mais o diagnóstico |
| `src/suavizador.h` | Filtro anti pula-pula da % |
| `src/historico.cpp/.h` | `leituras.csv` e `cargas.csv`, marcos de queda, sessões de uso e estimativas |
| `src/avisos.cpp/.h` | Faixa por cima do jogo, ntfy e o "porteiro" `GerenciadorAvisos` |
| `src/icone.cpp/.h` | Desenha o mouse com selo e o modo número (GDI+), além do tema claro/escuro |
| `src/widget.cpp/.h` | O "🖱 80%" encaixado na barra de tarefas |
| `src/grafico.cpp/.h` | Janela do histórico clássica (item "Ver histórico" do menu), gráfico desenhado à mão em GDI+ |
| `src/hub.cpp/.h` | O hub (v1.0.2): janela escura aberta pelo clique esquerdo, com páginas Início, Histórico, Avisos, Ajustes e Sobre. Tudo em GDI+, com "zonas clicáveis" refeitas a cada pintura. Cada botão vira um `HubAcao` que o `main.cpp` traduz para o mesmo `CMD_*` do menu |
| `src/polling.cpp/.h` | Polling rate medido pelo Raw Input (thread própria + janela só de mensagens). `Medidor` é a conta pura (testada); grava `polling.csv` quando muda |
| `src/atualizador.cpp/.h` | Atualização pelo GitHub Releases |
| `src/config.cpp/.h` | `config.ini` (GetPrivateProfile*) |
| `src/sistema.cpp/.h` | Iniciar com o Windows (registro `Run`) e mutex de instância única |
| `src/versao.h` | Versão (o build preenche), repositório e macro de texto largo |
| `src/comum.h` | `Leitura`, `Trava`/`Segurando`, `Agora()`, `FormatarMinutos`, `PastaDados()` e mensagens |
| `src/gdiplus_inc.h` | Inclui o GDI+ convivendo com `NOMINMAX` |
| `res/` | Ícone do programa (barbatana com raio), manifesto (Common Controls 6, PerMonitorV2) e versão |
| `tests/testes.cpp` | Testes: protocolo, histórico, polling, suavizador, avisos, atualizador e ícones |
| `tools/sonda-hid.ps1` | Sonda **somente leitura** do receptor (interfaces HID e `GetFeature`). Não faz parte do build |

### Dados do usuário (`%APPDATA%\SharkInfo\`)

`config.ini`, `leituras.csv` (`momento;percentual;carregando`), `cargas.csv` (`inicio;fim;percentual_inicial`), `polling.csv` (`momento;hz`, só quando o polling medido muda), `diagnostico.txt`, `atualizacao\` (o `.exe` baixado) e `demo\`, que guarda o histórico separado do modo `--demo`. A chave antiga `horas_bateria_padrao` pode continuar no `config.ini` de quem já usava o app; é ignorada.

### Argumentos de linha de comando

- `--demo`: mouse simulado com relógio acelerado (1 s real = 1 min).
- `--hub`: já abre o hub ao iniciar (útil para testar).
- `--diagnostico`: gera e abre o `diagnostico.txt`.
- `--pos-atualizacao <pid>`: uso interno do atualizador.

---

## 5. Como cada parte funciona

### 5.1 Protocolos dos receptores

**Xenta (VID `1D57`).** Receptores 2.4G nos PIDs `FA60`, `FA62` e `FA65`; cabo em `FA55`, `FA56`, `FA61`, `FA50` e `2155`.

- O receptor **transmite sozinho**, a cada ~2 s, no canal de usage page `0x000A`, a mensagem `[03][modelo][40|41][estado][nível]`.
- Estado: 1 = na bateria, 2 = cheio, 3 = carregando. No cabo, o nível fica **congelado**.
- O modelo `0x10` (X6/R1) manda o nível em dezenas (0..10). Os outros mandam em %.
- O leitor abre o canal **só para leitura** (convive com o app oficial).
- Ao reconectar, o primeiro valor costuma ser um falso 100%, então o leitor só confia quando **duas mensagens seguidas são iguais**.
- Depois de 15 s sem mensagem, o mouse é tratado como dormindo.

**Rongyuan** (lista de VID:PID no `leitores.cpp`; Attack Shark R3 e outros):

- Pergunta: feature report 0, `[00][F7][00…]`, com o tamanho exato do `FeatureReportByteLength`.
- Resposta: `[id][ok][% teclado][% mouse][teclado off][mouse off]`.

#### Pesquisa do polling rate (o que se sabe do receptor, 18/11/2026)

O dono quer o polling **lido do mouse**, não medido, porque o software oficial deixa escolher a taxa. O que foi levantado no receptor `1D57:FA60` do X6/R1 (rodando `--diagnostico` e `tools/sonda-hid.ps1`):

| Interface | Page / usage | Tamanhos | Função |
|---|---|---|---|
| `mi_00` | `0001 / 0006` | in 9, out 2 | teclado (padrão) |
| `mi_01` | `0001 / 0002` | in 8 | **o mouse** |
| `mi_02&col01` | `0001 / 0080` | in 2 | controle do sistema |
| `mi_02&col02` | `000C / 0001` | in 3 | teclas de mídia |
| `mi_02&col03` | `000A / 0000` | in 5 | **a bateria** (o canal que o app já escuta) |
| `mi_02&col04` | `000B / 0000` | **feature 134** | **canal de configuração**, quase certamente o que o software oficial usa |
| `mi_03` | `0001 / 0006` | in 15 | teclado (N-key) |

- `HidD_GetFeature` em todos os report IDs de 0 a 255 **falha** em todas as interfaces. O canal `col04` é de comando e resposta: só responde depois de receber um comando por `SetFeature`, e os comandos não são públicos. **Não** mande bytes aleatórios nele: pode regravar configuração do mouse.
- Software oficial em `C:\Program Files (x86)\Attack Shark Software\`: `AttackShark.exe` (a lógica), `hidapi.dll`, `Hook.dll` e um `hiddriver_N.dll` por modelo (1 a 18 e `4k_1`). Os `hiddriver_N.dll` só exportam o transporte (`Open_FeatureDevice`, `GetFeature`, `SetFeature`, `Hid_Read`, `Hid_Write`, `Set_VIDPID`, `Open_DevMonitor`...), sem os comandos. Os `Data\ms_N\pro.data` são o perfil salvo do mouse (quase todo zero), sem protocolo. Na mesma máquina há também o "REDRAGON Gaming Mouse" (software parecido, mesma família de firmware, hipótese não verificada).
- **Como descobrir o comando:** capturar o USB (USBPcap + Wireshark) enquanto o dono troca o polling no software oficial entre 125, 250, 500 e 1000 Hz, e comparar os `SET_REPORT`/`GET_REPORT` de 134 bytes da interface `col04`. A leitura do polling atual deve ser um `SetFeature` de consulta seguido de `GetFeature`. Alternativa: desmontar `AttackShark.exe`.
- Enquanto isso o app **mede** o polling pelo Raw Input. A medida só enxerga o que o Windows recebe; não vê o que está configurado no mouse.

**Fontes:** krisgfx9/Wireless-Device-Battery-Monitor, KaiSta/attackSharkOnLinux, incconutwo/mouse-battery-tray e qu1ncky/BatteryTray. O projeto do krisgfx é **GPL-3.0**. Usamos só os **fatos do protocolo**, sem copiar código, e deve continuar assim.

### 5.2 Avisos (`GerenciadorAvisos`)

- Avisa em 20% (aviso) e 10% (crítico), **uma vez por descarga**, com folga de 5% para não repetir quando o valor oscila no limite.
- Plugar o cabo zera os avisos. Ao chegar a 100% no cabo, avisa "pode tirar do cabo".
- Cada aviso vai por três canais, cada um com sua opção: notificação do Windows (balão com o ícone desenhado, via `NIIF_USER`), faixa por cima do jogo (`Faixa`) e celular (ntfy, JSON via WinHTTP).
- A faixa é uma janela `WS_EX_LAYERED | TRANSPARENT | TOOLWINDOW | NOACTIVATE | TOPMOST`, mostrada com `SWP_NOACTIVATE`. Ela **nunca pode pegar o foco**: se pegasse, o jogo em tela cheia minimizaria. Funciona em jogo "sem bordas", mas não em tela cheia exclusiva (limitação do Windows).

### 5.3 Suavizador (anti pula-pula)

Ele roda **antes** de tudo (histórico, ícone e avisos), em `LacoLeitura`.

- **Descer:** a moda dos últimos 3 min precisa ser menor. No empate, mantém o valor atual.
- **Subir fora do cabo:** a moda precisa ficar acima por 10 min seguidos.
- **No cabo:** passa direto. Ao sair do cabo, aceita o valor novo na hora.
- **Dormindo:** mostra o último valor bom.

### 5.4 Estimativas (`historico.cpp`)

- **Sessões de uso:** cortam no cabo e em buracos de mais de 30 min sem leitura. O app grava pelo menos 1 linha por minuto enquanto o mouse está acordado. Mouse dormindo ou PC desligado criam buracos, e assim a conta mede **tempo de uso**.
- **Marcos de queda:** são os momentos em que a % chega a um valor mais baixo do que já tinha chegado na sessão. A taxa é `(1º marco − último marco) / tempo entre eles`. Exige 2 marcos e pelo menos 10 min entre eles. Subidas são ignoradas.
- **Tempo restante:** a fonte `SESSAO` usa a taxa da descarga atual. Se não houver, a fonte `HISTORICO` usa a média das descargas anteriores, ponderada pelo tempo de uso. Sem taxa nenhuma, a fonte `MINIMO` afirma "dura pelo menos `pct × d / 10`", onde `d` é o tempo de uso no mesmo degrau (só depois de 10 min; é um limite de verdade, não um chute, e só cresce). Os avisos não usam o `MINIMO`. Sem nem isso, `minutos = -1` e a interface mostra "calculando…" (não existe mais chute inicial). O marco de cada nível vale o fim do pula-pula (logo depois da última leitura acima dele). Uma "descarga" vai de um cabo ao próximo; buracos sem leitura não cortam, só não contam no relógio de uso (por isso marcos de dias diferentes se somam). Só corta se, depois de um buraco, a % subir 20 pontos ou mais sem cabo (carregou fora do app).
- **Carga:** o mouse não informa a % no cabo. Então o app guarda a % de quando o cabo foi plugado e o horário em que chegou a 100%, e aprende a mediana dos minutos por % das últimas 5 cargas completas (só cargas que começaram abaixo de 95%). Até aprender, usa `minutos_carga_padrao` (120). O número no ícone enquanto carrega é o **palpite** da % atual.

### 5.5 Ícone e widget

- **Ícone:** mouse em cápsula (contorno branco no tema escuro, quase preto no claro) e selo no canto inferior direito.
  - Cores do selo: verde > 50%, amarelo > aviso, laranja > crítico, vermelho, azul com raio (carregando) e cinza com "z" (dormindo) ou "?" (não achou).
  - Em volta do selo há uma "borracha" (área apagada) para ele não encostar no contorno.
  - O tema é lido em `HKCU\...\Themes\Personalize` (`SystemUsesLightTheme`) e redesenhado no `WM_SETTINGCHANGE "ImmersiveColorSet"`.
- **Widget:** janela `WS_CHILD` de `Shell_TrayWnd`, posicionada à esquerda de `TrayNotifyWnd`. Ele é reposicionado a cada 1 s (timer) e recriado no `TaskbarCreated`.
  - O desenho usa `UpdateLayeredWindow` (alfa por pixel) com fundo de alfa 1. O alfa 1 é invisível, mas recebe o clique.
  - No Windows 10, ele pula o "Notícias e interesses" e encurta o `ReBarWindow32`.
  - Ainda não cobre: barra no topo/vertical (só tratamento básico), segundo monitor e alinhamento fino no Windows 11. É **a parte mais frágil**, porque não existe API oficial para isso. Se o encaixe falhar, o app segue só com o ícone.
- **Modo número** (`icone_numero=1`): quadrado colorido com a % no ícone da bandeja.

### 5.6 Atualizador (modelo do app Sinal)

1. **Procura:** 10 s depois de abrir e a cada 30 min, mais a opção manual no menu. Pede `api.github.com/repos/Pedr030/sharkinfo/releases/latest` com WinHTTP e User-Agent `SharkInfo/x`. A leitura do JSON é feita à mão, entendendo `\uXXXX` e emoji.
2. **Baixa:** se a versão for mais nova (`CompararVersoes`), baixa o `.sha256` e o `.exe` para `%APPDATA%\SharkInfo\atualizacao\` e confere o SHA-256 (BCrypt). Se não bater, o arquivo é descartado.
3. **Avisa:** só se `UsuarioOcupado()` for falso. Essa função usa `SHQueryUserNotificationState` e também testa se a janela da frente cobre o monitor inteiro (jogo sem bordas). Se o usuário estiver ocupado, tenta de novo a cada 1 min. Clicando no balão (`NIN_BALLOONUSERCLICK`), abre um `TaskDialog` com as novidades e as opções "Reiniciar agora" e "Depois".
4. **Troca:** renomeia o `.exe` aberto para `.exe.old`, move o novo para o lugar, abre o novo com `--pos-atualizacao <pid>` e sai. O novo espera o PID antigo fechar e apaga o `.old`. No "Depois", a troca acontece no `WM_DESTROY` (ao sair), sem reabrir.
5. **Versão `-dev`:** o atualizador fica desligado. O app não deve ficar em `Arquivos de Programas`, porque precisa de permissão para renomear o próprio arquivo.

### 5.7 O hub (`hub.cpp`)

- Clique esquerdo no ícone da bandeja (`NIN_SELECT`) ou no widget (`WM_APP_WIDGET`, wParam 1) chama `AbrirHub()` no `main.cpp`. O **clique direito continua igual** (menu). O item "Ver histórico" do menu ainda abre a janela clássica (`grafico.cpp`).
- Janela normal (`WS_OVERLAPPEDWINDOW`, mínimo 900 × 640 a 96 dpi, repinta a cada 1 s) com barra de título escura via `DwmSetWindowAttribute` carregado dinamicamente (sem `dwmapi.lib`). Páginas: Início, Histórico, Avisos, Ajustes, Sobre.
- **Sem controles do Windows.** Tudo é GDI+ em `Hub::Pintar`, com double buffering. Cada pintura refaz `zonas_` (retângulos com um id); `WM_MOUSEMOVE` acha o id sob o cursor (hover) e `WM_LBUTTONUP` age. Ids: `1000..1004` trocam de página, `2000/2001` o período do gráfico, e o resto são `HubAcao` (enum em `hub.h`).
- O hub **não conhece o app**: recebe `std::function`s. `MontarDadosHub()` (main) devolve a foto `DadosHub`; `AcaoDoHub(int)` (main) traduz cada `HubAcao` para o mesmo `CMD_*` do menu via `ExecutarComando`, então menu e hub nunca divergem. Os limites de aviso (botões − e +) mudam de 5 em 5 por `AjustarLimite` (aviso de 10 a 50, crítico de 5 até aviso − 5).
- O bitmap grande do mouse vem de `CriarBitmapMouse` (o mesmo do ícone) e fica em cache (`cacheMouse_`).
- Ícones da barra lateral são desenhados com linhas (`Hub::Icone`), sem fonte de ícones.

### 5.8 Polling rate medido (`polling.cpp`)

- Thread própria com uma janela comum oculta (`WS_POPUP`, sem tamanho) registrada com `RegisterRawInputDevices(0x01, 0x02, RIDEV_INPUTSINK)`. A thread principal fica livre.
- A cada `WM_INPUT` de mouse (só movimento relativo, `lLastX/Y` diferentes de zero) o `Medidor` recebe o instante em ms (`QueryPerformanceCounter`). Um **trecho** é um movimento contínuo (pausa maior que 40 ms fecha, e trecho longo fecha a cada 500 ms); a taxa do trecho é `(avisos − 1) / duração`. Vale trecho com pelo menos 25 avisos e 100 ms. O resultado é o **percentil 75** das últimas 24 taxas (mouse devagar perde avisos, então erra para baixo), arredondado para 125, 250, 500, 1000, 2000, 4000 ou 8000 Hz pela distância em escala logarítmica. Precisa de 2 trechos.
- Um `Medidor` por dispositivo (`hDevice`). O Attack Shark é reconhecido pelo nome do dispositivo (`VID_1D57`) e tem preferência; outros mouses só valem se nenhum Attack Shark apareceu.
- `Resultado` traz `hz`, `aoVivo`, `momento`, `eventos` (todos os `WM_INPUT` de mouse) e `trechos`. A medida é guardada em `polling.csv` quando muda e relida na abertura (aparece como "Última medição").
- Acima de 2000 Hz o Windows pode juntar avisos e a medida perde precisão.

---

## 6. Convenções

- **Código e comentários em português**, com explicação didática quase linha por linha e analogias. O dono é estudante de Ciência da Computação, aprende pelo código e pediu esse estilo. Mantenha o padrão em arquivos novos.
- **Commits:** Conventional Commits em português (`feat:`, `fix:`, `docs:`, `ci:`, `release:`), **sem linha de coautoria/IA**, igual ao repositório do Sinal.
- **Sem dependências externas.** Se algo parecer exigir uma biblioteca, procure primeiro a API do Windows equivalente.
- **Zero avisos** com `-Wall -Wextra`, que é como tudo foi entregue até agora.
- Toda função "de conta" (sem janela nem USB) deve ficar separada e ganhar teste no `tests/testes.cpp`.
- Antes de dizer que uma parte visual está pronta, peça um **print real** ao dono. As "prévias" feitas durante o desenvolvimento eram simulações.

---

## 7. Próximos passos sugeridos

1. **Polling rate:** pedir o print do cartão (ver "Pendências abertas", item 1) e corrigir a escuta. Depois, se o dono topar, capturar o USB com o software oficial para ler o polling direto do mouse (seção 5.1, "Pesquisa do polling rate").
2. **Toggle do número no ícone:** perguntar ao dono o que ele vê e seguir as hipóteses da pendência 2.
3. Ver com print as abas Histórico, Avisos, Ajustes e Sobre do hub.
4. Confirmar o **texto do widget** e o alinhamento com um print sem a dica por cima.
5. Testar a **faixa** e as **notificações** com o `--demo` (descarrega até 5%, vai pro cabo e enche em ~2 min).
6. Ver o tempo restante de verdade quando o mouse cair de 80 para 70 (primeira queda de 10%) e conferir se o número é plausível.
7. Ideias que o dono pode querer: separar a estimativa de bateria por polling rate, suporte a headset (o print original do krisgfx mostrava headset + mouse), widget em segundo monitor e escolha de cores.
