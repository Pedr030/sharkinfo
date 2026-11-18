# Novidades

Cada versão publicada tem uma seção aqui. O texto da seção vira as notas da release e aparece na janela de atualização do app.

## v1.0.3

- Tempo restante mais honesto: enquanto o app ainda não mediu a descarga, o destaque fica "calculando…" e o valor garantido aparece pequeno ("Garantido: ≥ 7 h"). Antes o número grande só subia, parecendo uma contagem ao contrário.
- Hub: textos que estavam cortados nos cartões foram encurtados e o cartão do polling rate ganhou espaço.
- Polling rate: a escuta ficou mais sensível (aceita movimentos mais curtos) e o cartão mostra quantos avisos de movimento já chegaram, pra dar pra ver se a medição está viva.

## v1.0.2

- Novo **hub**: o clique esquerdo no ícone (ou na % da barra de tarefas) abre uma janela escura com tudo num lugar só: o mouse grande com a bateria, tempo restante, polling rate, tempo de carga, histórico, avisos, ajustes e atualizações. O clique direito continua abrindo o mesmo menu de antes.
- **Polling rate**: o app mede quantas vezes por segundo o mouse se comunica (125, 250, 500, 1000 Hz...) pelo movimento do mouse e mostra no hub. Mexa o mouse um pouco e o número aparece.
- Tempo restante sem esperar tanto: depois de 10 min no mesmo degrau da bateria o app já mostra "dura pelo menos X", um mínimo de verdade que cresce a cada minuto de uso e vira a medida real assim que a bateria cai um degrau.
- Mouse pulando entre dois níveis (80 ↔ 90) não atrapalha mais a conta do tempo restante.

## v1.0.1

- Fim do "chute" de tempo restante: antes o app mostrava algo como "48 h (estimativa inicial)". Agora, sem dados reais, mostra só "calculando…".
- A estimativa sai mais cedo: o tempo em que o mouse dorme ou o PC fica desligado não zera mais a conta. Quedas de dias diferentes se somam, e depois da primeira descarga o app já sabe o tempo restante assim que você tira o mouse do cabo.
- Se você está vendo este texto na janela de atualização, o ciclo completo do atualizador (procurar, baixar, conferir o SHA-256 e trocar o arquivo) funcionou.

## v1.0.0

- Primeira versão publicada no GitHub.
- Atualização automática: o app procura versões novas sozinho, baixa em segundo plano, confere a impressão digital (SHA-256) do arquivo e avisa fora do jogo. Você escolhe "Reiniciar agora" ou "Depois" (instala ao fechar).
- % da bateria na barra de tarefas, ao lado do relógio, e ícone de mouse com selo colorido na bandeja.
- Opção de mostrar o número no ícone da bandeja (Aparência).
- Filtro que impede a % de ficar pulando (80 → 90 → 80) em mouses que medem de 10 em 10%.
- Tempo restante calculado pelos "marcos de queda", aprendendo com os dias anteriores.
- Avisos em 20% e 10% (notificação, faixa por cima do jogo e celular via ntfy), tempo de carga aprendido, histórico em gráfico, iniciar com o Windows.
