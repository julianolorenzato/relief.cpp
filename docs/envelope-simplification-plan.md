# Simplificação com garantia de envelope (half-spaces)

## Objetivo

Garantir que a malha simplificada pelo QEM nunca fique "para dentro" da malha
original em nenhuma região: todo ponto da malha simplificada deve estar do
lado externo (outward) — ou exatamente sobre — a superfície original. Sem
heightmap envolvido: a malha pode vir de qualquer OBJ/GLTF (`loadOBJ`,
`loadGLTF`), então a direção de "fora" é definida localmente pela normal de
cada face original, não por um eixo global.

## Ideia central

Um plano de face original, orientado outward, define um half-space:
`n·p + d ≥ 0`. Como essa é uma função afim de `p`, se os 3 cantos de um
triângulo satisfazem essa desigualdade, qualquer ponto interno do triângulo
também satisfaz (combinação convexa de pontos que satisfazem uma desigualdade
afim também satisfaz). Isso é o que permite usar half-spaces como restrição
de colapso e ainda assim garantir cobertura de toda a footprint do triângulo,
não só dos vértices.

### Por que precisa de acumulação (e não só checar o anel atual)

Ao colapsar `(v1,v2) → V`, a footprint do novo leque de triângulos em torno
de `V` é idêntica à footprint do leque antigo (`star(v1) ∪ star(v2)` menos as
2 faces removidas), desde que o colapso não inverta nenhum triângulo. O novo
leque só fica garantidamente "fora" da original em toda essa footprint se `V`
respeitar os half-spaces de **todas as faces originais já absorvidas** por
`v1` e por `v2` ao longo do histórico de colapsos — não apenas dos planos
atualmente adjacentes. Checar só o anel atual é uma heurística (funciona na
prática, mas não é garantia matemática: um vértice de anel pode já ter
"esquecido" uma face original distante que foi fundida nele há vários passos).

Por isso a acumulação é igual em espírito à de `Q` (`Q_novo = Q_v1 + Q_v2`),
mas em vez de somar uma matriz de tamanho fixo, é uma união de conjuntos de
planos: `envelope_novo = envelope_v1 ∪ envelope_v2`.

## Mudanças de estrutura de dados

`source/qem.h`:

```cpp
struct Vertex {
    ...
    std::vector<Eigen::Vector4d> envelope; // planos (n.x,n.y,n.z,d), outward, acumulados nos colapsos
};
```

`Eigen::Vector4d(n.x, n.y, n.z, d)` reaproveita a mesma convenção de
`quadricFromPlane` (plano como `ax+by+cz+d=0`), só que guardado como vetor,
não como produto externo — porque aqui é usado como restrição, não como
forma quadrática a minimizar.

## Inicialização

Nova função `computeEnvelope()`, paralela a `computeQ()`: para cada face,
calcular a normal outward (mesmo cálculo já usado em `computeQ`,
[qem.cpp:131](../source/qem.cpp#L131)) e empurrar o plano para `envelope` dos
3 vértices da face. Chamada uma vez em `simplify()`, logo após `computeQ()`.

## Critério de colapso

Em `computeCollapse(v1, v2)` ([qem.cpp:144](../source/qem.cpp#L144)), antes
de aceitar um candidato:

1. Montar `planos = envelope_v1 ∪ envelope_v2` (sem deduplicar por igualdade
   exata de início — ver seção de poda).
2. Para cada candidato já testado hoje (`v1.pos`, `v2.pos`, ponto médio),
   verificar se ele satisfaz `n·p + d ≥ -eps` para todo plano em `planos`.
3. Entre os candidatos viáveis, escolher o de menor custo de quádrica (mesmo
   critério de hoje). Se nenhum dos 3 for viável, a aresta não pode colapsar
   nesse passo.

Isso reaproveita a estrutura de 3-candidatos existente
([qem.cpp:154-163](../source/qem.cpp#L154-L163)) em vez de resolver um QP
contínuo — mais simples de implementar e de revisar, ao custo de travar
colapsos que um solver contínuo talvez aceitasse (posição viável que não é
nenhum dos 3 pontos testados). Fica como possível refinamento futuro, não
bloqueante para a primeira versão.

## Arestas inviáveis

Mesmo padrão que já existe para seams (`edgeLocked`,
[qem.h:138-140](../source/qem.h#L138-L140)): se nenhum candidato é viável, a
aresta simplesmente não entra na fila de colapso. Consequência aceita: em
regiões muito não-convexas, a malha pode não atingir `targetFaces` — algumas
arestas ficam permanentemente travadas. Não é tratado como erro, é o preço
da garantia.

## Propagação no merge

Em `mergeVertexPair` ([qem.cpp:305](../source/qem.cpp#L305)), ao lado de
`vertices[keep].Q += vertices[remove].Q`, adicionar a união:

```cpp
auto& dst = vertices[keep].envelope;
auto& src = vertices[remove].envelope;
dst.insert(dst.end(), src.begin(), src.end());
```

## Poda de planos redundantes

**Decisão pendente** (a pergunta anterior ficou sem resposta direta). Duas
opções:

- **Sem poda (recomendado para a primeira versão):** união simples, sem
  checar dominância. Mais simples, valida o conceito rápido. Risco: o vetor
  `envelope` cresce a cada colapso e nunca encolhe — para malhas de asset
  típicas (não terrenos com milhões de faces) isso deve ficar em dezenas de
  planos por vértice, tratável por força bruta; para malhas muito grandes ou
  simplificação muito agressiva, pode ficar caro.
- **Com poda:** ao unir, descartar plano `P` se outro plano do conjunto já
  domina `P` localmente (teste de dominância via LP pequeno, ou heurística
  mais barata por ângulo/distância). Mais código e mais um ponto de bugs;
  só vale a pena se o custo sem poda se mostrar um problema real.

Plano: implementar **sem poda** primeiro, medir custo em malhas de teste, e
só then decidir se poda é necessária.

## Plano de testes

1. Malha convexa simples (esfera, `sphere_original.obj` já existe no repo) —
   simplificar bastante agressivo e confirmar visualmente/por script que
   nenhum vértice da malha simplificada fica para dentro do casco da
   original.
2. Malha com região côncava — confirmar que colapsos ali ficam travados
   (não inviabiliza o resto da simplificação) e que o resultado ainda é
   válido (sem self-intersection introduzida pelo travamento parcial).
3. Comparar contagem final de faces com/sem o modo de envelope ligado, para
   medir quantas arestas ficam bloqueadas na prática.

## Fora de escopo agora

- Resolver QP contínuo para achar o ponto ótimo viável (em vez dos 3
  candidatos discretos).
- Poda de planos redundantes (ver seção acima).
- Qualquer caminho específico de heightmap/raster — descartado a pedido do
  usuário; a solução é genérica para qualquer malha OBJ/GLTF.
