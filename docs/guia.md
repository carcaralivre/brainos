# BrainOS: guia rápido (pt-BR)

## Botões e toques
- **Segurar OK** (o botão de cima): grava. Fale, solte. A nota aparece como "Recording, N s" e vira texto quando houver Wi-Fi e a chave do OpenRouter.
- **OK curto**: tranca a tela de onde você estiver; outro clique destranca e volta exatamente para onde estava. No teclado vale como Done; na exportação, como concluir.
- **Dormir**: na bateria, após 30 minutos sem uso, ele vai para a lock screen e dorme fundo; um clique em OK acorda, outro destranca onde você estava. Na tomada não dorme. O Wi-Fi fica sempre ligado enquanto acordado.
- **Cima/Baixo**: passam as páginas da lista; com uma nota aberta, vão para a anterior e a seguinte.
- **Cima + Baixo juntos**: tranca. Segurando os dois por 2 s: dorme (OK acorda).
- **Toque**: na lista, os botões New note e Settings embaixo; uma nota = abre (Pin, Edit, Archive, Delete). Settings tem Wi-Fi, Export, Archive e About; o resto fica em http://brain.local.
- **Fixadas**: até 3; aparecem no topo da lista e na lock screen. Ao fixar uma quarta, a fixada mais antiga é liberada.

## A página brain.local
Com o aparelho na rede, abra http://brain.local no iPhone ou no Mac: lá dá para criar e editar notas, fixar, arquivar e apagar, exportar as novas em Markdown (que as arquiva, como o QR), baixar tudo, checar e instalar atualização, e mudar nome, fuso, modelo de transcrição, som e tempos.

## Rede e chave
1. Settings → Wi-Fi → "Add a network": escolha a rede, digite a senha, Done.
2. No iPhone ou Mac, na mesma rede: `http://brain.local` (ou o IP que aparece em Settings → About). Cole a chave do OpenRouter e Save.
3. O Wi-Fi fica ligado 30 minutos após o último toque e desliga sozinho; qualquer toque religa. Configurável na página.

## Exportar para o celular
Settings → Export: o aparelho mostra os QR codes em sequência. No iPhone, abra a página leitora (https://carcaralivre.github.io/brainos-reader/), toque em Scan e aponte para a tela; a página junta as partes em qualquer ordem. Aperte OK no aparelho quando terminar: as notas vão para o Archive (configurável).

## Atualizar
Settings → About → Check for update; se houver versão nova, aparece o botão Install ali mesmo. O aparelho só procura atualização quando você pede. Também dá para fazer pela página brain.local. Se a versão nova não ligar, o aparelho volta sozinho para a anterior.

## Restaurar o firmware da Seeed
Pelo Playground da Seeed (https://www.seeedstudio.com/sticky/playground/official-firmware/), no Chrome, com o cabo USB.
