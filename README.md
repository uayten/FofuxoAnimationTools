# Fofuxo's Animation Tools

Plugin de editor para Unreal Engine 5.8. Duas coisas, que na prática são a mesma:
**tirar animação da Unreal** e **fazer o retarget não doer**.

Nasceu de um pipeline concreto — personagem rigado no Blender, retargetado na
Unreal, exportado de volta — e cada botão aqui existe porque uma etapa desse
caminho estava custando meia hora por dia.

---

## Exportar

O exportador de FBX da Unreal escreve uma animação por arquivo e não nomeia os
takes. Quem recebe do outro lado — Blender, Maya, Unity — abre trinta arquivos e
vê trinta takes chamados `Take 001`.

**Clique com o botão direito em Animation Sequences no Content Browser →
`Fofuxo -- Exportar`.**

- Todas as animações marcadas e um Skeletal Mesh num arquivo só, **cada animação
  como um take com o nome do asset**.
- **FBX, USD e glTF**, no mesmo diálogo.
- **Destinos**: perfis salvos de eixo para cima, frente, unidade e escala — um
  para o Blender, outro para o Unity, e não se pensa mais no assunto.
- **Animações por arquivo**: acima de um limite, o export se quebra em vários
  arquivos numerados. Alguns importadores engasgam com FBX de centenas de takes.
- Sem nenhuma animação marcada, sai só a malha.

Também há o comando de console, para script e para build:

```
Fofuxo.Exportar <saida.fbx> <caminho da malha> [pasta das animacoes] [Unity]
```

---

## IK Retargeter

Tudo abaixo aparece na barra do editor de IK Retargeter, numa seção **Fofuxo**.

### Live Retarget

O problema da mão. A pose de retarget se edita olhando o *ref pose*, e no ref
pose a mão está aberta — não dá para ver se os dedos fecham na espada, que é a
única coisa que importa nos dedos. Você só descobre que estão errados quando a
animação roda, e aí o editor de pose já não está mais ligado.

Com o Live Retarget ligado, o gizmo aparece **no Running Retarget**: pare a
animação no frame que quiser, clique num osso do alvo e gire.

O que o gizmo escreve continua sendo a pose de retarget, e não um ajuste daquele
frame — o retargeter não tem onde guardar correção por frame. Mas a conta faz
isso valer a pena. Numa cadeia FK a saída de um osso é

```
Saída(B) = DeltaDaFonte(B) · PoseDeRetarget(B)
```

Pós-multiplicar um `X` na pose de retarget pós-multiplica o mesmo `X` na saída,
**em qualquer frame**. Então girar o dedo olhando o frame 37 escreve o offset que
produz exatamente aquele giro no frame 37 — e o mesmo giro, em espaço de mundo,
em todos os outros. Para dedo isso é o certo: o erro de um dedo que segura uma
espada é constante, e o frame só serve para você enxergá-lo.

Só no alvo. A animação da fonte é o dado de entrada; não há o que ajustar nela.

### Painel Transforms editável

O painel de detalhes do osso é da engine, mas destravado: no Live Retarget dá
para **digitar** a rotação, e não só arrastar o gizmo. São os dois lados da mesma
escrita, e não havia motivo para um deles estar trancado.

### Alt+R

Devolve ao ref pose a rotação dos ossos selecionados. Vale nos dois modos —
inclusive com a animação rodando, onde o *Reset Selected Bones* da engine não
executa. É um comando registrado, então aparece em **Editar → Preferências do
Editor → Atalhos de Teclado** e a tecla pode ser trocada.

### Esticar

Alinha osso na hora de montar a pose de retarget. Quatro modos, no menu da
setinha ao lado do botão:

| modo | o que faz |
|---|---|
| Selecionados | alinha cada osso com a rotação do pai — deixa o dedo reto |
| Com filhos | o mesmo, descendo a cadeia |
| No último | alinha a cadeia inteira pela ponta |
| No mundo | aponta a ponta do osso para um eixo do mundo |

O **No mundo** é o que casa arma e mão entre dois esqueletos diferentes: a
referência é externa aos dois, então eles batem sem ninguém medir nada.

### Espelhar

Repete no osso do outro lado a rotação que você deu em um osso — rodou o
`thigh_l`, o `thigh_r` acompanha espelhado. Acha o par pelo nome (`l`/`r`,
`left`/`right`, `lt`/`rt`, separados por `_`, `.`, `-`, espaço, ou colados em
camelCase). Osso sem par fica de fora.

Se você mexer nos dois lados de uma vez — os dois selecionados no gizmo, um Auto
Align geral, um Ctrl+Z — o espelho não entra.

### Copiar pose

Traz para o lado que você está editando a pose de retarget **de outro
retargeter**, casando os ossos pelo nome. Serve para o conserto que não viaja: se
todo retarget do projeto sai do mesmo boneco, o lado fonte de todos eles tem a
mesma pose, e ajustar um não ajusta os outros.

Há também **pose em asset**, que atravessa projeto: salve a pose do Manny num
asset e aplique noutro projeto, ou num MetaHuman.

### Anexos de Preview (Fofuxo)

Um retarget op, na pilha. Pendura uma malha num osso — a espada na mão — só para
o visor.

Não é o mesmo que o *Add Preview Asset* do editor de esqueleto: aquele mora na
`USkeletalMesh` e na `USkeleton`, e some quando o rig é reimportado como asset
novo. Este mora no retargeter, que é o asset que sobrevive à troca dos dois
bonecos.

Cada linha tem:

- **Boneco** — fonte, alvo, ou os dois (a mesma arma nos dois lados, para
  comparar).
- **Osso na fonte / Osso no alvo** — dois campos, porque os dois esqueletos quase
  nunca chamam o mesmo osso pelo mesmo nome.
- **Deslocar o osso** — move o osso do alvo, e **sai nas animações exportadas**.
  É o que conserta a arma não estar na mão. Com o Live Retarget ligado, dá para
  arrastar isso pelo gizmo de translação.
- **Encaixe do preview** — move só a malha pendurada, morre no visor. Serve para
  pivô torto do modelo e para o lado da fonte, onde o deslocamento não alcança.
- **Alinhar no mundo** — o mesmo alinhamento do botão Esticar, dentro do op.

> **Este op tem que ficar depois do FK Chains e do Run IK Rig na pilha.** Os ops
> rodam em ordem e quem escreve por último manda: com ele em cima, o FK Chains
> recalcula o osso depois e o deslocamento se perde sem dizer nada.

### Visor da fonte

**Window → Fonte (Fofuxo)**, ou o botão na barra. Um segundo visor da *mesma*
cena, com a câmera colada no osso da fonte que corresponde ao osso selecionado —
e seguindo ele quadro a quadro enquanto a animação roda.

Num visor você gira o dedo do alvo; no outro vê como o dedo do gabarito está
naquele mesmo quadro, sem viajar de câmera entre os dois bonecos.

O correspondente sai do mapeamento de cadeias: achada a cadeia do alvo que contém
o osso, é o osso na mesma posição proporcional da cadeia mapeada.

### Ossos em vareta, e clicar neles

Duas coisas diferentes com a mesma origem: numa mão, os ossos da Unreal são
pequenos, muitos e quase impossíveis de acertar.

**Clicar perto passa a bastar** — sempre, sem interruptor. A Unreal seleciona por
hit proxy e lê *um* pixel, o que está debaixo do cursor; aqui se lê uma caixa de
22 pixels em volta e escolhe-se o osso mais próximo do centro. O proxy encontrado
volta para o modo da própria engine, que faz a seleção do jeito dele — painel de
detalhes, hierarquia e gizmo se atualizam sozinhos.

**Ossos em vareta** (botão na barra) troca o octaedro da Unreal por cilindro e
esfera **de tamanho constante na tela**: o osso da engine é medido em unidades de
mundo, então o mesmo desenho que é uma bola no pulso some quando a câmera afasta.

O desenho da engine não some, encolhe — é nele que mora a identidade do osso para
o clique. Ele fica escondido debaixo da vareta. O tamanho encolhido é o
`BoneDrawSize` do retargeter, o mesmo valor da régua em **Character → Bones**;
desligar devolve o valor de antes.

### Exportar animações

No Asset Browser do editor de retarget:

- **Export Selected Animations** — o batch retarget da engine, sem sair do editor.
- **Refazer as já exportadas** — reexporta o que já foi exportado antes, com os
  mesmos destinos. Depois de mexer na pose de retarget, é um clique.

---

## Instalar

Copie a pasta para `SeuProjeto/Plugins/` e abra o projeto. A Unreal compila na
primeira abertura.

Depende do plugin **IK Rig** (vem com a engine) e, para os formatos de cena, de
**USD Importer** e **glTF Exporter** — todos declarados no `.uplugin`.

## Compilar

O editor precisa estar **fechado**: o Live Coding tranca a build inteira
enquanto ele estiver aberto, e nem mostra o erro de compilação.

```
"<Engine>/Build/BatchFiles/Build.bat" <Projeto>Editor Win64 Development -Project="<caminho>/<Projeto>.uproject" -WaitMutex
```

Quando a Unreal só disser *"could not be compiled. Try rebuilding from source
manually"*, é essa linha que mostra o erro de verdade. Apagar `Intermediate/` e
`Binaries/` quase nunca é a solução — compile primeiro e leia o erro.

## Trocar o nome do plugin

São dois lugares, e só dois:

1. `FriendlyName` no `FofuxoExporter.uplugin`.
2. `FOFUXO_NOME` e `FOFUXO_NOME_CURTO` em
   [`Source/FofuxoComum/FofuxoNome.h`](Source/FofuxoComum/FofuxoNome.h), de onde
   sai todo texto de interface.

O nome da pasta e os nomes dos módulos são encanamento — aparecem em caminhos de
arquivo e no `IMPLEMENT_MODULE`, e trocá-los dá trabalho sem mudar nada que se
veja.

## Limites conhecidos

- **Escrever Behavior Tree, não.** Isto é um plugin de animação; nada aqui toca
  IA.
- **A aba do Visor da fonte não volta sozinha** quando o editor de retarget
  reabre: o registro dela acontece meio segundo depois, e o layout salvo já foi
  restaurado antes disso. Reabra por Window.
- **Salvar o RTG com as varetas ligadas grava o `BoneDrawSize` encolhido.**
  Desligar devolve o valor e o próximo save conserta.
- **Ctrl+Z no Live Retarget às vezes derruba o modo** de volta para o Editing
  Retarget Pose. O `PostUndo` do retargeter refaz as preview meshes e mexe no
  playback, e todos os setters para consertar isso de dentro são não exportados
  da `IKRigEditor`. Há uma correção condicional, que reexecuta o comando da barra
  quando o modo cai sozinho.
