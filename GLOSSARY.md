# Otimizador de Fones

Dispositivo de áudio USB que recebe o som do computador ou do celular e o altera, conforme o gosto de quem ouve, antes de entregá-lo ao fone de ouvido.

## Linguagem

### Processamento

**Módulo** (`module`):
Uma etapa de processamento que altera o som e que o usuário liga, desliga e ajusta. O conjunto de módulos é fixo no firmware.
_Evitar_: efeito, plugin, filtro

**Cadeia** (`chain`):
A sequência fixa de módulos pela qual passa todo o áudio, sempre na mesma ordem.
_Evitar_: pipeline, rack

### Equalização

**Equalizador** (`Equalizer`):
Módulo que muda o volume de cada faixa de frequência de forma independente. Na tela, aparece como "EQ".
_Evitar_: filtro, tonalidade

**Banda** (`band`):
Uma das 31 faixas de 1/3 de oitava do Equalizador, identificada pela frequência central da ISO 266, com ganho próprio.
_Evitar_: canal, faixa

### Espacialização

**Palco sonoro** (`soundstage`):
A posição e a largura em que o ouvinte percebe as fontes de som. No fone, o som tende a parecer dentro da cabeça.
_Evitar_: largura estéreo, abertura

**Virtualização** (`Virtualizer`):
Módulo que tira o som de dentro da cabeça e o faz parecer vindo de alto-falantes à frente do ouvinte, ampliando o Palco sonoro.
_Evitar_: alargamento estéreo, widening, som 3D

**Técnica** (`technique`):
A forma como a Virtualização é feita. Só uma Técnica fica ativa por vez.
_Evitar_: modo, algoritmo

**Crossfeed** (`crossfeed`):
Técnica de Virtualização que leva a cada ouvido uma cópia atrasada e abafada do canal oposto, como acontece ao ouvir alto-falantes.
_Evitar_: mistura de canais, cross-talk

**Preset** (`preset`):
Um ajuste de Crossfeed publicado e com nome (Padrão, Chu Moy, Jan Meier), definido pela frequência de corte e pelo nível do canal oposto.
_Evitar_: perfil, modo, nível

**Alto-falantes virtuais** (`virtual speakers`):
Técnica de Virtualização, planejada para depois do Crossfeed, que simula dois alto-falantes à frente a partir de respostas medidas da cabeça humana (HRTF).
_Evitar_: HRTF (nome da medida, não da técnica), surround
