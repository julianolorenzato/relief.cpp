# Da Projeção Escalar à Fórmula da Distância

Ótima pergunta — vamos destrinchar essa passagem com calma.

## Ponto de partida: a projeção escalar

Temos um plano com normal $\vec{n} = (a, b, c)$ e um ponto $Q = (x_1, y_1, z_1)$ que pertence ao plano. Queremos a distância de $P_0 = (x_0, y_0, z_0)$ até esse plano.

O vetor que liga $Q$ a $P_0$ é:

$$\overrightarrow{QP_0} = (x_0 - x_1,\ y_0 - y_1,\ z_0 - z_1)$$

A projeção escalar de $\overrightarrow{QP_0}$ sobre a direção da normal é:

$$D = \frac{\vec{n} \cdot \overrightarrow{QP_0}}{|\vec{n}|}$$

Isso já **é** a distância — geometricamente, é o "quanto" do vetor $\overrightarrow{QP_0}$ aponta na direção perpendicular ao plano. Como a normal é perpendicular ao plano, essa projeção mede exatamente o afastamento do ponto em relação à superfície.

## Passo 1: expandir o produto escalar

$$\vec{n} \cdot \overrightarrow{QP_0} = a(x_0 - x_1) + b(y_0 - y_1) + c(z_0 - z_1)$$

Distribuindo:

$$= ax_0 + by_0 + cz_0 - (ax_1 + by_1 + cz_1)$$

## Passo 2: usar o fato de que Q está no plano

Aqui está o pulo do gato. Como $Q = (x_1, y_1, z_1)$ pertence ao plano, ele satisfaz a equação do plano:

$$ax_1 + by_1 + cz_1 + d = 0 \quad \Rightarrow \quad ax_1 + by_1 + cz_1 = -d$$

Substituindo isso na expressão do Passo 1:

$$\vec{n} \cdot \overrightarrow{QP_0} = ax_0 + by_0 + cz_0 - (-d) = ax_0 + by_0 + cz_0 + d$$

**Essa é a mágica da dedução**: o termo $-(ax_1+by_1+cz_1)$, que dependia do ponto $Q$ escolhido no plano, se transforma em $+d$ — uma constante que já vem da equação do plano. Ou seja, **não importa qual ponto $Q$ do plano você escolheu** para calcular o vetor: o resultado final não depende dessa escolha, só das coordenadas de $P_0$ e dos coeficientes $a, b, c, d$.

## Passo 3: montar a fórmula final

Substituindo de volta na projeção escalar:

$$D = \frac{ax_0 + by_0 + cz_0 + d}{|\vec{n}|} = \frac{ax_0 + by_0 + cz_0 + d}{\sqrt{a^2+b^2+c^2}}$$

Como a distância deve ser sempre positiva (ou zero), e essa expressão pode dar negativo dependendo do lado do plano em que $P_0$ está, coloca-se o valor absoluto:

$$D = \frac{|ax_0 + by_0 + cz_0 + d|}{\sqrt{a^2+b^2+c^2}}$$

E ao quadrado, o módulo já não é mais necessário:

$$D^2 = \frac{(ax_0 + by_0 + cz_0 + d)^2}{a^2+b^2+c^2}$$

## Resumo da lógica

| Etapa | O que acontece |
|---|---|
| Projeção escalar | Mede o quanto $\overrightarrow{QP_0}$ "aponta" na direção normal |
| Expansão | Separa termos de $P_0$ e termos de $Q$ |
| $Q$ está no plano | Substitui os termos de $Q$ por $-d$, eliminando a dependência de $Q$ |
| Resultado | Fórmula só em função de $P_0$ e dos coeficientes do plano |

A ideia central é: **a projeção escalar já era a distância**, só precisávamos reescrever a fórmula de um jeito que não exigisse conhecer um ponto específico do plano — e a equação geral do plano ($ax+by+cz+d=0$) é justamente o que permite fazer essa troca.

Quer que eu mostre visualmente com um diagrama do vetor $\overrightarrow{QP_0}$ se projetando sobre a normal?