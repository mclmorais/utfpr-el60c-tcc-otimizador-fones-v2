# Processador de Áudio e Otimizador de Fones de Ouvido Independente

Este projeto implementa um dispositivo autônomo capaz de processar e otimizar sinais de áudio provenientes de notebooks e celulares quando conectados a fones de ouvido. O dispositivo funciona como um dispositivo USB Audio Class, interceptando o sinal de áudio digital e aplicando processamento digital de sinais (DSP) antes da conversão analógica final.

## Descrição

O projeto visa reduzir gargalos no processo de entrega de áudio, desde o envio em formato digital pelo dispositivo transmissor até a fase final analógica entregue ao fone de ouvido. Sua principal funcionalidade está na melhoria da qualidade de som entregue a fones de ouvido de entrada e média qualidade, sem a necessidade de utilização de equipamentos de alto custo.

### Características Principais

- **Dispositivo USB Audio Class**: Funciona como dispositivo de áudio USB autônomo, sem necessidade de drivers específicos
- **Processamento Digital de Sinais**: Aplica filtros biquad para equalização personalizada
- **Interface Touchscreen**: Permite ajuste visual e interativo dos parâmetros de equalização
- **Persistência de Configurações**: Salva as configurações de equalização na memória flash
- **31 Bandas de Equalização**: Equalizador gráfico de 1/3 de oitava, com as frequências centrais da ISO 266, de 20 Hz a 20 kHz
- **Alta Qualidade**: Conversão e amplificação de alta qualidade para melhor fidelidade sonora

## Hardware

O projeto foi desenvolvido para a placa de desenvolvimento **STM32F769I-Discovery**, que possui:

- **Microcontrolador**: STM32F769NIH6 (ARM Cortex-M7, 216 MHz)
- **Display LCD**: Tela touchscreen integrada
- **Áudio**: Codec WM8994 integrado
- **USB**: Interface USB OTG FS/HS
- **Memória Flash**: Para persistência de configurações

## Estrutura do Projeto

```
.
├── Application/
│   ├── DSP/                    # Processamento Digital de Sinais
│   │   ├── Inc/
│   │   │   └── audio_user_dsp.h
│   │   └── Src/
│   │       └── audio_user_dsp.c    # Filtros biquad e equalização
│   ├── Persistence/            # Persistência de dados
│   │   ├── Inc/
│   │   │   └── flash_persistence.h
│   │   └── Src/
│   │       └── flash_persistence.c  # Salvamento na flash
│   ├── Streaming/              # Streaming de áudio
│   │   ├── Inc/
│   │   └── Src/
│   │       └── audio_usb_playback_session.c  # Sessão de playback USB
│   ├── Touchscreen/            # Interface touchscreen
│   │   ├── Inc/
│   │   └── Src/
│   │       └── touchscreen.c   # Controle de toque e interface
│   ├── USB_Device_Audio/       # Stack USB Audio Class
│   │   ├── Inc/
│   │   └── Src/
│   └── User/                   # Código principal
│       ├── Inc/
│       └── Src/
│           └── main.c          # Função principal
├── Drivers/                    # Drivers HAL e BSP da ST
│   ├── BSP/
│   ├── CMSIS/
│   └── STM32F7xx_HAL_Driver/
└── Utilities/                  # Utilitários (fontes, etc.)
```

## Funcionalidades Técnicas

### Processamento de Áudio

O sistema implementa:

- **Filtros Biquad**: Filtros IIR (Infinite Impulse Response) de segunda ordem para equalização
- **31 Bandas de Frequência**: Equalizador gráfico de 1/3 de oitava, de 20 Hz a 20 kHz
- **Processamento em Tempo Real**: Processamento de amostras de áudio em tempo real via USB
- **Formato PCM**: Suporte a áudio PCM estéreo (2 canais)

### Interface de Usuário

- **Touchscreen**: Interface gráfica para ajuste de equalização
- **Sliders Visuais**: Controles deslizantes para cada banda de frequência, 8 por página
- **Feedback Visual**: Exibição das configurações atuais no display LCD

### Persistência

- **Armazenamento em Flash**: Configurações de equalização são salvas no setor 12 da flash, o primeiro setor do banco 2 (16 KB, endereço `0x08100000`). A flash fica em modo dual-bank. Assim, apagar esse setor não trava o código que roda do banco 1, e o áudio USB continua durante a gravação
- **Restauração Automática**: Configurações são restauradas automaticamente na inicialização
- **Marca de Formato**: A primeira palavra do setor guarda a marca `EQ31`. Os ajustes de 8 bandas salvos por versões antigas do firmware não têm essa marca e são descartados: todas as bandas começam em 0 dB

## Como Compilar e Executar

O projeto usa CMake. Ele foi convertido do STM32CubeIDE na versão 2.0.

### Pré-requisitos

1. **Visual Studio Code** com a extensão **STM32CubeIDE for Visual Studio Code**, da STMicroelectronics.
2. No **STM32Cube Bundles Manager** da extensão, instale:
   - `gnu-tools-for-stm32` (compilador GCC, versão 14.3.1)
   - `cmake` e `ninja`
   - `programmer` (STM32CubeProgrammer)
   - `stlink-gdbserver`
3. **Placa STM32F769I-Discovery** e dois cabos USB com dados:
   - **CN16 (ST-LINK)**: grava e depura o firmware.
   - **USB OTG**: conecta a placa como dispositivo de áudio.

No Linux, instale as regras do udev do ST-LINK para usar a placa sem `root`:

```bash
sudo cp ~/.local/share/stm32cube/bundles/stlink-gdbserver/*/bin/49-stlink*.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Compilar

No VS Code, abra a pasta do projeto, escolha o preset **Debug** e clique em **Build**.

Pelo terminal, com as ferramentas dos bundles no `PATH`:

```bash
cmake --preset Debug
cmake --build --preset Debug
```

O firmware fica em `build/Debug/horoscope.elf`.

### Gravar a Placa

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/horoscope.elf -v -rst
```

A gravação apaga só os setores do programa. As configurações do equalizador, salvas no setor 12, são mantidas.

#### Configurar a flash em dual-bank (uma vez por placa)

A placa vem de fábrica com a flash em modo single-bank (`nDBANK=1`). O firmware precisa do modo dual-bank (`nDBANK=0`): em single-bank, o botão "Salvar" não grava nada. A troca de modo muda o mapa da flash e embaralha o conteúdo gravado. Por isso, apague o chip e grave o firmware de novo:

```bash
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -ob nDBANK=0
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -e all
STM32_Programmer_CLI -c port=SWD -w build/Debug/horoscope.elf -v -rst
```

Confira com `STM32_Programmer_CLI -c port=SWD -ob displ`. A linha `nDBANK` deve mostrar `0x0`. Os ajustes do equalizador salvos antes da troca são perdidos: todas as bandas começam em 0 dB.

### Capturar a Tela do LCD

O script `tools/lcd_capture.py` lê a imagem do LCD pelo ST-LINK e salva um PNG. O firmware continua funcionando durante a leitura, que leva cerca de 10 segundos. Ele precisa do Python com a biblioteca Pillow.

```bash
tools/lcd_capture.py lcd.png
```

## Como Usar

1. **Conectar o Dispositivo**:
   - Conecte a placa STM32F769I-Discovery ao computador via USB
   - O dispositivo será reconhecido como um dispositivo USB Audio Class

2. **Selecionar como Saída de Áudio**:
   - No sistema operacional (Windows/Linux/Mac), selecione o dispositivo "Horoscope" ou similar como saída de áudio padrão
   - O dispositivo aparece como um dispositivo de áudio USB

3. **Ajustar a Equalização**:
   - Toque no botão "EQ" para abrir a tela do equalizador
   - A tela mostra 8 bandas por vez, em 4 páginas: 20 Hz a 100 Hz, 125 Hz a 630 Hz, 800 Hz a 4 kHz e 5 kHz a 20 kHz
   - Use os botões "<" e ">", à direita do botão "EQ", para trocar de página. Da última página, ">" volta para a primeira
   - Arraste o slider de cada banda para ajustar o ganho, de -15 dB a +15 dB, em passos de 1 dB
   - Toque em "Salvar" para gravar os ajustes na flash, em "Desfazer" para voltar aos ajustes salvos e em "Redefinir" para voltar todas as bandas a 0 dB

4. **Conectar o Fone de Ouvido**:
   - Conecte o fone de ouvido na saída de áudio da placa
   - O áudio processado será reproduzido através do fone

## Detalhes Técnicos

### Especificações de Áudio

- **Formato**: PCM (Pulse Code Modulation)
- **Canais**: Estéreo (2 canais: esquerdo e direito)
- **Resolução**: 16 bits por amostra
- **Frequências Suportadas**: Configurável (padrão: 48 kHz)

### Filtros Biquad

O sistema utiliza filtros biquad do tipo "peaking equalizer" para cada banda de frequência. Os coeficientes são calculados usando as fórmulas do Audio EQ Cookbook:

- **Ganho**: Ajustável de -15 dB a +15 dB por banda
- **Frequência Central**: 31 frequências da ISO 266, de 20 Hz a 20 kHz, em passos de 1/3 de oitava
- **Largura de Banda**: 1/3 de oitava em todas as bandas

### Arquitetura de Processamento

```mermaid
flowchart LR
    A[USB Audio Input] --> B[Buffer]
    B --> C[DSP Processing]
    C --> D[I²S Output]
    D --> E[Codec]
    E --> F[Fone de Ouvido]
    C --> G[Filtros Biquad<br/>31 bandas]
    G --> C
    C --> H[Persistência Flash]
    H --> C
    
    style A fill:#e1f5ff
    style C fill:#fff4e1
    style G fill:#ffe1f5
    style H fill:#e1ffe1
    style F fill:#e1f5ff
```

## Referências

Este projeto foi desenvolvido como Trabalho de Conclusão de Curso (TCC) de Engenharia Eletrônica na UTFPR.

### Bibliotecas e Frameworks Utilizados

- **STM32 HAL**: Hardware Abstraction Layer da STMicroelectronics
- **USB Device Library**: Stack USB da STMicroelectronics
- **CMSIS**: Cortex Microcontroller Software Interface Standard

### Documentação Relacionada

- [STM32F769I-Discovery User Manual](https://www.st.com/en/evaluation-tools/stm32f769i-disco.html)
- [USB Audio Class Specification](https://www.usb.org/documents)
- [Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)

## Troubleshooting

### O dispositivo não é reconhecido como áudio USB

- Verifique se o cabo USB está conectado corretamente
- Verifique se o firmware foi programado corretamente na placa
- No Linux, verifique com `lsusb` se o dispositivo aparece
- No Windows, verifique no Gerenciador de Dispositivos

### Áudio não está sendo reproduzido

- Verifique se o dispositivo foi selecionado como saída de áudio padrão
- Verifique a conexão do fone de ouvido na placa
- Verifique se há áudio sendo enviado pelo sistema operacional

## Licença

Este projeto foi desenvolvido como trabalho acadêmico. Consulte os arquivos de licença individuais dos componentes utilizados (STM32 HAL, USB Device Library, etc.).

## Autor

**Marcelo Fernandes de Morais Filho**

Trabalho de Conclusão de Curso de Graduação em Engenharia Eletrônica  
Universidade Tecnológica Federal do Paraná (UTFPR)  
Orientador: Prof. Dr. Rafael Eleodoro de Góes

---

*"Somebody was trying to tell me that CDs are better than vinyl because they don't have any surface noise. I said, 'Listen, mate, life has surface noise."* - John Peel

