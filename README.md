# Log Quality Follower
O projeto denominado como 'Log Quality Follower' tem como objetivo realizar o rastreio de cargas nos mais diversos locais com a utilização de sinal GPS que deverá informar a localidade, data e horario, alem desses o sistema tambem poderá entregar informações da qualidade da viagem como o nivel de vibração pela qual o material transportado está sendo submetido durante seu trejeto. Todos os dados coletados deveram ser transmitidos a um broken MQTT por meio de uma Rede CAT-M1.

A principio o projeto conta com desenvolvimento baseado na placa DEMO LilyGO-T-SIM7000g, a qual é descrevida no item referente ao hardware e tambem pode ser melhor estudade através do seu repositorio proprio:
https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G

### Hardware Describe
O projeto foi desenvolvido com base em uma DEMO Board de ESP32 chamada de T-SIM7000g (2020 04 15)

![T-SIM7000G-2021](https://user-images.githubusercontent.com/81943185/150889211-c1098896-2e1c-43e8-9965-a58b85a1e52e.jpg)

A placa possui como principais chips:
    ESP32-WROVER-B
    SIM7070g

O hardware tambem possui controle de carga de bateria por sistema solar
O chip responsavel pelo controle de carga de bateria é:
    DW01FA

A placa possui:
    Um dock para baterias 18650;
    Uma entrada USB tipo C;
    Conector para Antena LTE;
    Conector para antena GPS;
    Dock para MicroSD Card;
    Dock para NanoSIM card.

### Firmware Workflow

Uma vez que a placa esteja alimentada pela bateria, o Firmware inicializará os I/O's e parâmetros básicos de operação.
O uC deverá realizar as primeiras configurações com módulo GSM (BaudRate, ECO, Rede de comunicação, Bandas, etc...), caso não haja problemas durate essa etapa (como falta de sinal), o sistema deverá realizar a primeira verificação de localização e enviar ao broken para análise dos operadores.

Para realizar o envio da localização, o firmware fica em loop aguardando o triangulamento do GPS, momento em que as informações de longitude, latitude, horas, etc.. são enviadas de maneira correta.
Uma vez triangulado e coletado as informações o código trata a mensagem, verifica os demais periféricos e então faz o envio dos dados ao Broken, confirmando o correto funcionamento do GPS.

O processo de envio das mensagens ao broken, se inicia com o desligamento do GPS, para que o LTE comece a operar, uma vez conectado à rede LTE nas bandas configuradas o sistema faz a sincronização com broken (login) e então envia os dados coletados.

Uma vez finalizado o primeiro ciclo, o sistema entra em modo de operação normal. Durante esse período são realizadas medições constantes do acelerômetro/giroscópio afim de verificar vibrações ou tombamentos.

Durante períodos de 30 min (valor configurável) o sistema irá realizar o processo de: "Captura de localização" e envio de dados. O processo de captura de localização consiste no ligamento do GPS, triangulamento e processamento da mensagem de localização. 

Já o envio de dados consiste na junção dos dados processados do GPS e parte das informações geradas pelo acelerômetro/giroscópio. O pacote então é enviado via MQTT ao broken pela rede CAT-M1. Caso haja erro de envio (sem sinal), essas informações são salvas na memória temporária, é acionado uma função de verificação de sinal a qual monitora periodicamente o status de sinal e quando possível realiza o envio dos dados que estão em fila. Uma vez presente no broken eles podem ser processados e separados pela hora coletada no GPS.

A cada envio de mensagem ao broken o dispositivo fica aberto por um período a receber mensagens. É possível então acionar o modo de envio constante de mensagens caso seja observado alguma irregularidade (desvio de rota, vibração excessiva, tombamento, etc...).

