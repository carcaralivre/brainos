# BrainOS: guia rápido (pt-BR)

## Acordado e dormindo
- **Acordado**: o Wi-Fi fica ligado, as gravações e perguntas pendentes são enviadas e o http://brain.local responde.
- **Dormindo**: tudo desligado; a lock screen mostra um cadeado. Só o OK acorda: ele mostra a lock screen sem o cadeado, e outro OK destranca onde você estava.
- **Na bateria** dorme 3 minutos depois do último toque ou botão (ajuste no brain.local). Se uma gravação ou pergunta estiver sendo enviada, termina e depois dorme. **Na tomada** nunca dorme.
- À meia-noite ele acorda sozinho por um instante, troca a data da lock screen e volta a dormir.

## Lock screen
Bateria, a data por extenso, até 3 notas fixadas (4 linhas cada; tachinha, e balão se for chat) e "N notes". Tocar numa fixada abre a nota; tocar fora abre a lista; OK volta para a tela de onde você trancou.

## Botões segurados
| Onde | Segurar OK | Segurar Cima | Segurar Baixo |
|---|---|---|---|
| Lista, lock screen, Settings… | nota nova (abre nela) | nota de chat nova (abre nela) | **Chat** de passagem |
| Dentro de uma nota | nota nova | nota de chat nova | **acrescenta** ao fim da nota |
| Na tela Chat | nota nova | nota de chat nova | continua a conversa |

- Depois de "Listening… / Saving…" a nota abre direto. Enquanto espera ela mostra "Recording, N s, waiting for Wi-Fi" (ou "Question, …"), depois o texto com "(asking…)" e por fim a resposta; a tela se atualiza sozinha.
- **Acrescentar** (Baixo numa nota): grava e, quando transcrever, junta ao fim com uma linha em branco antes. Numa nota de chat, junta a fala e depois a resposta; o assistente recebe a conversa da nota (as últimas 10 trocas). Um acréscimo por vez: se o anterior ainda não chegou, aparece "Still transcribing the last one".
- **Chat de passagem** (Baixo fora de uma nota): grava, transcreve, o assistente responde olhando as suas notas da lista (as fixadas e as mais recentes). Segurar Baixo de novo continua a conversa. Nada é salvo, a não ser que você toque em **Save as note** (vira uma nota de chat). Sair da tela (seta, OK, dormir) descarta.

## Toques e botões curtos
- **OK curto**: tranca de onde você estiver; outro OK destranca e volta para lá. No teclado vale como Done.
- **Cima/Baixo**: passam as páginas das listas. Numa nota curta, vão para a anterior e a seguinte; numa nota longa, rolam as páginas ("2 / 3" no topo). Deslizar para os lados troca de nota.
- **Cima + Baixo juntos**: tranca. Segurando os dois por 2 s: dorme.
- **Lista de notas**: a seta no canto esquerdo volta para a lock screen; New note abre o teclado (Done salva e abre a nota); Settings tem Wi-Fi, Export, Archive e About.
- **Nota**: Pin, Edit, Tidy em cima; Archive e Delete embaixo. Enquanto a nota espera a nuvem, Edit, Tidy e Archive ficam apagados. **Tidy** pede ao assistente para corrigir a pontuação e tirar vícios de fala sem mudar o sentido; o original fica guardado e sai no Markdown como "(original: …)".
- **Apagar** (nota, arquivo, rede salva): o primeiro toque arma ("Yes, …"), o segundo confirma, tocar em outro lugar desarma.
- **Fixadas**: até 3. Ao fixar uma quarta, a mais antiga é liberada.

## Custo
Cada nota mostra, na linha da data, quanto custou (transcrições e respostas; "US$ 0" numa nota digitada). Settings → About mostra hoje, esta semana e este mês; o brain.local mostra os últimos 7 dias, 4 semanas e 12 meses e o total. Apagar uma nota não apaga o gasto dela. A transcrição é calculada pela duração do áudio e pelo preço do modelo; a resposta, pelo que o OpenRouter informa.

## A página brain.local
Com o aparelho acordado e na rede, abra http://brain.local no iPhone ou no Mac: criar, editar, fixar, Tidy, arquivar e apagar notas (apagar, arquivar e exportar têm **Undo** por 6 segundos), exportar as notas da lista em Markdown, baixar tudo, adicionar e esquecer redes Wi-Fi, checar e instalar atualização, e os ajustes: chave do OpenRouter, modelo de transcrição, "Transcription style", modelo e instruções do assistente, nome, fuso (ao conectar numa rede nova o aparelho acerta o fuso e o idioma sozinho) e minutos até dormir na bateria. O botão no topo abre a página leitora de QR.

## Rede e chave
1. Settings → Wi-Fi → "Add a network": escolha a rede (o cadeado marca as com senha, as barras mostram o sinal), digite a senha, Done. "Scan again" procura de novo. Na lista de redes salvas, tocar no nome conecta (há um Cancel enquanto tenta); tocar no X arma ("Yes, forget") e o segundo toque esquece.
2. No iPhone ou Mac, na mesma rede: http://brain.local (ou o IP em Settings → About). Cole a chave do OpenRouter e Save.

## Exportar para o celular
Settings → Export: as notas da lista vão nos QR codes, um por segundo. No iPhone, abra a página leitora (https://carcaralivre.github.io/brainos-reader/), toque em Scan e aponte para a tela. Toque em "Archive all" no aparelho e confirme ("Yes, archive all"): as notas vão para o Archive (configurável no brain.local).

## Atualizar
Settings → About → Check for update; se houver versão nova, aparece o botão Install. Sem Wi-Fi, a tela diz "No Wi-Fi". Também dá pelo brain.local. Se a versão nova não ligar, o aparelho volta sozinho para a anterior.

## Restaurar o firmware da Seeed
Pelo Playground da Seeed (https://www.seeedstudio.com/sticky/playground/official-firmware/), no Chrome, com o cabo USB.
