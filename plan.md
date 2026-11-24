# Plano de correção — auditoria pré-produção

Fonte: auditoria de 24 Nov 2026 (somente leitura). Nada disto foi aplicado ainda.

Regra de execução: um achado por vez, nesta ordem. Para cada um: mostrar o diff, explicar, compilar, e dizer como conferir no aparelho que o resto não quebrou. Parar e perguntar antes de qualquer mudança de arquitetura, de tela ou de gesto.

Não entra neste plano: reescrever o formato das notas, unificar o visual da página com o e-paper, traduzir a interface, nem ligar flash encryption.

## Antes de outra pessoa usar

### 1. BUG-02 — Não formatar o caderno sozinho

`LittleFS.begin(true)` em `firmware/brainos/brainos.ino` apaga todas as notas se a montagem falha, e a tela só mostra "0 notes".

- Trocar para `begin(false)`.
- Se falhar, uma tela dizendo que as notas não foram lidas e que nada foi apagado. Formatar só com um segundo gesto, separado.
- Esforço S. Não muda o uso normal.

Conferir: o aparelho liga como hoje. O caminho de falha só aparece com o filesystem propositalmente ilegível (não testar isso no aparelho do dia a dia).

### 2. BUG-01 — Não apagar o áudio se o save falhar

Em `firmware/brainos/stt.cpp`, o WAV é removido e só depois `notesSave()` é chamado. O retorno é ignorado em todos os chamadores (`web.cpp`, `ui.cpp`, `stt.cpp`, `chat.cpp`, `export.cpp`, `brainos.ino`). Se o disco está quase cheio, a nota aparece, o aparelho dorme, e no acordar a gravação virou "recording lost".

- Só apagar o WAV depois de `notesSave()` verdadeiro.
- Se falhar, manter o áudio e mostrar "Não salvo: memória cheia" na nota e na página.
- O mesmo critério em archive, delete e export: não seguir como se tivesse gravado.
- Esforço S.

Conferir: gravar uma nota curta com espaço de sobra. Ela transcreve, o arquivo em `/queue` some, e a nota continua lá depois de um sono.

### 3. SEC-04 — Recusar atualização sem checksum

Em `firmware/brainos/ota.cpp`, se a release não traz digest nem `brainos.bin.sha256`, a imagem é gravada mesmo assim. As releases atuais publicam o arquivo. Não há assinatura com chave dentro do aparelho; isso fica para "Depois".

- Abortar o install quando não houver hash de 64 dígitos.
- Esforço S.

Conferir: Check for update numa release que tenha `.sha256` continua oferecendo Install. Não instalar de verdade neste passo, a menos que você queira.

### 4. SEC-03 e SEC-07 — Serial de desenvolvimento fora da imagem de release

`notes`, `wipe` e `wifi add` em `firmware/brainos/brainos.ino` não pedem senha. `stt.cpp` imprime a transcrição inteira. O `sdkconfig` do build de 24 Nov 2026 está com secure boot, flash encryption e NVS encryption desligados. Criptografar o flash fica em "Depois": pode inutilizar a placa.

- Esses comandos e o texto da nota no log só existem quando a versão é `dev-…`.
- Na imagem de tag, logar tamanho e código HTTP, não o texto.
- Esforço S.
- Efeito: `firmware/tools/drive.py` deixa de funcionar na imagem de release. Continua funcionando no build local.

Conferir: build local ainda aceita `notes` no serial. Uma imagem com `BRAINOS_VERSION` de tag responde `? notes`.

### 5. SEC-01 — A página só ganha senha com um toque no aparelho

Enquanto `web.pass` está vazio, `POST /setup` em `firmware/brainos/web.cpp` está aberto. Quem chegar primeiro na rede cria a senha e passa a ler, apagar e pedir update. "Reset page password" em `ui.cpp` apaga hash e sessão e reabre essa porta.

- A página não aceita `/setup` até um toque no aparelho armar a criação por cerca de 2 minutos.
- O reset no About também só abre essa janela; não deixa a porta aberta para sempre.
- Esforço M. Muda o primeiro uso. Mostrar o diff e esperar autorização antes de aplicar.

Conferir: de outro celular, `http://<ip>/` antes do toque não oferece criar senha. Depois do toque, dentro da janela, oferece. Com senha já definida, `/setup` continua recusado.

### 6. UX-01 — Uma linha na lock screen quando não há notas

A lock screen (`ui.cpp`, `drawLock`) mostra bateria, data e "0 notes". O gesto de segurar OK não está escrito em lugar nenhum do aparelho.

- Com zero notas: "Segure OK e fale". Some quando a primeira nota existe.
- Esforço S. Muda a tela. Só aplicar com autorização.

## Em seguida

Fazer nesta ordem. O teste vem antes da unificação, senão os dois comportamentos se misturam sem rede de segurança.

### 7. BUG-06 — Testes no computador, sem a placa

Não há testes no repositório. Extrair e cobrir, num executável de host:

- `notesSave` quando a escrita falha (o arquivo antigo permanece).
- hash e comparação da senha da página.
- `parseVersion` / `updCompare`.

Esforço M.

### 8. WASTE-01 — Uma função só para as ações da nota

Pin, archive, tidy, cancel, original e delete estão escritos de novo em `ui.cpp` (~670–686) e `web.cpp` (~376–394). A página tem undo; o aparelho não. A função recebe isso como parâmetro. Não igualar os dois.

Esforço M. Só depois do item 7.

### 9. BUG-03 e SEC-05 — Idioma e o fuso que vêm da rede

`http://ip-api.com` (`net.cpp`) define o fuso e, na primeira vez, `stt.language`. Não há campo para mudar o idioma. Quem está no caminho pode fixar os dois. O fuso automático é um offset fixo, sem horário de verão, e substitui Paris / Nova York / Lisboa quando o SSID muda. Em Brasília isso não desloca o relógio.

- Select de idioma na página. Vazio = o modelo decide.
- Não gravar idioma a partir do ip-api sem uma escolha, ou só enquanto a pessoa não tiver escolhido.
- Se o fuso foi escolhido na página, `locate()` não sobrescreve.
- Esforço M. Muda a página de ajustes: mostrar o diff antes.

### 10. UX-03 — Install em dois toques no aparelho

Na About, Install é um toque. Na página, o botão arma ("Yes, install"). Delete no aparelho já é em dois toques.

- O mesmo gesto do Delete para o Install.
- Esforço S. Muda um gesto: mostrar o diff antes.
- Undo no aparelho fica fora deste passo.

### 11. SEC-06 — Sair da página

Um único token em NVS, cookie de um ano, sem botão de sair. Girar `web.session` derruba todos os navegadores de uma vez (é o que um token só permite). Baixar o `Max-Age` para 30 dias.

Esforço S. A pessoa digita a senha com mais frequência.

### 12. BUG-04 — Completar o que o item 9 não cobrir

Se o item 9 já impedir sobrescrever o fuso escolhido à mão, o que resta é mapear o país para uma zona com horário de verão em vez de montar `<-03>3`. Só vale se alguém fora do Brasil for usar o automático. Esforço M. Não fazer se o uso continua em Brasília.

### 13. BUG-05 — Não cobrar a transcrição duas vezes

Queda de energia depois da resposta da OpenRouter e antes do `notesSave` deixa o WAV no disco, e o boot envia de novo.

Esforço M. Não misturar com o item 2.

### 14. PERF-01 — Teto no ciclo de QR

Na tomada, a exportação troca o QR a cada 1 s e o orçamento de ghosting fica pausado (`export.cpp`, `screen.cpp`). Na bateria o sono corta isso em 3 minutos.

- Um refresh completo entre voltas, ou parar depois de duas voltas até um toque.
- O refresh completo não pode cair em cima de um quadro que o celular está lendo: texto de "aguarde" no lugar do QR.
- Esforço S. Muda a tela de export: mostrar o diff antes.
- Medir antes, no serial: `screen: partial refresh in N ms` durante um minuto na tela de export.

### 15. SEC-09 — Script de instalação no próprio site

`site/index.html` carrega esp-web-tools do unpkg, sem integridade. Copiar o arquivo para o repositório e servir junto da página. Esforço S. A atualização dessa biblioteca passa a ser manual.

### 16. UX-02 — Não dormir no meio da leitura

Três minutos sem toque (`sleep.min`, default 3) derrubam o Wi-Fi mesmo com uma nota aberta. O chat de passagem é descartado ao dormir.

- Nota, chat e teclado usam um prazo maior. A lock screen continua com os 3 minutos, senão o aparelho não dorme.
- Esforço S. Muda quando o aparelho dorme: mostrar o diff antes.

## Depois

- SEC-02. `brain.local` não tem certificado público. HTTPS de verdade implica um modo em que o aparelho é o access point na hora de colar a chave, ou aceitar que a chave entra por HTTP numa Wi-Fi de casa com isolamento de clientes. Não fazer no meio dos itens de cima.
- Flash encryption e secure boot, só com aparelho de teste e backup. Chave de eFuse perdida, ou corte de energia no meio, inutiliza a placa.
- Tradução das strings (UX-04). O guia em português está consistente; a tela é inglesa de propósito até aqui.
- Teclas do teclado (UI-01): letra 42,6 px, vírgula e ponto 34,7 px. Só ajustar depois de medir com o dedo no aparelho.
- Botões da página para 44 px e anel de foco (UI-02). Muda o visual.
- Não reescrever `/notes.jsonl` para um formato incremental (PERF-02). A troca atômica é o que segura queda de energia.

## Conferir fora do código

- Isolamento da Wi-Fi: de outro celular, `http://<ip-do-aparelho>` abre? Se abre, SEC-02 vale nessa rede.
- iPhone e Android: `http://brain.local` acordado. Se falhar, o IP em About.
- Não ler o flash do aparelho do dia a dia. `esptool read-flash 0x9000 0x5000` só num aparelho de teste, e apagar o arquivo depois.
- Painel da OpenRouter: por quanto tempo eles guardam o áudio. O firmware não controla isso.

## De propósito, não mexer

- Rollback de OTA, escape de HTML, checagem de `Origin`, cookie `HttpOnly`, gravação que fica quando a OpenRouter recusa, undo da página que não desfaz transcrição que chegou no meio.
- Os dois drivers de painel e o spike de STT.
- O decoder MP3 do core: o mapa de link mostra as seções em *Discarded*. Não está no binário de 1,72 MB.
