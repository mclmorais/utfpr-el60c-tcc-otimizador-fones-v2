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

### Código da ST

O código em `Drivers/` é cópia sem edição dos repositórios da ST no GitHub, nas versões que o [STM32CubeF7](https://github.com/STMicroelectronics/STM32CubeF7) v1.17.4 usa. O `.gitmodules` do STM32CubeF7 lista o repositório de cada componente, e a tag de cada um sai do commit que o submódulo aponta.

| Componente | Pasta | Versão | Origem |
| --- | --- | --- | --- |
| HAL | `Drivers/STM32F7xx_HAL_Driver` | v1.3.3 | [stm32f7xx-hal-driver](https://github.com/STMicroelectronics/stm32f7xx-hal-driver) |
| CMSIS Device | `Drivers/CMSIS/Device/ST/STM32F7xx` | v1.2.10 | [cmsis-device-f7](https://github.com/STMicroelectronics/cmsis-device-f7) |
| CMSIS Core | `Drivers/CMSIS/Include` | 5.0.8 | Pasta `Drivers/CMSIS/Include` do [STM32CubeF7](https://github.com/STMicroelectronics/STM32CubeF7) v1.17.4 |
| BSP da placa | `Drivers/BSP/STM32F769I-Discovery` | v2.2.1 | [32f769idiscovery-bsp](https://github.com/STMicroelectronics/32f769idiscovery-bsp) |
| Painel OTM8009A | `Drivers/BSP/Components/otm8009a` | v1.0.7 | [stm32-otm8009a](https://github.com/STMicroelectronics/stm32-otm8009a) |
| Painel NT35510 | `Drivers/BSP/Components/nt35510` | v1.0.3 | [stm32-nt35510](https://github.com/STMicroelectronics/stm32-nt35510) |
| Touch FT6x06 | `Drivers/BSP/Components/ft6x06` | v1.0.2 | [stm32-ft6x06](https://github.com/STMicroelectronics/stm32-ft6x06) |
| Ponte HDMI ADV7533 | `Drivers/BSP/Components/adv7533` | v1.0.2 | [stm32-adv7533](https://github.com/STMicroelectronics/stm32-adv7533) |
| Interfaces comuns | `Drivers/BSP/Components/common` | v4.0.1 | [stm32-bsp-common](https://github.com/STMicroelectronics/stm32-bsp-common) |

Do HAL, o projeto guarda todos os headers e só os `.c` que o `cmake/files.cmake` compila.

O código de áudio USB vem do pacote [X-CUBE-USB-AUDIO](https://www.st.com/en/embedded-software/x-cube-usb-audio.html) 1.0.0, a única versão publicada. A ST distribui esse pacote só pelo site, sem repositório no GitHub. Ele foi adaptado ao projeto, então não é cópia fiel:

- `Application/USB_Device_Audio/` e `Application/Streaming/`
- `Drivers/BSP/STM32F769I-Discovery/stm32f769i_discovery_audio_ex.*` e `Drivers/BSP/Components/wm8994/`

Para atualizar um componente de `Drivers/`, copie os arquivos da nova tag por cima dos atuais, compile e teste o áudio USB na placa. Atualize também a versão nesta tabela. O HAL v1.3.0, por exemplo, mudou o tratamento de frames isócronos incompletos e exigiu uma mudança no `usbd_audio.c`.

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

### Formatar e Analisar o Código

O clang-format formata o código e o clang-tidy procura erros comuns. As regras ficam em `.clang-format` e `.clang-tidy`. As bibliotecas da ST em `Drivers/` e `Utilities/` e os headers de imagem, listados em `.clang-format-ignore`, ficam de fora.

Instale as duas ferramentas pelo pip. A versão do clang-format é fixa, porque versões diferentes formatam de jeitos diferentes:

```bash
pip install clang-format==23.1.3 clang-tidy==23.1.4
```

O script `tools/lint.py` roda as ferramentas sobre o código do projeto:

```bash
tools/lint.py format
```

```bash
tools/lint.py check
```

```bash
tools/lint.py tidy
```

`format` reescreve os arquivos, `check` só aponta o que está fora do padrão e `tidy` roda o clang-tidy. O `tidy` usa o `compile_commands.json` gerado pelo CMake, então compile o preset **Debug** antes. A extensão da ST para o VS Code usa o clangd, que lê os mesmos arquivos: o comando **Format Document** segue o `.clang-format` e os avisos do `.clang-tidy` aparecem no editor.

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

As versões e os repositórios de origem estão em [Código da ST](#código-da-st).

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

