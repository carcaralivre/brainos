# BrainOS: guia rápido (pt-BR)

## Acordado e dormindo
- **Acordado**: o Wi-Fi fica ligado, as gravações e perguntas pendentes são enviadas e o http://brain.local responde.
- **Dormindo**: tudo desligado; a lock screen mostra um cadeadinho acima de "N notes". Só o OK acorda: ele mostra a lock screen sem o cadeado, e outro OK destranca onde você estava.
- **Na bateria** dorme 3 minutos depois do último toque, botão ou uso da página brain.local (ajuste no brain.local). Se uma gravação ou pergunta estiver sendo enviada, termina e depois dorme. **Na tomada** nunca dorme.
- À meia-noite ele acorda sozinho por um instante, troca a data da lock screen e volta a dormir.

## Lock screen
Bateria, o dia da semana (grande, em negrito) e a data embaixo, até 3 notas fixadas (4 linhas cada; tachinha, e balão se for chat) e "N notes". Tocar numa fixada abre a nota; tocar fora abre a lista; OK volta para a tela de onde você trancou.

## Botões segurados
| Onde | Segurar OK | Segurar Cima | Segurar Baixo |
|---|---|---|---|
| Lista, lock screen, Settings… | nota nova (abre nela) | nota de chat nova (abre nela) | **Chat** de passagem |
| Dentro de uma nota | nota nova | nota de chat nova | **acrescenta** ao fim da nota |
| Na tela Chat | nota nova | nota de chat nova | continua a conversa |

- Uma gravação tem no máximo **30 segundos**: ao chegar lá a tela mostra "30 s is the limit" e salva o que foi dito até ali.
- Uma gravação **nunca é apagada** porque o OpenRouter recusou (sem crédito, chave recusada, modelo desconhecido): a nota diz o motivo no lugar de "waiting for Wi-Fi" e tenta de novo quando o problema passa ou quando você salva os ajustes no brain.local. Só "Nothing was heard" (silêncio) encerra a gravação.
- Se a gravação não deu certo, a tela diz por quê por um instante ("Too short", "Storage full", "Microphone didn't start") e volta para onde estava. Com o teclado aberto, segurar um botão não grava nada (para não perder o que você digitou).
- Depois de "Listening… / Saving…" a nota abre direto. Enquanto espera ela mostra "Recording, N s, waiting for Wi-Fi" (ou "Question, …"), depois o texto com "(asking…)" e por fim a resposta; a tela se atualiza sozinha.
- **Acrescentar** (Baixo numa nota): grava e, quando transcrever, junta ao fim com uma linha em branco antes. Numa nota de chat, junta a fala e depois a resposta; o assistente recebe a conversa da nota (as últimas 10 trocas). Um acréscimo por vez: se o anterior ainda não chegou, aparece "Still transcribing the last one".
- **Chat de passagem** (Baixo fora de uma nota): grava, transcreve, o assistente responde olhando as suas notas da lista (as fixadas e as mais recentes). Segurar Baixo de novo continua a conversa. Nada é salvo, a não ser que você toque em **Save as note** (vira uma nota de chat). Sair da tela (seta, OK, dormir) descarta.

## Toques e botões curtos
- **OK curto**: tranca de onde você estiver; outro OK destranca e volta para lá. No teclado vale como Done.
- **Cima/Baixo**: passam as páginas das listas. Numa nota curta, vão para a anterior e a seguinte; numa nota longa, rolam as páginas ("2 / 3" no topo). Deslizar para os lados troca de nota.
- **Cima + Baixo juntos**: tranca. Segurando os dois por 2 s: dorme.
- **Lista de notas**: a seta no canto esquerdo volta para a lock screen; New note abre o teclado (Done salva e abre a nota); Settings tem Wi-Fi, Export, Archive e About.
- **Nota**: uma nota de chat tem "Chat note" no topo (o balãozinho fica só nas listas). Pin, Edit, Tidy em cima; Archive e Delete embaixo. Edit com o texto vazio não muda nada (para apagar, Delete). Uma nota aceita até 4000 caracteres no teclado e no brain.local. Enquanto a nota espera a nuvem, Edit e Archive ficam apagados; se ela espera uma resposta ou o Tidy, o terceiro botão vira **Cancel**. Numa nota já arrumada o terceiro botão é **Original**, que devolve o texto de antes (mexer na nota depois do Tidy descarta esse original). Nota de chat não tem Tidy. Se você trancar ou o aparelho dormir com o teclado aberto, o que foi digitado é salvo. **Tidy** pede ao assistente para corrigir a pontuação e tirar vícios de fala sem mudar o sentido; o original fica guardado e sai no Markdown como "(original: …)".
- **Apagar** (nota, arquivo, rede salva): o primeiro toque arma ("Yes, …"), o segundo confirma, tocar em outro lugar desarma.
- **Fixadas**: até 3. Ao fixar uma quarta, a mais antiga é liberada.

## Custo
Cada nota mostra, na linha da data, quanto custou (transcrições e respostas; "US$ 0" numa nota digitada); o valor é o que o OpenRouter informa, e uma transcrição que ele não informa é calculada pela duração e pelo preço do modelo. Settings → About mostra hoje, esta semana e este mês; o brain.local mostra os últimos 7 dias, 4 semanas e 12 meses e o total. Apagar uma nota não apaga o gasto dela. 

## A página brain.local
Com o aparelho acordado e na rede, abra http://brain.local no iPhone ou no Mac: criar, editar, fixar, Tidy, arquivar e apagar notas (apagar, arquivar e exportar têm **Undo** por 6 segundos), exportar as notas da lista em Markdown, baixar tudo, adicionar e esquecer redes Wi-Fi, checar e instalar atualização, e os ajustes: chave do OpenRouter, modelo e idioma de transcrição, "Transcription style", modelo e instruções do assistente, nome, fuso (uma escolha na página não é mais sobrescrita ao mudar de rede) e minutos até dormir na bateria. A página mostra as 50 notas mais recentes ("Show all" mostra todas) e se recarrega sozinha quando as notas mudam (uma aba esquecida aberta não impede o aparelho de dormir). **Senha**: no aparelho, Settings → About → **Create page password**; a página então deixa criar uma senha por dois minutos (8 caracteres ou mais; use uma só do Brain, porque ela trafega sem criptografia pelo Wi-Fi). Depois disso cada navegador digita a senha uma vez e fica lembrado por 30 dias, mesmo com o aparelho dormindo e acordando. **Log out** na página troca o token e pede a senha de novo em todos os navegadores. Cinco senhas erradas seguidas: espera de um minuto. Esqueceu? No aparelho, Settings → About → **Reset page password** (dois toques): abre a mesma janela de dois minutos. Sem o toque, ninguém na rede cria a senha. Undo de um arquivamento só devolve a nota à lista, sem desfazer uma transcrição que tenha chegado. O botão no topo abre a página leitora de QR.

## Rede e chave
1. Settings → Wi-Fi → "Add a network": escolha a rede (o cadeado marca as com senha, as barras mostram o sinal), digite a senha, Done. "Scan again" procura de novo. Na lista de redes salvas (a conectada no topo, as outras em ordem alfabética), tocar no nome conecta (há um Cancel enquanto tenta); tocar no X arma (ele fica branco num quadrado preto) e o segundo toque nele esquece.
2. No aparelho: Settings → About → **Create page password** (ou **Reset page password** se já existia). Você tem dois minutos. No iPhone ou Mac, na mesma rede: http://brain.local (ou o IP em Settings → About). Crie a senha da página, cole a chave do OpenRouter e Save. Sem o toque no aparelho a página não aceita criar senha.

## Exportar para o celular
Settings → Export: as notas da lista vão nos QR codes, um por segundo; uma nota longa vai em pedaços que a página leitora junta de volta. Notas que ainda esperam transcrição, resposta ou Tidy ficam para o próximo export. No iPhone, abra a página leitora (https://carcaralivre.github.io/brainos/reader/), toque em Scan e aponte para a tela. Toque em "Archive all" no aparelho e confirme ("Yes, archive all"): as notas vão para o Archive (configurável no brain.local).

## Atualizar
Settings → About → Check for update; se houver versão nova, aparece o botão Install. Sem Wi-Fi, a tela diz "No Wi-Fi". Também dá pelo brain.local. Se a versão nova não ligar, o aparelho volta sozinho para a anterior.

## Restaurar o firmware da Seeed
Pelo Playground da Seeed (https://www.seeedstudio.com/sticky/playground/official-firmware/), no Chrome, com o cabo USB.
