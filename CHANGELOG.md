# Novidades

Cada versão publicada tem uma seção aqui. O texto da seção vira as notas da release e aparece na janela de atualização do app.

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
